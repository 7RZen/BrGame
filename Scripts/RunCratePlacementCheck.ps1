param(
    [ValidateSet("Town", "Battlefield", "TestArena")][string[]]$Maps = @("Town", "Battlefield", "TestArena"),
    [switch]$Windowed,
    [ValidateRange(45, 180)][int]$Seconds = 65
)
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
if (@(Get-Process UnrealEditor*,LiveCodingConsole -ErrorAction SilentlyContinue).Count) {
    throw "Close editor/Live Coding before running this serial check."
}
$Mode = if ($Windowed) { "Windowed" } else { "NullRHI" }
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
foreach ($Map in $Maps) {
    $Package = switch ($Map) {
        "Town" { "/Game/BrGame/Maps/L_Town" }
        "Battlefield" { "/Game/BrGame/Maps/L_BattleField" }
        "TestArena" { "/Game/TopDown/Lvl_TopDown" }
    }
    $Initial = if ($Map -eq "Town") { 6 } else { 2 }
    $Periodic = if ($Map -eq "Town") { 3 } else { 1 }
    $Log = Join-Path $Root "Saved\Logs\CratePlacement-$Stamp-$Map-$Mode.log"
    $Arguments = @(
        "`"$Root\BrGame.uproject`"",
        "$Package`?listen?game=/Script/BrGame.BrGameMode?Name=CrateCheck",
        "-game", "-unattended", "-nosound", "-nosteam", "-log", "-BrCrateNavProbe",
        "-abslog=`"$Log`"",
        '-LogCmds="LogNavigation Verbose,LogNavigationDataBuild Verbose,LogNavMesh Verbose"'
    )
    if ($Windowed) { $Arguments += @("-windowed", "-ResX=960", "-ResY=540", "-NoSplash") }
    else { $Arguments += "-nullrhi" }
    Write-Host "Starting $Map $Mode ($Seconds real seconds, default host interval; $Log)"
    $Owned = Start-Process -FilePath $UE -ArgumentList $Arguments -PassThru
    try {
        $Deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $Deadline) {
            Start-Sleep -Seconds 1
            if ($Owned.HasExited) { throw "$Map $Mode exited early ($($Owned.ExitCode)); see $Log" }
        }
    }
    finally {
        if (Get-Process -Id $Owned.Id -ErrorAction SilentlyContinue) {
            Stop-Process -Id $Owned.Id
            Wait-Process -Id $Owned.Id -ErrorAction SilentlyContinue
            # Wait-Process can return before process enumeration drops the exited
            # editor. Avoid racing the next serial invocation's safety check.
            $StopDeadline = (Get-Date).AddSeconds(5)
            while ((Get-Process -Id $Owned.Id -ErrorAction SilentlyContinue) -and (Get-Date) -lt $StopDeadline) {
                Start-Sleep -Milliseconds 100
            }
        }
    }
    $Text = Get-Content $Log -Raw
    if ($Text -notmatch "Crate initial wave placed: Map=$Map Count=$Initial Spawned=$Initial\b" -or
        $Text -notmatch "Crate interval wave placed: Map=$Map Count=$Periodic Spawned=$Periodic\b") {
        throw "$Map $Mode did not place the configured initial/interval waves; see $Log"
    }
    $Landings = [regex]::Matches($Text, 'Crate landed: [^\r\n]* Inside=(?<Inside>\d+) Items=(?<Spawned>\d+)/(?<Requested>\d+)')
    if ($Landings.Count -ne ([regex]::Matches($Text, 'Crate landed:')).Count) {
        throw "Unparsed landing record; cannot verify every crate's loot: $Log"
    }
    if ($Landings.Count -lt ($Initial + $Periodic)) {
        throw "$Map $Mode has only $($Landings.Count) landings; see $Log"
    }
    if ($Text -match "Crate landing cancelled|Crate drop skipped|Crate loot item skipped|Fatal error:|Assertion failed:") {
        throw "$Map $Mode skipped/cancelled a crate or loot item, or crashed; see $Log"
    }
    $Surfaces = [regex]::Matches($Text, "Crate falling: .*Inside=1 Attempts=\d+ Ground=\w+ TaggedStatic=1 CapsuleClear=1 PawnClear=1 WaveClear=1")
    if ($Surfaces.Count -lt ($Initial + $Periodic)) { throw "Missing placement clearance evidence: $Log" }
    foreach ($Landing in $Landings) {
        $Spawned = [int]$Landing.Groups['Spawned'].Value
        $Requested = [int]$Landing.Groups['Requested'].Value
        if ($Landing.Groups['Inside'].Value -ne '1') { throw "Crate landed outside the zone: $Log" }
        if ($Requested -le 0 -or $Spawned -ne $Requested) {
            throw "Crate scattered $Spawned/$Requested items; complete loot required: $Log"
        }
    }
    if ($Map -eq "TestArena") {
        # This fixture's broad floor is Z=0; SM_Cylinder's tagged cap is Z=210.
        # Assert physical landing height, not just the tag/success log flags.
        $Heights = [regex]::Matches($Text, 'Crate landed: Location=[^\r\n]* Z=(-?[\d.]+) Inside=1')
        if ($Heights.Count -ne $Landings.Count) { throw "Missing TestArena landing height evidence: $Log" }
        foreach ($Height in $Heights) {
            if ([double]::Parse($Height.Groups[1].Value, [cultureinfo]::InvariantCulture) -gt 50) {
                throw "TestArena crate landed on a raised cap rather than the floor: $Log"
            }
        }
    }
    $Nav = [regex]::Matches($Text, "Crate nav probe: [^\r\n]+")
    Write-Host "PASS $Map $Mode initial=$Initial periodic=$Periodic landings=$($Landings.Count) verifiedSurfaces=$($Surfaces.Count)"
    if ($Nav.Count) {
        Write-Host $Nav[0].Value
        Write-Host $Nav[$Nav.Count - 1].Value
    }
    Write-Host "Log: $Log"
}
