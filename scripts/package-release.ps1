#!/usr/bin/env pwsh
#Requires -Version 5.1
# Packaging for BioShock Infinite Head Tracking (C++ project, no .csproj).
#
# ONE ZIP, deliberately: BioShockInfiniteHeadTracking-v{version}-installer.zip.
#
# There is no -nexus.zip stage and there must not be one. The payload is a system-DLL
# proxy that has to land beside BioShockInfinite.exe in Binaries\Win32, and a mod manager
# deploys into one fixed subtree below the game folder. Vortex ships no BioShock Infinite
# extension at all (checked against its bundledPlugins directory: 132 game extensions,
# none for this game), so there is no queryModPath that could reach the exe's directory
# and nothing for a Nexus archive layout to be right about. A ZIP dragged into a manager
# would deploy somewhere the game never looks, the manager would report success, and the
# game would start normally with no head tracking and no log - which is the exact silent
# failure this note exists to stop the next session from re-creating.
#
# Consequences that go with that decision, so they are not rediscovered separately:
#   - scripts/release-nightly.ps1 passes -NoNexusZip. Without it a nightly fails, because
#     Publish-NightlyBuild treats a missing Nexus ZIP as fatal by default.
#   - There is no NEXUS_MODS.md, because there is no Nexus page.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# The same file release-mod.yml validates the pushed tag against, so a packaged ZIP and a
# tagged release can never disagree about the version.
$cmakeLists = Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(BioShockInfiniteHeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]
$modName = 'BioShockInfiniteHeadTracking'
$gameId  = 'bioshock-infinite'
$payload = 'xinput1_3.dll'

Write-Host ""
Write-Host "=== Packaging $modName v$version ===" -ForegroundColor Magenta
Write-Host ""

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$dllPath = Join-Path $projectDir "bin/Release/$payload"
if (-not (Test-Path $dllPath)) {
    throw "$payload not found at: $dllPath. Run 'pixi run build-release' first."
}

# The game imports XINPUT1_3.dll by ordinal and the loader binds it before main() runs, so
# a payload that lost its export table does not degrade - it stops the game from starting
# at all, after install.cmd has already moved the real DLL aside. That failure is
# invisible until a player launches, so it is checked here, on the bytes being shipped.
#
# The export table is read out of the PE directly rather than shelled out to dumpbin:
# dumpbin lives inside the MSVC toolchain and is only on PATH inside a developer prompt,
# so a check that depended on it would be skipped on exactly the machines that package
# without one. A check that can be skipped is not a check.
function Get-DllExportNames {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) { throw "$Path is not a PE image" }
    $numSections = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
    $optOffset = $peOffset + 24
    $magic = [BitConverter]::ToUInt16($bytes, $optOffset)
    # PE32 puts the data directories 96 bytes into the optional header, PE32+ 112.
    $dirOffset = $optOffset + $(if ($magic -eq 0x20B) { 112 } else { 96 })
    $exportRva = [BitConverter]::ToUInt32($bytes, $dirOffset)
    if ($exportRva -eq 0) { return @() }

    $sectionOffset = $optOffset + [BitConverter]::ToUInt16($bytes, $peOffset + 20)
    $sections = @()
    for ($i = 0; $i -lt $numSections; $i++) {
        $s = $sectionOffset + $i * 40
        $sections += [pscustomobject]@{
            Va   = [BitConverter]::ToUInt32($bytes, $s + 12)
            Size = [Math]::Max([BitConverter]::ToUInt32($bytes, $s + 8), [BitConverter]::ToUInt32($bytes, $s + 16))
            Raw  = [BitConverter]::ToUInt32($bytes, $s + 20)
        }
    }
    function Convert-RvaToOffset {
        param([uint32]$Rva)
        foreach ($sec in $sections) {
            if ($Rva -ge $sec.Va -and $Rva -lt ($sec.Va + $sec.Size)) { return $sec.Raw + ($Rva - $sec.Va) }
        }
        throw "RVA 0x$($Rva.ToString('X')) is outside every section"
    }

    $exportOffset = Convert-RvaToOffset $exportRva
    $nameCount = [BitConverter]::ToUInt32($bytes, $exportOffset + 24)
    $namesRva  = [BitConverter]::ToUInt32($bytes, $exportOffset + 32)
    $namesOffset = Convert-RvaToOffset $namesRva
    $names = @()
    for ($i = 0; $i -lt $nameCount; $i++) {
        $strOffset = Convert-RvaToOffset ([BitConverter]::ToUInt32($bytes, $namesOffset + $i * 4))
        $end = $strOffset
        while ($bytes[$end] -ne 0) { $end++ }
        $names += [System.Text.Encoding]::ASCII.GetString($bytes, $strOffset, $end - $strOffset)
    }
    return $names
}

$exports = Get-DllExportNames -Path $dllPath
foreach ($required in @('XInputGetState', 'XInputSetState', 'XInputGetCapabilities', 'XInputEnable')) {
    if ($exports -notcontains $required) {
        throw "$payload exports no $required (found: $($exports -join ', ')). The game binds XInput at load time; shipping this would stop BioShock Infinite from starting."
    }
}
Write-Host "  exports verified ($($exports.Count) named: $($exports -join ', '))" -ForegroundColor Green

$scriptsDir = Join-Path $projectDir 'scripts'
foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    if (-not (Test-Path (Join-Path $scriptsDir $s))) {
        throw "Required script not found: $s"
    }
}

$modManifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $modManifestPath)) {
    throw "launcher-manifest.json not found at: $modManifestPath"
}

Write-Host '--- Installer ZIP ---' -ForegroundColor Yellow

$staging = Join-Path $releaseDir 'staging-installer'
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Path $staging -Force | Out-Null

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptsDir $s) -Destination $staging -Force
}

# install.cmd / uninstall.cmd are thin wrappers: they resolve the game through
# shared/find-game.ps1 and dispatch to shared/install-body-shim.cmd. Bundle that shared
# tree so the release ZIP is self-contained.
Copy-SharedBundle -StagingDir $staging

# Copy-SharedBundle ships the whole fleet's shared tree: every framework's install body,
# and the canonical games.json listing every title CameraUnlock targets. Both go out to
# anyone who downloads this mod. Reduce the bundle to what this ZIP's own scripts read,
# so the download describes this mod and nothing else.
$sharedDir = Join-Path $staging 'shared'
$wrapperText = (Get-Content (Join-Path $staging 'install.cmd') -Raw) +
               (Get-Content (Join-Path $staging 'uninstall.cmd') -Raw)
foreach ($body in (Get-ChildItem $sharedDir -Filter '*-body*.cmd' -ErrorAction SilentlyContinue)) {
    if ($wrapperText -notmatch [regex]::Escape($body.Name)) {
        Remove-Item $body.FullName -Force
    }
}

# Dropping the bodies strands their helpers: check-loader-arch.ps1 is only ever invoked
# from install-body-bepinex.cmd, which is no longer in the ZIP. Remove every helper that
# nothing still shipping names, re-running until a pass removes nothing so a helper
# reachable only through an already-removed helper goes with it. find-game.ps1 is named by
# install-body-shim.cmd and GamePathDetection.psm1 by find-game.ps1, so both survive.
do {
    $removedHelper = $false
    foreach ($helper in (Get-ChildItem $sharedDir -File | Where-Object { $_.Extension -in @('.ps1', '.psm1') })) {
        $others = Get-ChildItem $sharedDir -File | Where-Object { $_.FullName -ne $helper.FullName }
        $reachable = $wrapperText + (($others | ForEach-Object { Get-Content $_.FullName -Raw }) -join "`n")
        if ($reachable -notmatch [regex]::Escape($helper.Name)) {
            Remove-Item $helper.FullName -Force
            $removedHelper = $true
        }
    }
} while ($removedHelper)

# The trim above decides by TEXT SEARCH, so a rename upstream in Copy-SharedBundle silently
# turns a live helper into an unreachable one and deletes it. The ZIP still builds, and the
# failure lands on the user as "find-game.ps1 not found" at install time. Assert the two
# the installer cannot run without.
# All four, not just the two the second loop can remove: the bodies are trimmed by the
# first loop, on the same text search, and losing one of those breaks install.cmd outright
# rather than only its game-path lookup.
foreach ($required in @('install-body-shim.cmd', 'uninstall-body.cmd',
                        'find-game.ps1', 'GamePathDetection.psm1')) {
    if (-not (Test-Path (Join-Path $sharedDir $required))) {
        throw "shared/$required was trimmed as unreachable, so the installer could not run. A file it is named by was probably renamed upstream."
    }
}

# find-game.ps1 looks up exactly one id. Keep that entry; the loader's only structural
# requirement is a non-empty top-level `games` object.
$gamesPath = Join-Path $sharedDir 'games.json'
$games = Get-Content $gamesPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $games.games.PSObject.Properties.Name.Contains($gameId)) {
    throw "games.json has no '$gameId' entry, so the installer could not resolve the game. Add it in cameraunlock-core/data/games.json."
}
$games.games = [PSCustomObject]@{ $gameId = $games.games.$gameId }
# $comment is guidance for people editing the canonical file and names other repos.
# Nothing at install time reads it.
$games.PSObject.Properties.Remove('$comment')
[System.IO.File]::WriteAllText($gamesPath, ($games | ConvertTo-Json -Depth 10),
                               (New-Object System.Text.UTF8Encoding $false))
Write-Host "  shared/ trimmed to $gameId" -ForegroundColor Green

$pluginsDir = Join-Path $staging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $dllPath -Destination $pluginsDir -Force

# A binary distribution, so our own MIT LICENSE and the third-party notices for
# everything compiled into the payload ship at the ZIP root.
foreach ($doc in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) {
        throw "Required notice file not found: $doc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item -Path $p -Destination $staging -Force
}

# The launcher reads launcher-manifest.json from the ZIP root to ingest the package.
# Stamp the version from the build so the shipped manifest can never disagree with the
# built DLL. The only "version": "X.Y.Z" string is mod_info.version; schema_version is a
# bare number and is left untouched by the regex.
$stagedManifest = Join-Path $staging 'launcher-manifest.json'
$manifestText = Get-Content $modManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText($stagedManifest, $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "  launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Push-Location $staging
try { Compress-Archive -Path '.\*' -DestinationPath $installerZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $staging

$installerKb = [math]::Round((Get-Item $installerZip).Length / 1KB, 1)
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKb) -ForegroundColor Green

Write-Host ''
Write-Host '=== Package Complete ===' -ForegroundColor Magenta

Write-Output $installerZip
