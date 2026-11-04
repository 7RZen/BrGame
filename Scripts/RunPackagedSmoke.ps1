[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Packaged,
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1, 120)][int]$MenuTimeoutSeconds = 60,
    [int]$Port = 17877
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Exe = (Resolve-Path -LiteralPath $Packaged).Path
if ((Get-Item -LiteralPath $Exe).PSIsContainer) {
    $PackageRoot = $Exe
    $Exe = Join-Path $PackageRoot "BrGame\Binaries\Win64\BrGame.exe"
    if (!(Test-Path -LiteralPath $Exe)) { $Exe = Join-Path $PackageRoot "BrGame.exe" }
}
elseif ([IO.Path]::GetFileName($Exe) -ieq "BrGame.exe") {
    $ActualGame = Join-Path (Split-Path -Parent $Exe) "BrGame\Binaries\Win64\BrGame.exe"
    if (Test-Path -LiteralPath $ActualGame) { $Exe = $ActualGame }
}
if (!(Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "Packaged BrGame.exe not found: $Exe" }
if (Get-Process UnrealEditor, UnrealEditor-Cmd, LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "UnrealEditor/UnrealEditor-Cmd/LiveCodingConsole is running. Close it before smoke; no process was stopped."
}

$Logs = Join-Path $Root "Saved\Logs\MP\Packaged"
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$MenuLog = Join-Path $Logs "MainMenu.log"
Remove-Item -LiteralPath $MenuLog -Force -ErrorAction SilentlyContinue
$Game = $null
try {
    $MenuArgs = @("-nosteam", "-windowed", "-ResX=1280", "-ResY=720", "-nosound", "-log", "-abslog=`"$MenuLog`"")
    $Game = Start-Process -FilePath $Exe -ArgumentList $MenuArgs -WorkingDirectory (Split-Path -Parent $Exe) -PassThru
    $Deadline = (Get-Date).AddSeconds($MenuTimeoutSeconds)
    $LoadedMenu = $false
    while ((Get-Date) -lt $Deadline) {
        if ($Game.HasExited) { throw "Packaged main-menu process exited early: $($Game.ExitCode). Log: $MenuLog" }
        if (Test-Path -LiteralPath $MenuLog) {
            $LoadedMenu = [bool](Select-String -LiteralPath $MenuLog -Pattern "Load map complete /Game/BrGame/Maps/L_MainMenu" -Quiet)
            if ($LoadedMenu) { break }
        }
        Start-Sleep -Milliseconds 500
    }
    if (!$LoadedMenu) { throw "Timed out waiting for packaged L_MainMenu. Log: $MenuLog" }
    Write-Output "PACKAGED MENU PASS: L_MainMenu loaded. Log: $MenuLog"
}
finally {
    if ($Game -and !$Game.HasExited) {
        Stop-Process -Id $Game.Id -ErrorAction SilentlyContinue
        $Game.WaitForExit(15000) | Out-Null
    }
}

$FlowScript = Join-Path $PSScriptRoot "RunFlowSmoke.ps1"
$PowerShell = (Get-Process -Id $PID).Path
$FlowArgs = @(
    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$FlowScript`"",
    "-Packaged", "`"$Exe`"", "-Port", "$Port", "-TimeoutSeconds", "$TimeoutSeconds"
)
$FlowProcess = Start-Process -FilePath $PowerShell -ArgumentList $FlowArgs -WorkingDirectory $Root -Wait -PassThru
if ($FlowProcess.ExitCode -ne 0) { throw "Packaged flow smoke failed with exit code $($FlowProcess.ExitCode)." }
$HostLog = Join-Path $Root "Saved\Logs\MP\Flow\Host.log"
$Client1Log = Join-Path $Root "Saved\Logs\MP\Flow\Client1.log"
$Client2Log = Join-Path $Root "Saved\Logs\MP\Flow\Client2.log"
function Require-LogMatch([string]$Path, [string]$Pattern, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path) -or !(Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)) {
        throw "Packaged smoke log check failed: $Description ($Path)"
    }
}
Require-LogMatch $MenuLog "Load map complete /Game/BrGame/Maps/L_MainMenu" "main menu loaded"
Require-LogMatch $HostLog "Flow ready: L_Town Lobby=0" "host travelled to L_Town"
Require-LogMatch $HostLog "Flow Results:" "results phase"
Require-LogMatch $HostLog "Flow ready: L_Lobby Lobby=1" "host returned to lobby"
Require-LogMatch $Client1Log "Flow client state: L_Town Phase=2 Pawn=BrCharacter" "first client spawned in match"
Require-LogMatch $Client2Log "Flow client state: L_Town Phase=2 Pawn=BrCharacter" "second client spawned in match"
Require-LogMatch $Client1Log "Flow client state: L_Lobby Phase=1" "first client returned to lobby"
Require-LogMatch $Client2Log "Flow client state: L_Lobby Phase=1" "second client returned to lobby"
Require-LogMatch $HostLog "LoadCompleted: /Game/BrGame/Input/DA_BrInputConfig" "input configuration loaded"
Require-LogMatch $HostLog "LoadCompleted: /Game/BrGame/Fog/M_Fog" "fog material loaded"
Require-LogMatch $HostLog "Fog rendering ready:.*Texture=2048" "fog render targets initialized"
Require-LogMatch $HostLog "Crate initial wave:" "crate loot definitions loaded and initial wave scheduled"
Require-LogMatch $HostLog "LoadCompleted: /Game/BrGame/Zone/M_ZoneRing" "zone ring material loaded"
$RuntimeErrors = 'LogBr: Error|LogStreaming: Error|LogUObjectGlobals: Error|LogScript: Warning|LogLinker: Warning.*(Failed|missing|not found)|LogStreaming: Warning.*(Failed|missing|not found)|Failed to load.*(/Game/|/Engine/)|Failed to find object|Could not find object|Fatal error|Assertion failed|Ensure condition failed'
foreach ($Log in @($MenuLog, $HostLog, $Client1Log, $Client2Log)) {
    if (Select-String -LiteralPath $Log -Pattern $RuntimeErrors -Quiet) {
        throw "Packaged smoke found an asset/runtime error in $Log"
    }
}
Write-Output "PACKAGED FLOW PASS: host, two IP clients, match travel, player spawns, results and return to lobby."
