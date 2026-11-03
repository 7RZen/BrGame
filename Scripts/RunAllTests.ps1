[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$Windowed,
    [ValidateRange(60, 1800)][int]$BuildTimeoutSeconds = 600,
    [ValidateRange(60, 1800)][int]$TestTimeoutSeconds = 600,
    [ValidateRange(30, 900)][int]$SmokeTimeoutSeconds = 360
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root "BrGame.uproject"
$UE = Join-Path ${env:ProgramFiles} "Epic Games\UE_5.8"
$Build = Join-Path $UE "Engine\Build\BatchFiles\Build.bat"
$EditorCmd = Join-Path $UE "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$RunRoot = Join-Path $Root "Saved\Logs\RunAll\$Stamp"
$OutputRoot = Join-Path $RunRoot "steps"
$ArtifactRoot = Join-Path $RunRoot "smoke-logs"
New-Item -ItemType Directory -Force -Path $OutputRoot, $ArtifactRoot | Out-Null
$Results = [System.Collections.Generic.List[object]]::new()

if (Get-Process UnrealEditor, UnrealEditor-Cmd, LiveCodingConsole -ErrorAction SilentlyContinue) {
    throw "UnrealEditor/UnrealEditor-Cmd/LiveCodingConsole is running. Close it before regression; no process was stopped."
}
foreach ($Required in @($Project, $EditorCmd) + $(if (!$SkipBuild) { @($Build) } else { @() })) {
    if (!(Test-Path -LiteralPath $Required)) { throw "Required project/UE file missing: $Required" }
}

function Invoke-ProcessStep {
    param(
        [string]$Name, [string]$File, [string[]]$Arguments, [int]$Timeout,
        [string]$PassPattern, [switch]$Automation
    )
    $OutFile = Join-Path $OutputRoot "$Name.stdout.log"
    $ErrFile = Join-Path $OutputRoot "$Name.stderr.log"
    Remove-Item $OutFile, $ErrFile -Force -ErrorAction SilentlyContinue
    $Started = Get-Date
    $Process = Start-Process -FilePath $File -ArgumentList $Arguments -WorkingDirectory $Root `
        -RedirectStandardOutput $OutFile -RedirectStandardError $ErrFile -PassThru
    $Finished = $Process.WaitForExit($Timeout * 1000)
    if (!$Finished) {
        try { $Process.Kill($true) } catch { Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue }
        $Process.WaitForExit()
        $Message = "Timed out after ${Timeout}s (PID $($Process.Id)); launched process tree stopped."
        $Results.Add([pscustomobject]@{ Step=$Name; Result="FAIL"; Detail=$Message; Seconds=[int]((Get-Date)-$Started).TotalSeconds })
        return $false
    }
    $ExitCode = $Process.ExitCode
    $Text = (Get-Content -LiteralPath $OutFile -Raw -ErrorAction SilentlyContinue) + "`n" +
        (Get-Content -LiteralPath $ErrFile -Raw -ErrorAction SilentlyContinue)
    if ($Automation) {
        $AutomationLog = Join-Path $OutputRoot "Automation.log"
        if (Test-Path -LiteralPath $AutomationLog) {
            $Text += "`n" + (Get-Content -LiteralPath $AutomationLog -Raw -ErrorAction SilentlyContinue)
        }
    }
    $Good = $ExitCode -eq 0
    $Detail = "exit=$ExitCode"
    if ($Automation) {
        $SuccessCount = ([regex]::Matches($Text, "Test Completed\. Result=\{Success\}")).Count
        $FailureCount = ([regex]::Matches($Text, "Test Completed\. Result=\{Fail(?:ure)?\}")).Count
        if ($SuccessCount -lt 1 -or $FailureCount -gt 0 -or $Text -match "TEST COMPLETE\. EXIT CODE: [1-9]") { $Good = $false }
        $Detail += " tests=$SuccessCount failures=$FailureCount"
    }
    elseif ($PassPattern -and $Text -notmatch $PassPattern) {
        $Good = $false
        $Detail += " required PASS marker missing"
    }
    $Results.Add([pscustomobject]@{
        Step=$Name; Result=$(if ($Good) { "PASS" } else { "FAIL" }); Detail=$Detail
        Seconds=[int]((Get-Date)-$Started).TotalSeconds
    })
    return $Good
}

function Invoke-Smoke {
    param([string]$Name, [string]$Script, [string[]]$Arguments, [string]$SourceLogDirectory, [string]$PassPattern)
    $PowerShell = (Get-Process -Id $PID).Path
    $ScriptPath = Join-Path $PSScriptRoot $Script
    $PSArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$ScriptPath`"") + $Arguments
    $Good = Invoke-ProcessStep -Name $Name -File $PowerShell -Arguments $PSArgs `
        -Timeout $SmokeTimeoutSeconds -PassPattern $PassPattern
    if ($SourceLogDirectory) {
        $Source = Join-Path $Root $SourceLogDirectory
        if (Test-Path -LiteralPath $Source) {
            $Destination = Join-Path $ArtifactRoot $Name
            New-Item -ItemType Directory -Force -Path $Destination | Out-Null
            Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    return $Good
}

$AllPassed = $true
try {
    if (!$SkipBuild) {
        $BuildCommand = "call `"$Build`" BrGameEditor Win64 Development -Project=`"$Project`" -WaitMutex"
        if (!(Invoke-ProcessStep -Name "Build-Editor" -File $env:ComSpec -Arguments @("/d", "/c", $BuildCommand) `
            -Timeout $BuildTimeoutSeconds -PassPattern "Result: Succeeded")) {
            $AllPassed = $false
            throw "Build failed; tests and smokes were not started."
        }
    }

    $AutomationArgs = @(
        "`"$Project`"", '"-ExecCmds=Automation RunTests BrGame; Quit"',
        "-unattended", "-nullrhi", "-nosound", "-log", "-nosteam",
        "-abslog=`"$(Join-Path $OutputRoot 'Automation.log')`""
    )
    if (!(Invoke-ProcessStep -Name "Automation-BrGame" -File $EditorCmd -Arguments $AutomationArgs `
        -Timeout $TestTimeoutSeconds -Automation)) { $AllPassed = $false; throw "Automation tests failed or matched zero tests." }

    $SmokeCases = @(
        @{ Name="Flow"; Script="RunFlowSmoke.ps1"; Args=@(); Logs="Saved\Logs\MP\Flow"; Pass="FLOW SMOKE PASS" },
        @{ Name="Flow-Grouped"; Script="RunFlowSmoke.ps1"; Args=@("-Grouped"); Logs="Saved\Logs\MP\FlowGrouped"; Pass="FLOW SMOKE PASS" },
        @{ Name="Flow-Zone"; Script="RunFlowSmoke.ps1"; Args=@("-Zone"); Logs="Saved\Logs\MP\Zone"; Pass="ZONE SMOKE PASS" },
        @{ Name="Flow-DisconnectGrace"; Script="RunFlowSmoke.ps1"; Args=@("-DisconnectGrace"); Logs="Saved\Logs\MP\FlowDisconnect"; Pass="FLOW DISCONNECT PASS" },
        @{ Name="Flow-LvlTopDown"; Script="RunFlowSmoke.ps1"; Args=@("-MatchLevel","Lvl_TopDown"); Logs="Saved\Logs\MP\Flow"; Pass="FLOW SMOKE PASS" },
        @{ Name="Fog"; Script="RunFogSmoke.ps1"; Args=@(); Logs="Saved\Logs\MP\Fog"; Pass="FOG SMOKE PASS" },
        @{ Name="Fog-Grouped"; Script="RunFogSmoke.ps1"; Args=@("-Grouped"); Logs="Saved\Logs\MP\FogGrouped"; Pass="FOG SMOKE PASS" },
        @{ Name="Session"; Script="RunSessionSmoke.ps1"; Args=@(); Logs="Saved\Logs\MP\Session"; Pass="PASS: Null create/find/join" },
        @{ Name="Match-AllVsAll"; Script="RunMatchSmoke.ps1"; Args=@(); Logs="Saved\Logs\MP\Match"; Pass="MATCH SMOKE PASS" },
        @{ Name="Match-Grouped"; Script="RunMatchSmoke.ps1"; Args=@("-Grouped"); Logs="Saved\Logs\MP\MatchGrouped"; Pass="MATCH SMOKE PASS" }
        @{ Name="Menu"; Script="RunMenuSmoke.ps1"; Args=@(); Logs="Saved\Logs\MP\Menu"; Pass="MENU SMOKE PASS" }
    )
    if ($Windowed) {
        $SmokeCases += @(
            @{ Name="Fog-Windowed"; Script="RunFogSmoke.ps1"; Args=@("-Windowed"); Logs="Saved\Logs\MP\FogWindowed"; Pass="FOG SMOKE PASS" },
            @{ Name="Fog-Grouped-Windowed"; Script="RunFogSmoke.ps1"; Args=@("-Grouped","-Windowed"); Logs="Saved\Logs\MP\FogGroupedWindowed"; Pass="FOG SMOKE PASS" }
        )
    }
    foreach ($Case in $SmokeCases) {
        if (!(Invoke-Smoke -Name $Case.Name -Script $Case.Script -Arguments $Case.Args `
            -SourceLogDirectory $Case.Logs -PassPattern $Case.Pass)) { $AllPassed = $false }
    }
}
catch {
    if ($Results.Count -eq 0 -or $Results[-1].Result -ne "FAIL") {
        $Results.Add([pscustomobject]@{ Step="Run"; Result="FAIL"; Detail=$_.Exception.Message; Seconds=0 })
    }
    $AllPassed = $false
}
finally {
    $Results | Format-Table -AutoSize | Out-String | ForEach-Object { Write-Output $_ }
    Write-Output "Logs: $RunRoot"
}
if (!$AllPassed -or @($Results | Where-Object Result -eq "FAIL").Count -gt 0) { exit 1 }
exit 0
