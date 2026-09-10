#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built xinput1_3.dll to the game's Binaries/Win32/ directory for local
# testing. The game imports XINPUT1_3.dll, so dropping it beside the exe is the whole
# loading mechanism - there is nothing else to install.
#
# Usage: deploy.ps1 [Debug|Release] [GamePath]
# Defaults to Debug. An explicit GamePath wins over auto-detection
# (same contract as install.cmd).

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$payload = Join-Path $projectDir "bin/$Configuration/xinput1_3.dll"
if (-not (Test-Path $payload)) {
    throw "Build output not found: $payload. Run 'pixi run build' or 'pixi run build-release' first."
}

if ($GamePath) {
    if (-not (Test-Path $GamePath)) {
        throw "Explicit game path does not exist: $GamePath"
    }
    $gamePath = $GamePath
} else {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $gamePath = Find-GamePath -GameId 'bioshock-infinite'
    if (-not $gamePath) {
        throw "Could not locate BioShock Infinite. Set BIOSHOCK_INFINITE_PATH, install via Steam, or pass the game path: deploy.ps1 $Configuration <path>"
    }
}

$exeDir = Join-Path $gamePath 'Binaries\Win32'
if (-not (Test-Path $exeDir)) {
    throw "Expected exe directory not found: $exeDir"
}

# The payload takes a SYSTEM DLL's name, so anything already sitting there under that
# name is someone else's - another wrapper mod, or a hand-placed copy - and overwriting it
# is unrecoverable.
#
# The content compare is the whole of it, and install-body-shim.cmd does the same thing for
# the same reason. Backing up whenever no backup exists enshrines OUR shim as "the
# original" on the second deploy: uninstall then finds a .backup, restores it, prints
# "Restored original xinput1_3.dll", and leaves a stale build of this mod loading on every
# launch with nothing to say so.
$dest = Join-Path $exeDir (Split-Path $payload -Leaf)
$backup = "$dest.backup"
if ((Test-Path $dest) -and -not (Test-Path $backup)) {
    # Is the file already there ONE OF OURS, rather than identical to the build being
    # deployed? A developer rebuilds between deploys, so comparing against the current
    # payload would answer "different" every time and enshrine the previous build as the
    # original: uninstall would then restore it, print "Restored original xinput1_3.dll",
    # and leave a stale mod loading on every launch with nothing to say so.
    $marker = [Text.Encoding]::ASCII.GetBytes('BioShock Infinite Head Tracking')
    $bytes = [IO.File]::ReadAllBytes($dest)
    $isOurs = $false
    for ($i = 0; $i -le $bytes.Length - $marker.Length; $i++) {
        if ($bytes[$i] -ne $marker[0]) { continue }
        $match = $true
        for ($j = 1; $j -lt $marker.Length; $j++) {
            if ($bytes[$i + $j] -ne $marker[$j]) { $match = $false; break }
        }
        if ($match) { $isOurs = $true; break }
    }
    if (-not $isOurs) {
        Copy-Item $dest $backup
        Write-Host "Kept the file already there as $(Split-Path $backup -Leaf)" -ForegroundColor Yellow
    }
}

Copy-Item $payload -Destination $exeDir -Force
Write-Host "Deployed: $payload -> $exeDir" -ForegroundColor Green
