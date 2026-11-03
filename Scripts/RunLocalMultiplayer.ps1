# Launch a local listen server and N clients.
# Example: .\Scripts\RunLocalMultiplayer.ps1 -Clients 2
# Headless smoke run: .\Scripts\RunLocalMultiplayer.ps1 -Clients 1 -Headless
# Menu flow (every window opens the start page; host/join manually): .\Scripts\RunLocalMultiplayer.ps1 -Clients 1 -Menu
[CmdletBinding()]
param(
    [ValidateRange(1, 32)]
    [int]$Clients = 1,
    [string]$Map = "/Game/BrGame/Maps/L_Town",
    [ValidateRange(1, 65535)]
    [int]$Port = 7777,
    [switch]$Headless,
    [switch]$Menu,
    [ValidateRange(320, 7680)]
    [int]$ResX = 1280,
    [ValidateRange(240, 4320)]
    [int]$ResY = 720,
    [int]$WinX = 40,
    [int]$WinY = 40,
    [ValidateRange(0, 60)]
    [int]$WindowOffset = 40,
    [ValidateRange(0, 120)]
    [int]$ClientStartupDelaySeconds = 10
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $ProjectRoot "BrGame.uproject"
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$LogDirectory = Join-Path $ProjectRoot "Saved\Logs\MP"

if (!(Test-Path -LiteralPath $Project)) { throw "Project not found: $Project" }
if (!(Test-Path -LiteralPath $Editor)) { throw "Unreal Editor not found: $Editor" }
New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null

$CommonArgs = @("-game", "-nosteam", "-log")
if ($Headless) {
    $CommonArgs += @("-nullrhi", "-unattended", "-nosound")
}
else {
    $CommonArgs += @("-windowed", "-ResX=$ResX", "-ResY=$ResY")
}

$Instances = @()
if ($Menu) {
    # Opens the default map (start page) in Clients + 1 windows.
    for ($Index = 0; $Index -le $Clients; $Index++) {
        $Role = if ($Index -eq 0) { "Player1" } else { "Player$($Index + 1)" }
        $InstanceLog = Join-Path $LogDirectory "$Role.log"
        Remove-Item -LiteralPath $InstanceLog -Force -ErrorAction SilentlyContinue
        $InstanceArgs = @($Project) + $CommonArgs
        $InstanceArgs += @("-WinX=$($WinX + $WindowOffset * $Index)", "-WinY=$($WinY + $WindowOffset * $Index)", "-abslog=$InstanceLog")
        $Process = Start-Process -FilePath $Editor -ArgumentList $InstanceArgs -WorkingDirectory $ProjectRoot -PassThru
        $Instances += [pscustomobject]@{ Role = $Role; PID = $Process.Id; Log = $InstanceLog }
    }
    $Instances
    return
}
$ServerLog = Join-Path $LogDirectory "ListenServer.log"
Remove-Item -LiteralPath $ServerLog -Force -ErrorAction SilentlyContinue
$ServerArgs = @($Project, "$Map`?listen?game=/Script/BrGame.BrGameMode") + $CommonArgs
$ServerArgs += @("-Port=$Port", "-WinX=$WinX", "-WinY=$WinY", "-abslog=$ServerLog")
$Server = Start-Process -FilePath $Editor -ArgumentList $ServerArgs -WorkingDirectory $ProjectRoot -PassThru
$Instances += [pscustomobject]@{ Role = "ListenServer"; PID = $Server.Id; Log = $ServerLog }

if ($Clients -gt 0 -and $ClientStartupDelaySeconds -gt 0) {
    Start-Sleep -Seconds $ClientStartupDelaySeconds
}

for ($Index = 1; $Index -le $Clients; $Index++) {
    $Role = "Client$Index"
    $ClientLog = Join-Path $LogDirectory "$Role.log"
    Remove-Item -LiteralPath $ClientLog -Force -ErrorAction SilentlyContinue
    $ClientArgs = @($Project, "127.0.0.1:$Port") + $CommonArgs
    $ClientArgs += @("-WinX=$($WinX + $WindowOffset * $Index)", "-WinY=$($WinY + $WindowOffset * $Index)", "-abslog=$ClientLog")
    $Client = Start-Process -FilePath $Editor -ArgumentList $ClientArgs -WorkingDirectory $ProjectRoot -PassThru
    $Instances += [pscustomobject]@{ Role = $Role; PID = $Client.Id; Log = $ClientLog }
}

$Instances
