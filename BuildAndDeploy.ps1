param(
    [Alias("v")]
    [string]$Version = "0.0.1",
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$engineRoot      = "C:\Program Files\Epic Games\UE_5.8"
$projectRoot     = $PSScriptRoot
$projectFile     = Join-Path $projectRoot "BrGame.uproject"
$archiveRoot     = Join-Path $projectRoot "Build\Package"
$sourcePath      = Join-Path $archiveRoot "Windows"
$destinationFile = "C:\Users\oiste\Jottacloud\Temp\BrGame\BrGame_$Version.zip"

if (-not $SkipBuild) {
    # Packaging fails or produces stale output while the editor / Live Coding holds project binaries.
    $running = Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -match '^(UnrealEditor|UnrealEditor-Cmd|LiveCodingConsole)$' }
    if ($running) {
        throw "Close Unreal Editor / Live Coding before packaging (running: $($running.ProcessName -join ', '))."
    }

    $runUat = Join-Path $engineRoot "Engine\Build\BatchFiles\RunUAT.bat"
    Write-Host "Building and packaging BrGame ($Configuration)..." -ForegroundColor Cyan
    & $runUat BuildCookRun `
        "-project=$projectFile" `
        -noP4 -platform=Win64 "-clientconfig=$Configuration" `
        -build -cook -stage -pak -archive `
        "-archivedirectory=$archiveRoot" -utf8output
    if ($LASTEXITCODE -ne 0) {
        throw "BuildCookRun failed with exit code $LASTEXITCODE. Nothing was zipped."
    }
}

if (-not (Test-Path (Join-Path $sourcePath "BrGame.exe"))) {
    throw "Packaged game not found at $sourcePath."
}

$destinationDir = Split-Path $destinationFile
New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
if (Test-Path $destinationFile) { Remove-Item $destinationFile -Force }

Write-Host "Zipping $sourcePath -> $destinationFile ..." -ForegroundColor Cyan
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $sourcePath, $destinationFile, [System.IO.Compression.CompressionLevel]::Optimal, $false)

$sizeMb = [Math]::Round((Get-Item $destinationFile).Length / 1MB, 1)
Write-Host "Done: $destinationFile ($sizeMb MB)" -ForegroundColor Green
