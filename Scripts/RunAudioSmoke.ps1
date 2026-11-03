# Single owned windowed game, real audio device; no -nosound or -nullrhi.
[CmdletBinding()]
param([ValidateRange(45,90)][float]$CameraPitchAngle = 65, [int]$TimeoutSeconds = 90)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
if (Get-Process UnrealEditor,UnrealEditor-Cmd,LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "Editor/LiveCodingConsole running; close it first. No process was stopped."
}
$Logs = Join-Path $Root "Saved\Logs\Audio"
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$Log = Join-Path $Logs "WindowPitch$CameraPitchAngle.log"
Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue
$Args = @("`"$Root\BrGame.uproject`"", "/Game/BrGame/Maps/L_Town?Name=AudioSmoke",
    "-game", "-nosteam", "-windowed", "-ResX=960", "-ResY=540", "-unattended",
    "-BrAudioWindowSmoke", "-log", "-abslog=`"$Log`"", "-LogCmds=`"LogBr Verbose`"",
    "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:CameraPitchAngle=$CameraPitchAngle")
$Process = Start-Process $Editor -ArgumentList $Args -WorkingDirectory $Root -PassThru
try {
    if (!$Process.WaitForExit($TimeoutSeconds * 1000)) { throw "Audio window timed out: $Log" }
    if ($Process.ExitCode -ne 0) { throw "Audio window exit=$($Process.ExitCode): $Log" }
    $Text = Get-Content -LiteralPath $Log -Raw
    if ($Text -match "LogBr: Error|Fatal error|Assertion failed|Ensure condition failed|LogAudio[^:]*: Error") {
        throw "Audio window logged errors: $Log"
    }
    foreach ($Pattern in @("AUDIO LISTENER PASS", "AUDIO WINDOW SMOKE PASS", "Audio volumes applied:", "Audio playback:.*Device=1", "LogAudio: Display: Audio Device .* registered")) {
        if ($Text -notmatch $Pattern) { throw "Missing '$Pattern': $Log" }
    }
    if ([regex]::Matches($Text, "Audio playback:.*Device=1").Count -lt 12) { throw "Missing playback submissions: $Log" }
    Select-String -Path $Log -Pattern "AUDIO .*PASS|Audio volumes applied:|Audio playback:|Audio Device .*registered" | ForEach-Object { Write-Output $_.Line }
    Write-Output "AUDIO WINDOW PASS Pitch=$CameraPitchAngle Log=$Log (log validation, not human listening)"
}
finally {
    if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue; $Process.WaitForExit(10000) | Out-Null }
}
