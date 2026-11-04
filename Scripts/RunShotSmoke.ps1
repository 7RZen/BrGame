# Captures gameplay-camera screenshots on L_Town (dev-only -BrShotSmoke hook, windowed host).
# Output: Saved\Screenshots\Step10\<point>.png, named from the Shot smoke capture log order.
[CmdletBinding()]
param([int]$Port = 17981, [int]$TimeoutSeconds = 180,
    [switch]$Minimap,
    [switch]$Menu,
    [switch]$Aiming,
    [switch]$HitFeedback,
    [switch]$Posture,
    [ValidateRange(1200,4000)][float]$PostureOrthoWidth = 2500,
    [switch]$Maps,
    [switch]$MapAnnouncement,
    [ValidateRange(30,90)][float]$CameraPitchAngle = 65,
    [ValidatePattern('^[a-zA-Z0-9_-]*$')][string]$RunLabel = "")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Project = Join-Path $Root "BrGame.uproject"
$Case = "Shot$RunLabel"
$Logs = Join-Path $Root "Saved\Logs\MP\$Case"
$ShotDir = Join-Path $Root $(if ($Posture) { "Saved\Screenshots\Posture" } elseif ($Maps -or $MapAnnouncement) { "Saved\Screenshots\Maps" } elseif ($HitFeedback) { "Saved\Screenshots\HitFeedback" } elseif ($Aiming) { "Saved\Screenshots\Aiming" } elseif ($Menu) { "Saved\Screenshots\Menu" } elseif ($Minimap) { "Saved\Screenshots\Minimap" } else { "Saved\Screenshots\Step10" })
$Source = Join-Path $Root "Saved\Screenshots\WindowsEditor"
if (Get-Process UnrealEditor,UnrealEditor-Cmd,LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "Editor/LiveCodingConsole running; close it first. Nothing was stopped."
}
New-Item -ItemType Directory -Path $Logs, $ShotDir -Force | Out-Null
$Start = Get-Date
$Processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
function Launch($Role, $URL, $Extra = @(), $Windowed = $true) {
    $Log = Join-Path $Logs "$Role.log"
    Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
    $Args = @("`"$Project`"", $URL, "-game", "-nosteam", "-nosound", "-unattended", "-log", "-abslog=`"$Log`"",
        "-BrFlowSmoke", "-LogCmds=`"LogBr Verbose`"")
    if ($Windowed) { $Args += @("-windowed", "-ResX=1280", "-ResY=720") } else { $Args += "-nullrhi" }
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
            $Errors = @(Select-String -Path $File -Pattern "LogBr: Error|Fatal error|Assertion failed|Ensure condition failed")
            if ($Errors.Count) { $Errors | ForEach-Object { Write-Output $_.Line }; throw "Shot smoke error: $Role" }
            $Matches = @(Select-String -Path $File -Pattern $Pattern)
            if ($Matches.Count) { Write-Output $Matches[-1].Line; return }
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out waiting for $Role '$Pattern'"
}
try {
    if (([int]$Posture.IsPresent + [int]$MapAnnouncement.IsPresent + [int]$Maps.IsPresent + [int]$Menu.IsPresent + [int]$Minimap.IsPresent + [int]$Aiming.IsPresent + [int]$HitFeedback.IsPresent) -gt 1) { throw "Shot fixtures cannot be combined." }
    if ($MapAnnouncement) {
        Launch "Host" "/Game/BrGame/Maps/L_Town?Name=MapHost" @("-BrShotSmoke", "-BrMapAnnouncementShot")
    } elseif ($Maps) {
        Launch "Host" "/Game/BrGame/Maps/L_Lobby?listen?Name=MapHost" @("-BrMapsShot", "-Port=$Port")
    } elseif ($Menu) {
        Launch "Host" "/Game/BrGame/Maps/L_Town?Name=MenuHost" @("-BrShotSmoke", "-BrMenuShot")
    } elseif ($Minimap) {
        Launch "Host" "/Game/BrGame/Maps/L_Town?Name=MapHost?Mode=Grouped" @("-BrShotSmoke", "-BrMinimapShot")
    } else {
        $AimingArgs = $(if ($Posture) { @("-BrPostureShot", "-ExecCmds=`"r.MotionBlurQuality 0`"", "-BrPostureShotWidth=$PostureOrthoWidth") } elseif ($HitFeedback) { @("-BrHitShot", "-ExecCmds=`"r.MotionBlurQuality 0`"") } elseif ($Aiming) { @("-BrAimingShot") } else { @() })
        Launch "Host" "/Game/BrGame/Maps/L_Lobby?listen?Name=ShotHost" (@("-Port=$Port", "-BrShotSmoke",
            "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:CameraPitchAngle=$CameraPitchAngle") + $AimingArgs)
        WaitLog "Host" "Flow ready: L_Lobby Lobby=1"
        Launch "Client" "127.0.0.1:$Port`?Name=ShotClient" $(if ($Posture) { @("-BrPostureShot", "-ExecCmds=`"r.MotionBlurQuality 0`"", "-BrPostureShotWidth=$PostureOrthoWidth", "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:CameraPitchAngle=$CameraPitchAngle") } else { @() }) $Posture.IsPresent
        WaitLog "Host" "Flow admitted: ShotClient"
    }
    WaitLog "Host" "SHOT SMOKE PASS"
    # RequestScreenshot writes asynchronously after several frames; give the window time before stopping.
    Start-Sleep -Seconds 15
    if ($Posture) {
        foreach ($Role in @("Host", "Client")) {
            $Names = @(Select-String -Path (Join-Path $Logs "$Role.log") -Pattern "Posture shot capture: Name=(\S+)" |
                ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -Unique)
            if ($Names.Count -ne 5) { throw "Expected 5 $Role posture captures, got $($Names.Count)." }
            foreach ($Name in $Names) {
                $File = Join-Path $Source "Posture_$Name.png"
                if (!(Test-Path $File)) { throw "Missing $File" }
                Copy-Item $File (Join-Path $ShotDir "$Name$RunLabel.png") -Force
                Remove-Item $File
                Write-Output "SHOT $Name"
            }
        }
        Write-Output "SHOT SMOKE PASS: 10 posture screenshots saved to $ShotDir"
        return
    }
    $Names = @(Select-String -Path (Join-Path $Logs "Host.log") -Pattern "Shot smoke capture: Name=(\S+)" |
        ForEach-Object { $_.Matches[0].Groups[1].Value })
    $Found = 0
    foreach ($Name in $Names) {
        $Prefix = if ($Maps -or $MapAnnouncement) { "Maps" } elseif ($HitFeedback) { "HitFeedback" } elseif ($Menu) { "Menu" } elseif ($Minimap) { "Minimap" } else { "Step10" }
        $Source1 = Join-Path $Source "${Prefix}_$Name.png"
        if (!(Test-Path -LiteralPath $Source1)) { throw "Missing screenshot $Source1" }
        $Target = Join-Path $ShotDir "$Name$RunLabel.png"
        Copy-Item -LiteralPath $Source1 -Destination $Target -Force
        Remove-Item -LiteralPath $Source1 -Force
        $Found++
        Write-Output "SHOT $Name -> $Target"
    }
    Write-Output "SHOT SMOKE PASS ($Case): $Found gameplay-camera screenshots saved"
}
finally {
    foreach ($Process in $Processes) {
        if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue }
        if (!$Process.WaitForExit(10000)) { Write-Warning "Launched PID $($Process.Id) still exiting." }
    }
}
