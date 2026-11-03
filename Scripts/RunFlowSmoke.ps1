# Bounded flow smoke: lobby, seamless match, late join, eliminations, results, lobby.
[CmdletBinding()]
param([int]$Port = 17877, [int]$TimeoutSeconds = 180, [switch]$Grouped, [switch]$DisconnectGrace, [switch]$Zone,
    [string]$Packaged,
    # Short name of the match level the lobby travels to (BrDeveloperSettings.MatchMap). Use Lvl_TopDown for the template map.
    [ValidatePattern('^[A-Za-z0-9_]+$')][string]$MatchLevel = "L_Town")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root "BrGame.uproject"
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Runner = $Editor
if ($Packaged) {
    $Runner = (Resolve-Path -LiteralPath $Packaged).Path
    if ((Get-Item -LiteralPath $Runner).PSIsContainer) {
        $PackageRoot = $Runner
        $Runner = Join-Path $PackageRoot "BrGame\Binaries\Win64\BrGame.exe"
        if (!(Test-Path -LiteralPath $Runner)) { $Runner = Join-Path $PackageRoot "BrGame.exe" }
    }
    elseif ([IO.Path]::GetFileName($Runner) -ieq "BrGame.exe") {
        $ActualGame = Join-Path (Split-Path -Parent $Runner) "BrGame\Binaries\Win64\BrGame.exe"
        if (Test-Path -LiteralPath $ActualGame) { $Runner = $ActualGame }
    }
    if (!(Test-Path -LiteralPath $Runner -PathType Leaf)) { throw "Packaged BrGame.exe not found: $Runner" }
}
$Logs = Join-Path $Root "Saved\Logs\MP\Flow"
if ($Grouped) { $Logs = Join-Path $Root "Saved\Logs\MP\FlowGrouped" }
if ($Zone) {
    if ($DisconnectGrace) { throw "-Zone and -DisconnectGrace are separate smoke cases." }
    $Logs = Join-Path $Root "Saved\Logs\MP\Zone"
}
if ($DisconnectGrace) {
    $Case = if ($Grouped) { "FlowDisconnectGrouped" } else { "FlowDisconnect" }
    $Logs = Join-Path $Root "Saved\Logs\MP\$Case"
}
if (Get-Process UnrealEditor, UnrealEditor-Cmd, LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "UnrealEditor/UnrealEditor-Cmd/LiveCodingConsole running; close it before smoke. Nothing was stopped."
}
New-Item -ItemType Directory -Path $Logs -Force | Out-Null
$Processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
function Launch($Role, $URL, $Extra = @()) {
    $Log = Join-Path $Logs "$Role.log"
    Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
    if ($Packaged) {
        $Args = @($URL, "-nosteam", "-windowed", "-ResX=1280", "-ResY=720", "-nosound", "-unattended", "-log", "-abslog=`"$Log`"", "-LogCmds=`"LogBr Verbose,LogStreaming Verbose`"") + $Extra
    }
    else {
        $Args = @("`"$Project`"", $URL, "-game", "-nosteam", "-nullrhi", "-nosound", "-unattended", "-log", "-abslog=`"$Log`"", "-LogCmds=`"LogBr Verbose`"") + $Extra
    }
    $Process = Start-Process -FilePath $Runner -ArgumentList $Args -WorkingDirectory $Root -PassThru
    $Processes.Add($Process)
    Write-Output "$Role PID=$($Process.Id) Log=$Log"
}
function WaitLog($Role, $Pattern, $Minimum = 1, $AfterPattern = "") {
    $File = Join-Path $Logs "$Role.log"
    $End = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $End) {
        foreach ($Process in $Processes) {
            if ($Process.HasExited) { throw "Launched process $($Process.Id) exited early: $($Process.ExitCode)" }
        }
        if (Test-Path $File) {
            $Matches = @(Select-String -Path $File -Pattern $Pattern)
            if ($AfterPattern) {
                $Markers = @(Select-String -Path $File -Pattern $AfterPattern)
                if ($Markers.Count) {
                    $LastMarker = $Markers[-1].LineNumber
                    $Matches = @($Matches | Where-Object { $_.LineNumber -gt $LastMarker })
                }
                else { $Matches = @() }
            }
            if ($Matches.Count -ge $Minimum) {
                Write-Output $Matches[-1].Line
                return
            }
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out waiting for $Role pattern '$Pattern' x$Minimum"
}
try {
    $URL = "/Game/BrGame/Maps/L_Lobby?listen?Name=SmokeHost"
    if ($Grouped) { $URL += "?Mode=Grouped?NumGroups=2" }
    $HostExtra = @("-Port=$Port", "-BrFlowSmoke")
    if ($MatchLevel -ne "L_Town") {
        # Override the match map for this run only (e.g. Lvl_TopDown). Default run uses DefaultGame.ini (L_Town).
        $MatchPath = if ($MatchLevel -eq "Lvl_TopDown") { "/Game/TopDown/Lvl_TopDown.Lvl_TopDown" } else { "/Game/BrGame/Maps/$MatchLevel.$MatchLevel" }
        $HostExtra += "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:MatchMap=$MatchPath"
    }
    if ($Zone) {
        $HostExtra += @("-BrZoneSmoke", "-LogCmds=`"LogBr Verbose`"",
            "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:ZoneTimeScale=20",
            "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:DownedDuration=2")
    }
    if ($DisconnectGrace) {
        $HostExtra += @("-BrFlowDisconnectSmoke",
            "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:ReconnectGracePeriod=4",
            "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:DownedDuration=2")
    }
    Launch "Host" $URL $HostExtra
    WaitLog "Host" "Flow ready: L_Lobby Lobby=1"
    $ClientExtra = @()
    if ($DisconnectGrace) { $ClientExtra += "-BrFlowDisconnectSmoke" }
    Launch "Client1" "127.0.0.1:$Port`?Name=SmokeClient1" $ClientExtra
    WaitLog "Host" "Flow admitted: SmokeClient1.*Lobby=1"
    WaitLog "Host" "Flow ready: $MatchLevel Lobby=0"
    WaitLog "Client1" "Flow client state: $MatchLevel Phase=2 Pawn=BrCharacter"
    if ($Zone) {
        # A late join must receive the current snapshot, not start its own timeline.
        Launch "Client2" "127.0.0.1:$Port`?Name=SmokeLateJoin"
        WaitLog "Client2" "Zone replicated: Stage="
        WaitLog "Client2" "Flow client state: $MatchLevel Phase=2 Pawn=BrCharacter"
        WaitLog "Host" "Zone stage: 5/5"
        WaitLog "Host" "Zone sudden death: respawns disabled"
        WaitLog "Host" "Zone damage: .* Applied="
        WaitLog "Host" "Life Downed:"
        WaitLog "Host" "Life Eliminated:"
        WaitLog "Host" "Flow Results:"
        WaitLog "Host" "Flow ready: L_Lobby Lobby=1" 2
        WaitLog "Client1" "Flow client state: L_Lobby Phase=1" 1 "Flow client state: $MatchLevel Phase=2"
        WaitLog "Client2" "Flow client state: L_Lobby Phase=1" 1 "Flow client state: $MatchLevel Phase=2"
        $StageLines = @(Select-String -Path (Join-Path $Logs "Host.log") -Pattern "Zone stage: (\d+)/(\d+) Radius=([\d.]+) Target=([\d.]+)")
        if ($StageLines.Count -ne 5) { throw "Expected five zone stages, got $($StageLines.Count)" }
        # A target may be narrowed after navmesh validation. The unverified target
        # in the initial stage log is provisional; continuity must use the final
        # verified radius when one was logged.
        $VerifiedTargets = @{}
        foreach ($Verified in Select-String -Path (Join-Path $Logs "Host.log") -Pattern "Zone target nav retry verified: Stage=(\d+) Radius=([\d.]+)") {
            $VerifiedMatch = $Verified.Matches[0]
            $VerifiedTargets[[int]$VerifiedMatch.Groups[1].Value] =
                [double]::Parse($VerifiedMatch.Groups[2].Value, [cultureinfo]::InvariantCulture)
        }
        $PreviousTarget = $null
        foreach ($Line in $StageLines) {
            $Match = $Line.Matches[0]
            $Stage = [int]$Match.Groups[1].Value
            $Radius = [double]::Parse($Match.Groups[3].Value, [cultureinfo]::InvariantCulture)
            $Target = [double]::Parse($Match.Groups[4].Value, [cultureinfo]::InvariantCulture)
            if ($null -ne $PreviousTarget -and [math]::Abs($Radius - $PreviousTarget) -gt 0.2) {
                throw "Stage discontinuity: previous target $PreviousTarget, current $Radius"
            }
            if ($Radius -le 0 -or $Target -ge $Radius) { throw "Invalid shrinking stage: $($Line.Line)" }
            $PreviousTarget = if ($VerifiedTargets.ContainsKey($Stage)) { $VerifiedTargets[$Stage] } else { $Target }
        }
        if ($PreviousTarget -ne 0) { throw "Final zone did not collapse" }
        $Sudden = @(Select-String -Path (Join-Path $Logs "Host.log") -Pattern "Zone sudden death:")[-1].LineNumber
        $IllegalRespawns = @(Select-String -Path (Join-Path $Logs "Host.log") -Pattern "Life Respawn:" | Where-Object { $_.LineNumber -gt $Sudden })
        if ($IllegalRespawns.Count) { throw "A player respawned during sudden death" }
    }
    elseif ($DisconnectGrace) {
        WaitLog "Client1" "Flow smoke: client leaving while downed"
        WaitLog "Client1" "Flow client state: L_MainMenu Phase=0"
        WaitLog "Host" "Round disconnect:"
        WaitLog "Host" "Offline timeout:"
        WaitLog "Host" "Flow forfeit: SmokeClient1"
        WaitLog "Host" "Flow Results:"
        WaitLog "Host" "Flow ready: L_Lobby Lobby=1" 2
    }
    else {
    Launch "Client2" "127.0.0.1:$Port`?Name=SmokeLateJoin"
    WaitLog "Host" "Flow admitted: SmokeLateJoin.*Lobby=0 Results=0"
    WaitLog "Client2" "Flow client state: $MatchLevel Phase=2 Pawn=BrCharacter"
    WaitLog "Host" "Flow Results:"
    if ($Grouped) {
        WaitLog "Host" "Flow Results: Group 1 wins"
        WaitLog "Host" "Flow admitted: SmokeLateJoin Team=0 Lobby=0 Results=0"
    }
    WaitLog "Client1" "Flow client state: $MatchLevel Phase=3"
    WaitLog "Host" "Flow ready: L_Lobby Lobby=1" 2
    # Initial lobby replication may be skipped by the fast headless start; assert ordering, not count.
    WaitLog "Client1" "Flow client state: L_Lobby Phase=1" 1 "Flow client state: $MatchLevel Phase=3"
    WaitLog "Client2" "Flow client state: L_Lobby Phase=1" 1 "Flow client state: $MatchLevel Phase=2"
    if ($Grouped) {
        WaitLog "Host" "Flow admitted: SmokeClient1 Team=1 Lobby=1" 2
        WaitLog "Host" "Flow admitted: SmokeLateJoin Team=0 Lobby=1"
    }
    WaitLog "Host" "Flow spectating \(server\):"
    WaitLog "Client1" "Flow spectating:"
    }
    foreach ($File in Get-ChildItem $Logs -Filter *.log) {
        $Errors = @(Select-String -Path $File.FullName -Pattern "LogBr: Error|Fatal error|Assertion failed|Ensure condition failed|LogScript: Warning")
        if ($Errors.Count) { $Errors | ForEach-Object { Write-Output $_.Line }; throw "Smoke log errors in $($File.Name)" }
    }
    if ($Zone) {
        Write-Output "ZONE SMOKE PASS: stages -> outside damage -> downed -> sudden death -> elimination -> results -> lobby; late-join snapshot"
    }
    elseif ($DisconnectGrace) {
        Write-Output "FLOW DISCONNECT PASS: downed -> disconnect -> offline timeout -> grace forfeit -> results -> lobby"
    }
    else {
        Write-Output "FLOW SMOKE PASS: lobby -> match (2 spawns) -> late join spawn -> results -> lobby (all clients)"
    }
}
finally {
    foreach ($Process in $Processes) {
        if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue }
        if (!$Process.WaitForExit(10000)) { Write-Warning "Launched PID $($Process.Id) is still exiting." }
    }
}
