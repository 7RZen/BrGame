# Real Null LAN beacon discovery + session join + server-side password rejection.
[CmdletBinding()]
param([int]$Port = 17977, [int]$TimeoutSeconds = 90)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Logs = Join-Path $Root "Saved\Logs\MP\Session"
if (Get-Process UnrealEditor,LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "Close UnrealEditor/LiveCodingConsole before smoke. Nothing stopped."
}
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$Processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
function Launch($Role, $Extra) {
    $Log = Join-Path $Logs "$Role.log"
    Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
    $Args = @("`"$Root\BrGame.uproject`"", "-game", "-nosteam", "-nullrhi", "-nosound", "-unattended", "-log",
        "-abslog=`"$Log`"", "-Port=$Port") + $Extra
    $Process = Start-Process $Editor -ArgumentList $Args -WorkingDirectory $Root -PassThru
    $Processes.Add($Process)
    Write-Output "$Role PID=$($Process.Id)"
}
function WaitLog($Role, $Pattern, $AfterPattern = "") {
    $File = Join-Path $Logs "$Role.log"
    $End = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $End) {
        foreach ($Process in $Processes) {
            if ($Process.HasExited) { throw "Process $($Process.Id) exited unexpectedly: $($Process.ExitCode)" }
        }
        if (Test-Path $File) {
            $Found = @(Select-String -Path $File -Pattern $Pattern)
            if ($AfterPattern) {
                $Markers = @(Select-String -Path $File -Pattern $AfterPattern)
                if ($Markers.Count) { $Found = @($Found | Where-Object { $_.LineNumber -gt $Markers[-1].LineNumber }) }
                else { $Found = @() }
            }
            if ($Found.Count) { Write-Output $Found[-1].Line; return }
        }
        Start-Sleep -Seconds 1
    }
    throw "Timeout: $Role $Pattern"
}
try {
    Launch "Host" @("-BrSessionSmokeHost", "-BrSessionPrivacySmoke")
    WaitLog "Host" "Session created: Success=1 Steam=0"
    Launch "Wrong" @("-BrSessionSmokeClient", "-BrWrongPassword")
    WaitLog "Wrong" "Session search complete: Success=1 Compatible=1"
    WaitLog "Wrong" "Incorrect or missing server password"
    WaitLog "Wrong" "Session destroyed: Success=1"
    WaitLog "Wrong" "Flow client state: L_MainMenu Phase=0" "Incorrect or missing server password"
    Launch "Correct" @("-BrSessionSmokeClient")
    WaitLog "Correct" "Session search complete: Success=1 Compatible=1"
    # Player name is a saved local preference, not necessarily the factory default "Player".
    # The exact admission count below still requires host + the correct-password client only.
    WaitLog "Host" "PostLogin: Controller=BrPlayerController_1 Pawn=None"
    WaitLog "Correct" "Flow client state: L_Lobby Phase=1"
    WaitLog "Correct" "Session destroyed: Success=1"
    WaitLog "Correct" "Flow client state: L_MainMenu Phase=0" "Flow client state: L_Lobby Phase=1"
    WaitLog "Host" "Session LAN visibility published: Public=0"
    Launch "Browser" @("-BrSessionSmokeSearchOnly")
    WaitLog "Browser" "Session search complete: Success=1 Compatible=0"
    WaitLog "Host" "Session LAN visibility published: Public=1" "Session LAN visibility published: Public=0"
    WaitLog "Browser" "Session search complete: Success=1 Compatible=1" "Session search complete: Success=1 Compatible=0"
    $Admissions = @(Select-String -Path (Join-Path $Logs "Host.log") -Pattern "Flow admitted:")
    if ($Admissions.Count -ne 2) { throw "Expected only host and correct-password client admitted, got $($Admissions.Count)" }
    Write-Output "PASS: Null create/find/join, password rejection/acceptance/leave, Public -> private -> Public LAN discovery."
}
finally {
    foreach ($Process in $Processes) {
        if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue }
        $Process.WaitForExit()
    }
}
