# Single-account standalone initialization/advertisement/clean shutdown only.
[CmdletBinding()]
param([int]$TimeoutSeconds = 90)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (Get-Process UnrealEditor,LiveCodingConsole -ErrorAction SilentlyContinue) { throw "Close editor first. Nothing stopped." }
if (!(Get-Process steam -ErrorAction SilentlyContinue)) { throw "Steam client is not running. Start it and log in." }
$Log = Join-Path $Root "Saved\Logs\MP\SteamHost.log"
New-Item -ItemType Directory -Force -Path (Split-Path $Log) | Out-Null
Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Args = @("`"$Root\BrGame.uproject`"", "-game", "-nullrhi", "-nosound", "-unattended", "-log",
    "-abslog=`"$Log`"", "-BrSessionSmokeHost", "-BrSessionCheckExit",
    "-LogCmds=`"LogOnline Verbose,LogOnlineSession Verbose,LogSockets Verbose`"")
$Process = Start-Process $Editor -ArgumentList $Args -WorkingDirectory $Root -PassThru
try {
    if (!$Process.WaitForExit($TimeoutSeconds * 1000)) { throw "Steam host check timed out; inspect $Log" }
    if ($Process.ExitCode -ne 0) { throw "Steam host check exited $($Process.ExitCode); inspect $Log" }
    foreach ($Pattern in @("Session created: Success=1 Steam=1", "Session updated: Success=1", "Session destroyed: Success=1",
        "Lobby Data \(BRGAME_s, BrGame\)", "Lobby Data \(BRLOCK_b, true\)")) {
        if (!(Select-String -Path $Log -Pattern $Pattern -Quiet)) { throw "Missing $Pattern in $Log" }
    }
    Write-Output "PASS: Steam initialized, BrGame lobby created/updated, password flag advertised, session destroyed. Log=$Log"
    Write-Output "NOT a two-account join/invite/relay transport test."
}
finally {
    if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue }
    $Process.WaitForExit()
}
