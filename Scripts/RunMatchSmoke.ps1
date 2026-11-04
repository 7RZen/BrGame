# Development-only network match regression: start, late join, real server shot,
# pickup application, results, travel back, and a clean second round.
[CmdletBinding()]
param(
    [int]$Port = 18177,
    [ValidateRange(60, 300)][int]$TimeoutSeconds = 180,
    [switch]$Grouped,
    [ValidatePattern('^[a-zA-Z0-9_-]*$')][string]$RunLabel = ""
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root "BrGame.uproject"
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Case = if ($Grouped) { "MatchGrouped" } else { "Match" }
$Case += $RunLabel
$Logs = Join-Path $Root "Saved\Logs\MP\$Case"
if (!(Test-Path -LiteralPath $Editor)) { throw "Unreal Editor not found: $Editor" }
if (Get-Process UnrealEditor, UnrealEditor-Cmd, LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "Editor/LiveCodingConsole running; close it first. No process was stopped."
}
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
# Each WaitLog checks all roles: stale failures from a previous run must not
# fail this run before the corresponding role has even been launched.
foreach ($Role in @("Host","Client1","Client2","Client3")) {
    Remove-Item -LiteralPath (Join-Path $Logs "$Role.log") -ErrorAction SilentlyContinue
}
$Processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

function Launch([string]$Role, [string]$URL, [string[]]$Extra = @()) {
    $Log = Join-Path $Logs "$Role.log"
    Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue
    $Args = @("`"$Project`"", $URL, "-game", "-nosteam", "-nullrhi", "-nosound",
        "-unattended", "-log", "-abslog=`"$Log`"", "-LogCmds=`"LogBr Verbose`"") + $Extra
    $Process = Start-Process -FilePath $Editor -ArgumentList $Args -WorkingDirectory $Root -PassThru
    $Processes.Add($Process)
    Write-Output "$Role PID=$($Process.Id) Log=$Log"
}

function WaitLog([string]$Role, [string]$Pattern, [int]$Minimum = 1) {
    $File = Join-Path $Logs "$Role.log"
    $End = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $End) {
        foreach ($Process in $Processes) {
            if ($Process.HasExited) { throw "Launched PID $($Process.Id) exited early: $($Process.ExitCode)" }
        }
        foreach ($ProbeLog in Get-ChildItem -LiteralPath $Logs -Filter *.log) {
            $Failure = Select-String -Path $ProbeLog.FullName -Pattern "E2E MATCH SMOKE FAIL|E2E MESH POSE FAIL|E2E AUDIO PROBE FAIL|E2E HIT PROBE FAIL|E2E SPRINT (CLIENT|SERVER) FAIL|E2E SLIDE .*FAIL" | Select-Object -Last 1
            if ($Failure) { throw "$($ProbeLog.Name) E2E assertion failed: $($Failure.Line)" }
        }
        if (Test-Path -LiteralPath $File) {
            $Matches = @(Select-String -Path $File -Pattern $Pattern)
            if ($Matches.Count -ge $Minimum) {
                Write-Output $Matches[-1].Line
                return
            }
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out waiting for $Role pattern '$Pattern' x$Minimum. Logs: $Logs"
}

try {
    $URL = "/Game/BrGame/Maps/L_Lobby?listen?Name=MatchHost"
    if ($Grouped) { $URL += "?Mode=Grouped?NumGroups=2" }
    Launch "Host" $URL @("-Port=$Port", "-BrFlowSmoke", "-BrMatchSmoke",
        "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:ReviveDuration=1.5",
        "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:RespawnHoldTime=0.1",
        "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:DownedDuration=4")
    WaitLog "Host" "Flow ready: L_Lobby Lobby=1"
    Launch "Client1" "127.0.0.1:$Port`?Name=MatchClient1" @("-BrMatchSmoke")
    WaitLog "Host" "Flow admitted: MatchClient1.*Lobby=1"
    Launch "Client2" "127.0.0.1:$Port`?Name=MatchClient2" @("-BrMatchSmoke")
    WaitLog "Host" "Flow admitted: MatchClient2.*Lobby=1"

    # Host + two clients start the match. The hook opens a bounded event-driven
    # admission window; the third remote client joins only after match travel.
    WaitLog "Host" "Flow ready: L_Town Lobby=0"
    WaitLog "Host" "E2E LATE JOIN WINDOW"
    Launch "Client3" "127.0.0.1:$Port`?Name=MatchLateJoin" @("-BrMatchSmoke")
    WaitLog "Host" "Flow admitted: MatchLateJoin.*Lobby=0"
    WaitLog "Client3" "Flow client state: L_Town Phase=2 Pawn=BrCharacter"
    WaitLog "Host" "E2E MATCH SMOKE PASS"
    WaitLog "Host" "E2E SPRINT SPEED PASS" 2
    WaitLog "Host" "E2E SPRINT FIRE PASS" 2
    WaitLog "Client3" "E2E SPRINT CLIENT PASS: Corrections=0" 2
    WaitLog "Host" "E2E SLIDE FIRE PASS" 2
    WaitLog "Client3" "E2E SLIDE CLIENT PASS: Corrections=0" 2
    WaitLog "Client3" "E2E SLIDE DISTANCE PASS" 2
    WaitLog "Client3" "E2E SLIDE COOLDOWN PASS" 2
    foreach ($Stage in 1..2) {
        WaitLog "Client3" "E2E HIT PROBE PASS Stage=$Stage Victim=0" 2
        $VictimMatches = @(Get-ChildItem -LiteralPath $Logs -Filter "Client*.log" |
            Select-String -Pattern "E2E HIT PROBE PASS Stage=$Stage Victim=1")
        if ($VictimMatches.Count -ne 2) { throw "Expected one victim hit assertion per round at stage $Stage; found $($VictimMatches.Count)." }
    }
    foreach ($Stage in 1..5) {
        WaitLog "Client3" "E2E AUDIO PROBE PASS Stage=$Stage " 2
    }
    WaitLog "Host" "E2E SECOND ROUND CLEAN"
    WaitLog "Host" "Flow ready: L_Town Lobby=0" 2
    WaitLog "Host" "Flow Results:"
    WaitLog "Host" "Flow ready: L_Lobby Lobby=1" 2
    # Both rounds must pass sustained checks on the listen host and another
    # client, not the downed pawn's owner. Samples cover >=0.5s after settling.
    foreach ($Role in @("Host", "Client3")) {
        foreach ($Pose in @("DOWNED", "REVIVED", "CROUCH", "PRONE")) {
            $Authority = if ($Role -eq "Host") { 1 } else { 0 }
            WaitLog $Role "E2E MESH POSE $Pose PASS:.*Authority=$Authority NonOwner=1" 2
        }
    }

    $HostLog = Join-Path $Logs "Host.log"
    foreach ($Pattern in @(
        "E2E SPAWNS PASS", "E2E LATE JOIN SPAWN PASS", "E2E PICKUPS PASS", "E2E SHOT PASS",
        "E2E REVIVE PASS", "E2E RESPAWN PASS", "E2E GRENADE PASS", "E2E TIMEOUT RESPAWN PASS",
        "E2E SECOND ROUND CLEAN", "E2E MATCH SMOKE PASS"
    )) {
        $Minimum = if ($Pattern -in @("E2E SPAWNS PASS", "E2E PICKUPS PASS", "E2E SHOT PASS",
            "E2E REVIVE PASS", "E2E RESPAWN PASS", "E2E GRENADE PASS", "E2E TIMEOUT RESPAWN PASS")) { 2 } else { 1 }
        $Count = @(Select-String -Path $HostLog -Pattern $Pattern).Count
        if ($Count -lt $Minimum) { throw "Expected $Minimum '$Pattern' assertions in host log, found $Count." }
    }
    foreach ($File in Get-ChildItem -LiteralPath $Logs -Filter *.log) {
        $Errors = @(Select-String -Path $File.FullName -Pattern "LogBr: Error|Fatal error|Assertion failed|Ensure condition failed|LogScript: Warning|E2E MATCH SMOKE FAIL")
        if ($Errors.Count) {
            $Errors | ForEach-Object { Write-Output $_.Line }
            throw "Smoke log errors in $($File.Name)"
        }
    }
    Write-Output "MATCH SMOKE PASS ($Case): late join, combat, revive/choice/timeout respawns, grenade, pickups, results and clean second round"
}
finally {
    foreach ($Process in $Processes) {
        if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue }
        if (!$Process.WaitForExit(10000)) { Write-Warning "Launched PID $($Process.Id) is still exiting." }
    }
}
