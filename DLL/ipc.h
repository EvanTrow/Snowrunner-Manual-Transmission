#pragma once
#include "shared.h"

// Local named-pipe remote control server. Lets an external process (e.g. a
// PowerShell script) drive the same actions available to key/controller
// bindings by writing newline-terminated command lines to
// \\.\pipe\SnowRunnerMT and reading a response line back.
//
// Disabled by default; enable via [OPTIONS] ENABLE REMOTE CONTROL in SMT.ini
// or the in-game menu.

extern void InitIPC();
extern void ShutdownIPC();

// Pops the next queued remote command (if any). Called from the existing
// input-processing thread so remote commands run on the same thread that
// already owns access to the vehicle/game memory.
extern bool PopRemoteCommand(std::string& out);
