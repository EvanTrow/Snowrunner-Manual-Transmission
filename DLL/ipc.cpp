#include "shared.h"
#include "ipc.h"
#include "config.h"
#include "input.h"
#include <mutex>
#include <queue>
#include <algorithm>

// Simple line-based remote control protocol over a local named pipe.
//
// Any line matching a key in `bindFunctions` (e.g. "GEAR 3", "GEAR UP",
// "CLUTCH", "RANGE HIGH", "SHOW MENU") is queued and executed on the
// existing input-processing thread, exactly as if it were triggered by a
// bound key/button. This keeps all game-memory access on the single thread
// that already owns it instead of adding a second writer.
//
// Additional commands:
//   PING   -> PONG                                             (always available)
//   STATUS -> OK VEHICLE=<0|1> GEAR=<n> MAXGEAR=<n> AUTO=<0|1> RANGE=<-1|0|1>
//             AWD=<0|1> DIFFLOCK=<0|1> HANDBRAKE=<0|1>
//   LIST   -> OK <space separated list of valid action names>
//
// Everything but PING is refused unless [OPTIONS] ENABLE REMOTE CONTROL is
// true, and the pipe itself is not created at all while the option is off.

static const char* PIPE_NAME = R"(\\.\pipe\SnowRunnerMT)";

std::atomic<bool> keepAliveIPC = true;

static std::mutex commandQueueMutex;
static std::queue<std::string> commandQueue;

static void QueueRemoteCommand(const std::string& cmd) {
	std::lock_guard<std::mutex> lock(commandQueueMutex);
	commandQueue.push(cmd);
}

bool PopRemoteCommand(std::string& out) {
	std::lock_guard<std::mutex> lock(commandQueueMutex);
	if (commandQueue.empty()) {
		return false;
	}
	out = commandQueue.front();
	commandQueue.pop();
	return true;
}

static std::string BuildStatusResponse() {
	if (!remoteVehiclePresent.load()) {
		return "OK VEHICLE=0";
	}
	std::ostringstream oss;
	oss << "OK VEHICLE=1 GEAR=" << remoteGearSnapshot.load()
		<< " MAXGEAR=" << remoteMaxGearSnapshot.load()
		<< " AUTO=" << (remoteAutoSnapshot.load() ? 1 : 0)
		<< " RANGE=" << range.load()
		<< " AWD=" << (remoteAWDSnapshot.load() ? 1 : 0)
		<< " DIFFLOCK=" << (remoteDiffLockSnapshot.load() ? 1 : 0)
		<< " HANDBRAKE=" << (remoteHandbrakeSnapshot.load() ? 1 : 0);
	return oss.str();
}

static std::string HandleIPCLine(const std::string& rawLine) {
	std::string cmd = rawLine;
	while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == ' ' || cmd.back() == '\t')) {
		cmd.pop_back();
	}
	size_t start = cmd.find_first_not_of(" \t");
	if (start == std::string::npos) {
		return "ERR EMPTY";
	}
	cmd = cmd.substr(start);

	std::string upper = cmd;
	std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

	if (upper == "PING") {
		return "PONG";
	}

	if (!iniConfig["OPTIONS"]["ENABLE REMOTE CONTROL"].as<bool>()) {
		return "ERR REMOTE CONTROL DISABLED";
	}

	if (upper == "STATUS") {
		return BuildStatusResponse();
	}
	if (upper == "LIST") {
		std::string list = "OK";
		for (auto& entry : bindFunctions) {
			list += " " + entry.first;
		}
		return list;
	}
	if (bindFunctions.contains(upper)) {
		QueueRemoteCommand(upper);
		return "OK QUEUED " + upper;
	}
	return "ERR UNKNOWN " + cmd;
}

static DWORD WINAPI IPCServerThread(LPVOID) {
	LogMessage("IPC: server thread started, pipe name:", PIPE_NAME);
	while (keepAliveIPC) {
		if (!iniConfig["OPTIONS"]["ENABLE REMOTE CONTROL"].as<bool>()) {
			Sleep(500);
			continue;
		}

		HANDLE hPipe = CreateNamedPipeA(
			PIPE_NAME,
			PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			4096, 4096, 0, nullptr);

		if (hPipe == INVALID_HANDLE_VALUE) {
			LogMessage("IPC: CreateNamedPipe failed", GetLastError());
			Sleep(1000);
			continue;
		}

		BOOL connected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
		if (connected) {
			LogMessage("IPC: client connected");
			std::string pending;
			char buffer[4096];
			DWORD bytesRead = 0;
			while (keepAliveIPC && ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
				pending.append(buffer, bytesRead);
				size_t pos;
				while ((pos = pending.find('\n')) != std::string::npos) {
					std::string line = pending.substr(0, pos);
					pending.erase(0, pos + 1);
					std::string response = HandleIPCLine(line) + "\n";
					DWORD written = 0;
					WriteFile(hPipe, response.c_str(), (DWORD)response.size(), &written, nullptr);
				}
			}
			LogMessage("IPC: client disconnected");
		}

		DisconnectNamedPipe(hPipe);
		CloseHandle(hPipe);
	}
	LogMessage("IPC: server thread exiting");
	return 0;
}

static HANDLE ipcThreadHandle = nullptr;

void InitIPC() {
	keepAliveIPC = true;
	ipcThreadHandle = CreateThread(nullptr, 0, IPCServerThread, nullptr, 0, nullptr);
}

void ShutdownIPC() {
	keepAliveIPC = false;

	// Unblock a thread parked in ConnectNamedPipe/ReadFile so it can exit.
	HANDLE dummy = CreateFileA(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (dummy != INVALID_HANDLE_VALUE) {
		CloseHandle(dummy);
	}

	if (ipcThreadHandle) {
		WaitForSingleObject(ipcThreadHandle, 2000);
		CloseHandle(ipcThreadHandle);
		ipcThreadHandle = nullptr;
	}
}
