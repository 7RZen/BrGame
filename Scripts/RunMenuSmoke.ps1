[CmdletBinding()]
param([int]$Port = 18281, [int]$TimeoutSeconds = 120)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Editor = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Logs = Join-Path $Root "Saved\Logs\MP\Menu"
if (Get-Process UnrealEditor,UnrealEditor-Cmd,LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "Editor/LiveCoding running; close it first. Nothing was stopped."
}
New-Item -ItemType Directory -Path $Logs -Force | Out-Null
foreach ($Role in @("Host","Client1","Client2")) {
    Remove-Item -LiteralPath (Join-Path $Logs "$Role.log") -ErrorAction SilentlyContinue
}
$Processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
function Launch($Role, $URL, $Extra = @()) {
    $Log = Join-Path $Logs "$Role.log"
    Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
    $Args = @("`"$Root\BrGame.uproject`"", $URL, "-game", "-nosteam", "-nullrhi", "-nosound",
        "-unattended", "-log", "-abslog=`"$Log`"", "-BrMenuSmoke", "-BrMenuSmokePort=$Port",
        "-LogCmds=`"LogBr Verbose`"") + $Extra
    $Process = Start-Process -FilePath $Editor -ArgumentList $Args -WorkingDirectory $Root -PassThru
    $Processes.Add($Process)
    Write-Output "$Role PID=$($Process.Id)"
}
function WaitLog($Role, $Pattern) {
    $End = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $End) {
        foreach ($Process in $Processes) {
            if ($Process.HasExited) { throw "Launched PID $($Process.Id) exited early." }
        }
        foreach ($File in Get-ChildItem $Logs -Filter *.log) {
            $Bad = Select-String -Path $File.FullName -Pattern "LogBr: Error|Fatal error|Assertion failed|Ensure condition failed|LogScript: Warning"
            $HostFile = Join-Path $Logs "Host.log"
            $HostLeft = (Test-Path $HostFile) -and (Select-String -Path $HostFile -Pattern "MENU HOST LEAVE via overlay")
            if ($HostLeft) { $Bad = @($Bad | Where-Object { $_.Line -notmatch "LogBr: Error: Flow network failure:" }) }
            if ($Bad) { throw "$($File.Name): $($Bad[-1].Line)" }
        }
        $File = Join-Path $Logs "$Role.log"
        if ((Test-Path $File) -and (Select-String -Path $File -Pattern $Pattern)) {
            Write-Output "$Role PASS: $Pattern"; return
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out $Role '$Pattern'"
}
try {
    Launch Host "/Game/BrGame/Maps/L_Lobby?listen?Name=MenuHost" @("-BrFlowSmoke", "-Port=$Port",
        "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:ResultsDuration=60")
    WaitLog Host "Flow ready: L_Lobby Lobby=1"
    Launch Client1 "127.0.0.1:$Port`?Name=MenuClient1"
    WaitLog Host "Flow admitted: MenuClient1"
    Launch Client2 "127.0.0.1:$Port`?Name=MenuClient2"
    foreach ($Pattern in @("MENU UNSTUCK SUCCESS PASS", "MENU UNSTUCK DAMAGE CANCEL PASS",
        "MENU FORFEIT PASS", "MENU REJOIN FORFEITED PASS", "MENU HOST LEAVE via overlay")) {
        WaitLog Host $Pattern
    }
    WaitLog Host "Flow client state: L_MainMenu Phase=0"
    WaitLog Client1 "Flow network failure:"
    WaitLog Client2 "Flow network failure:"
    WaitLog Client1 "Flow client state: L_MainMenu Phase=0"
    WaitLog Client2 "Flow client state: L_MainMenu Phase=0"
    Write-Output "MENU SMOKE PASS: unstuck, damage cancel, immediate forfeit/corpse/winner, rejoin and host leave"
}
finally {
    foreach ($Process in $Processes) {
        if (!$Process.HasExited) { Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue }
        $Process.WaitForExit(10000) | Out-Null
    }
}
