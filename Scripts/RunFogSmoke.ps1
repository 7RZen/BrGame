# Bounded, real client actor-presence checks (not just server relevancy decisions).
[CmdletBinding()]
param([int]$Port = 17977, [int]$TimeoutSeconds = 120, [switch]$Grouped, [switch]$Windowed,
    [switch]$Spectator, [ValidateRange(0,240)][int]$FrameRate = 0,
    # Legacy level-name interface, translated to the lobby map selection.
    [ValidatePattern('^[A-Za-z0-9_]+$')][string]$MatchLevel = "L_Town",
    [ValidatePattern('^[a-zA-Z0-9_-]*$')][string]$RunLabel = "")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Project = Join-Path $Root "BrGame.uproject"
$Case = if ($Grouped) { "FogGrouped" } else { "Fog" }
if ($Windowed) { $Case += "Windowed" }
if ($Spectator) {
    if ($Grouped) { throw "Spectator smoke requires all-vs-all." }
    $Case = "FogSpectator"
}
$Case += $RunLabel
$Logs = Join-Path $Root "Saved\Logs\MP\$Case"
if (Get-Process UnrealEditor,LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "Editor/LiveCodingConsole running; close it first. Nothing was stopped."
}
New-Item -ItemType Directory -Path $Logs -Force | Out-Null
$Processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
function Launch($Role, $URL, $Extra = @()) {
    $Log = Join-Path $Logs "$Role.log"
    Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
    $Args = @("`"$Project`"", $URL, "-game", "-nosteam", "-nosound", "-unattended", "-log", "-abslog=`"$Log`"",
        "-BrFogSmoke", "-LogCmds=`"LogBr Verbose`"")
    if ($Spectator) {
        $Args = $Args | Where-Object { $_ -notlike "-LogCmds=*" }
        $Args += @("-BrFogSpectatorSmoke", "-ExecCmds=`"t.MaxFPS $FrameRate`"", '-LogCmds="LogBr Verbose,LogNetTraffic Warning,LogNetPackageMap Log"')
    }
    if (!$Windowed) { $Args += "-nullrhi" }
    else { $Args += @("-windowed", "-ResX=800", "-ResY=600") }
    $Process = Start-Process -FilePath $Editor -ArgumentList ($Args + $Extra) -WorkingDirectory $Root -PassThru
    $Processes.Add($Process)
    Write-Output "$Role PID=$($Process.Id) Log=$Log"
}
function WaitLog($Role, $Pattern) {
    $File = Join-Path $Logs "$Role.log"
    $End = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $End) {
        foreach ($Process in $Processes) {
            if ($Process.HasExited) { throw "Launched PID $($Process.Id) exited: $($Process.ExitCode)" }
        }
        if (Test-Path $File) {
            $Errors = @(Select-String -Path $File -Pattern "LogBr: Error|Fatal error|Assertion failed|Ensure condition failed|LogScript: Warning|Failed to compile Material|Error compiling")
            if ($Errors.Count) { $Errors | ForEach-Object { Write-Output $_.Line }; throw "Fog smoke error: $Role" }
            $Matches = @(Select-String -Path $File -Pattern $Pattern)
            if ($Matches.Count) { Write-Output $Matches[-1].Line; return }
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out waiting for $Role '$Pattern'"
}
try {
    $URL = "/Game/BrGame/Maps/L_Lobby?listen?Name=FogHost"
    if ($Grouped) { $URL += "?Mode=Grouped?NumGroups=2" }
    # Do not let a machine-local saved host map choice invalidate the smoke's
    # expected level (or turn a Town render check into a Battlefield check).
    $MatchExtra = @("-BrSmokeMapId=Town")
    if ($MatchLevel -ne "L_Town") {
        # Override the match map for this run only; the default is explicitly Town.
        if ($MatchLevel -ne "Lvl_TopDown") { throw "Unknown allowlisted MatchLevel: $MatchLevel" }
        $MatchExtra = @("-BrSmokeMapId=TestArena")
    }
    Launch "Host" $URL (@("-Port=$Port", "-BrFlowSmoke") + $MatchExtra)
    WaitLog "Host" "Flow ready: L_Lobby Lobby=1"
    if ($Grouped -or $Spectator) {
        Launch "Enemy" "127.0.0.1:$Port`?Name=FogEnemy"
        WaitLog "Host" "Flow admitted: FogEnemy Team=.* Lobby=1"
    }
    $ClientName = if ($Spectator) { "FogSpectator" } else { "FogClient" }
    Launch "Client" "127.0.0.1:$Port`?Name=$ClientName"
    WaitLog "Client" "Flow client state: $MatchLevel Phase=2 Pawn=BrCharacter"
    if ($Spectator) {
        WaitLog "Client" "FOG SPECTATOR PASS Stage=0 .* Withheld=0 Results=0"
        foreach ($Stage in 3..8) {
            WaitLog "Client" "FOG SPECTATOR PASS Stage=$Stage .* Withheld=0 Results=0"
        }
        Write-Output "FOG SPECTATOR SMOKE PASS ($Case): active fog; eliminated client receives pawn; five target switches; non-target withheld"
        return
    }
    $Far = if ($Grouped) { 1 } else { 0 }
    WaitLog "Client" "FOG PROBE PASS Stage=1 HostCharacters=$Far ExpectedPresent=$Far Authority=0"
    WaitLog "Client" "FOG PROBE PASS Stage=3 HostCharacters=1 ExpectedPresent=1 Authority=0"
    WaitLog "Client" "FOG PROBE PASS Stage=5 HostCharacters=$Far ExpectedPresent=$Far Authority=0"
    if ($Windowed) {
        WaitLog "Host" "Fog rendering ready:"
        WaitLog "Client" "Fog rendering ready:"
        WaitLog "Client" "Fog GPU probe PASS: Stage=5"
        WaitLog "Client" "Fog GPU unexplored probe PASS: Stamp=0 Coverage=0"
    }
    Write-Output "FOG SMOKE PASS ($Case): far -> clear-LOS near -> far; client actor presence verified"
}
finally {
    foreach ($Process in $Processes) {
        if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue }
        if (!$Process.WaitForExit(10000)) { Write-Warning "Launched PID $($Process.Id) still exiting." }
    }
}
