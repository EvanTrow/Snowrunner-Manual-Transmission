# SnowRunnerMT.psm1
#
# PowerShell client for the SnowRunner Manual Transmission mod's remote
# control pipe. Requires [OPTIONS] ENABLE REMOTE CONTROL = true in SMT.ini
# (or toggled on in the in-game menu) while SnowRunner is running with the
# mod loaded.
#
# Import-Module .\SnowRunnerMT.psm1
# Test-SMTConnection
# Set-SMTGear 3
# Get-SMTStatus

$script:SMTPipeName = "SnowRunnerMT"

function Send-SMTCommand {
    <#
    .SYNOPSIS
        Sends a raw command line to the SMT mod and returns its response.
    .EXAMPLE
        Send-SMTCommand "GEAR 3"
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, Position = 0)]
        [string]$Command,

        [int]$TimeoutMs = 2000
    )

    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $script:SMTPipeName, [System.IO.Pipes.PipeDirection]::InOut)
    try {
        try {
            $pipe.Connect($TimeoutMs)
        }
        catch [TimeoutException] {
            throw "Could not connect to the SMT remote control pipe. Is SnowRunner running with the mod loaded, and is [OPTIONS] ENABLE REMOTE CONTROL turned on?"
        }

        $writer = New-Object System.IO.StreamWriter($pipe)
        $writer.AutoFlush = $true
        $reader = New-Object System.IO.StreamReader($pipe)

        $writer.WriteLine($Command)
        $response = $reader.ReadLine()

        if ($null -eq $response) {
            throw "SMT pipe closed the connection without responding."
        }

        return $response
    }
    finally {
        $pipe.Dispose()
    }
}

function Test-SMTConnection {
    <#
    .SYNOPSIS
        Returns $true if the SMT mod's remote control pipe is reachable.
    #>
    [CmdletBinding()]
    param()
    try {
        return (Send-SMTCommand "PING") -eq "PONG"
    }
    catch {
        return $false
    }
}

function Get-SMTStatus {
    <#
    .SYNOPSIS
        Returns the current transmission state (gear, max gear, auto mode, range,
        AWD, diff lock, handbrake).
    #>
    [CmdletBinding()]
    param()

    $response = Send-SMTCommand "STATUS"
    if ($response -notmatch '^OK\s') {
        throw "SMT error: $response"
    }

    $result = [ordered]@{ Vehicle = $false }
    foreach ($token in ($response -split '\s+' | Select-Object -Skip 1)) {
        $key, $value = $token -split '='
        switch ($key) {
            'VEHICLE' { $result.Vehicle = $value -eq '1' }
            'AUTO' { $result.Auto = $value -eq '1' }
            'AWD' { $result.AWD = $value -eq '1' }
            'DIFFLOCK' { $result.DiffLock = $value -eq '1' }
            'HANDBRAKE' { $result.Handbrake = $value -eq '1' }
            default { $result[$key] = [int]$value }
        }
    }
    [PSCustomObject]$result
}

function Get-SMTActionList {
    <#
    .SYNOPSIS
        Returns the list of valid action names the mod accepts (e.g. "GEAR 3", "GEAR UP").
    #>
    [CmdletBinding()]
    param()

    $response = Send-SMTCommand "LIST"
    if ($response -notmatch '^OK\s') {
        throw "SMT error: $response"
    }
    ($response -split '\s+' | Select-Object -Skip 1)
}

function Invoke-SMTAction {
    <#
    .SYNOPSIS
        Triggers a raw mod action by name, e.g. "GEAR 3", "GEAR UP", "CLUTCH",
        "RANGE HIGH", "SHOW MENU". Use Get-SMTActionList for the full set.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, Position = 0)]
        [string]$Action
    )

    $response = Send-SMTCommand $Action
    if ($response -notmatch '^OK\s') {
        throw "SMT error: $response"
    }
    $response
}

function Set-SMTGear {
    <#
    .SYNOPSIS
        Shifts directly to the given numbered gear (1-12).
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, Position = 0)]
        [ValidateRange(1, 12)]
        [int]$Gear
    )
    Invoke-SMTAction "GEAR $Gear"
}

function Set-SMTNeutral { Invoke-SMTAction "GEAR N" }
function Set-SMTReverse { Invoke-SMTAction "GEAR R" }
function Set-SMTHighGear { Invoke-SMTAction "GEAR H" }
function Set-SMTLowGear { Invoke-SMTAction "GEAR L" }
function Set-SMTLowPlusGear { Invoke-SMTAction "GEAR L+" }
function Set-SMTLowMinusGear { Invoke-SMTAction "GEAR L-" }
function Invoke-SMTGearUp { Invoke-SMTAction "GEAR UP" }
function Invoke-SMTGearDown { Invoke-SMTAction "GEAR DOWN" }
function Invoke-SMTRangeHigh { Invoke-SMTAction "RANGE HIGH" }
function Invoke-SMTRangeLow { Invoke-SMTAction "RANGE LOW" }
function Show-SMTMenu { Invoke-SMTAction "SHOW MENU" }
function Invoke-SMTToggleAWD { Invoke-SMTAction "AWD" }
function Invoke-SMTToggleDiffLock { Invoke-SMTAction "DIFF LOCK" }
function Invoke-SMTToggleHandbrake { Invoke-SMTAction "HANDBRAKE" }

Export-ModuleMember -Function `
    Send-SMTCommand, `
    Test-SMTConnection, `
    Get-SMTStatus, `
    Get-SMTActionList, `
    Invoke-SMTAction, `
    Set-SMTGear, `
    Set-SMTNeutral, `
    Set-SMTReverse, `
    Set-SMTHighGear, `
    Set-SMTLowGear, `
    Set-SMTLowPlusGear, `
    Set-SMTLowMinusGear, `
    Invoke-SMTGearUp, `
    Invoke-SMTGearDown, `
    Invoke-SMTRangeHigh, `
    Invoke-SMTRangeLow, `
    Show-SMTMenu, `
    Invoke-SMTToggleAWD, `
    Invoke-SMTToggleDiffLock, `
    Invoke-SMTToggleHandbrake
