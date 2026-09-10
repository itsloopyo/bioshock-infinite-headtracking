#!/usr/bin/env pwsh
#Requires -Version 5.1
# Fully unattended release workflow for BioShockInfiniteHeadTracking.
# Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
#
# Running this command IS the authorization. There is no second gate: the
# release runs end to end with zero prompts. The preconditions below (clean
# tree, on main, tag absent, valid semver) are the safety net in place of
# any interactive confirmation - each fails fast with a non-zero exit.

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [string]$Version,
    [switch]$AllowDirty,
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

# Nightly takes its own path from here, and deliberately does NOT re-sync
# THIRD-PARTY-NOTICES: that step makes a commit, which a nightly has no business doing. If
# the submodule has been bumped without the notices being updated, packaging refuses and
# says so - run a real release, or the sync script by hand, to correct it.
if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1') -AllowDirty:$AllowDirty
    exit $LASTEXITCODE
}

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

function Write-NoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# --- 1. Resolve and validate the target version ------------------------
$cmakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$cmakeText = Get-Content $cmakePath -Raw
if ($cmakeText -notmatch 'project\(BioShockInfiniteHeadTracking VERSION (\d+\.\d+\.\d+)') {
    Write-Error "Could not parse current version from CMakeLists.txt"
    exit 1
}
$current = $Matches[1]

if ($Version -in @('major', 'minor', 'patch')) {
    $target = Get-NextVersion -Current $current -Bump $Version
} else {
    $target = $Version
}

if (-not (Test-SemVer -Version $target)) {
    Write-Error "Not a valid semver: $target"
    exit 1
}

# --- 2. Preconditions (these stand in for interactive confirmation) ----
$branch = (git -C $ProjectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Error "Releases must run on 'main' (currently on '$branch')."
    exit 1
}

if (-not $AllowDirty) {
    $status = git -C $ProjectRoot status --porcelain
    if ($status) {
        Write-Error "Working tree is not clean. Commit or stash changes before releasing."
        exit 1
    }
}

$tag = "v$target"
if (git -C $ProjectRoot tag --list $tag) {
    Write-Error "Tag $tag already exists."
    exit 1
}

# --- 3. THIRD-PARTY-NOTICES re-sync -----------------------------------
#
#
# Runs AFTER the preconditions above, not before them: it makes a commit, and a
# commit must not be the thing that happens when the version argument is missing
# or the release is being run from the wrong branch.
#
# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
function Test-GitClean {
    param([string]$Root, [string]$Path)
    # $ErrorActionPreference is deliberately relaxed for the call: a non-zero exit is
    # this command's ANSWER, not a failure, and under pwsh 7.4+ the strict setting
    # turns that answer into a terminating error.
    $ErrorActionPreference = 'Continue'
    & git -C $Root diff --quiet -- $Path
    return ($LASTEXITCODE -eq 0)
}

$noticesRoot = $ProjectRoot
if (-not (Test-GitClean -Root $noticesRoot -Path 'THIRD-PARTY-NOTICES.md')) {
    throw "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run."
}
& (Join-Path $noticesRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $noticesRoot
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
if (-not (Test-GitClean -Root $noticesRoot -Path 'THIRD-PARTY-NOTICES.md')) {
    & git -C $noticesRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

Write-Host "Releasing $current -> $target" -ForegroundColor Cyan

# --- 3. Changelog from commits since the last tag ----------------------
# This is the gate that aborts when there are no user-facing commits, so run
# it BEFORE mutating any version files or building - a failure here then
# leaves a clean tree instead of stranding a half-applied version bump with
# no tag.
$changelogPath = Join-Path $ProjectRoot 'CHANGELOG.md'
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
$hasTags = git -C $ProjectRoot tag -l 2>$null
if (-not $hasTags) {
    # First release - ensure a baseline CHANGELOG exists
    if (-not (Test-Path $changelogPath)) {
        $date = Get-Date -Format 'yyyy-MM-dd'
        Set-Content $changelogPath "# Changelog`n`n## [$target] - $date`n`nFirst release.`n"
    }
} else {
    try {
        $changelogArgs = @{
            ChangelogPath = $changelogPath
            Version       = $target
            # Kept in step with artifact-paths in .github/workflows/release.yml: the
            # committed CHANGELOG and the published release notes are generated from
            # these two lists, so a difference makes them disagree about the release.
            ArtifactPaths = @('src/', 'cameraunlock-core', 'scripts/install.cmd',
                              'scripts/uninstall.cmd', 'CMakeLists.txt',
                              'launcher-manifest.json')
        }
        New-ChangelogFromCommits @changelogArgs | Out-Null
    } catch {
        if (-not $Force) {
            Write-Error "$($_.Exception.Message)`nNo user-facing changes to release. Re-run with -Force for a maintenance release."
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $target
    }
}

# --- 4. Bump the canonical version (CMakeLists.txt) + derived copies ---
# CMakeLists.txt is the canonical version: the build compiles it in as
# HEADTRACKING_VERSION, which is what the DLL logs at attach, so there is no
# second copy in the source to drift. MOD_VERSION is what install.cmd writes
# into .headtracking-state.json. All three must move together or shipped
# artifacts report a stale version.
$versionFiles = @(
    @{ Path = 'CMakeLists.txt';      Pattern = 'project\(BioShockInfiniteHeadTracking VERSION \d+\.\d+\.\d+'; Replacement = "project(BioShockInfiniteHeadTracking VERSION $target" },
    @{ Path = 'pixi.toml';           Pattern = '(?m)^version = "\d+\.\d+\.\d+"';                        Replacement = "version = `"$target`"" },
    @{ Path = 'scripts/install.cmd'; Pattern = '(?m)^set "MOD_VERSION=\d+\.\d+\.\d+"';                  Replacement = "set `"MOD_VERSION=$target`"" }
)
foreach ($vf in $versionFiles) {
    $vfPath = Join-Path $ProjectRoot $vf.Path
    $vfText = Get-Content $vfPath -Raw
    if ($vfText -notmatch $vf.Pattern) {
        Write-Error "Version pattern not found in $($vf.Path) - cannot bump."
        exit 1
    }
    Write-NoBom -Path $vfPath -Text ($vfText -replace $vf.Pattern, $vf.Replacement)
}

# --- 5. Build, test, package and validate ------------------------------
#
# The whole gate, before anything is committed, tagged or pushed. `build-release`
# on its own compiles and stops: a dropped XInput export, a failing unit test or a
# manifest the launcher would reject all survived it and surfaced in CI instead -
# by which point the tag was public and recovery meant deleting it and
# force-pushing over the release commit. verify-package pulls in build-release,
# test, the packager's export check and both validators.
Write-Host "Building, testing, packaging and validating..." -ForegroundColor Cyan
pixi run verify-package
if ($LASTEXITCODE -ne 0) {
    Write-Error ("Release gate failed (build, tests, packaging or validation). No tag " +
                 "was created and nothing was pushed, but the version bump IS written to " +
                 "CMakeLists.txt, pixi.toml, scripts/install.cmd and CHANGELOG.md, and the " +
                 "THIRD-PARTY-NOTICES re-sync may already be committed. Fix the failure, " +
                 "then either re-run or 'git restore' those files.")
    exit 1
}

# --- 6. Commit the version bump + changelog ----------------------------
git -C $ProjectRoot add CMakeLists.txt pixi.toml scripts/install.cmd CHANGELOG.md
git -C $ProjectRoot commit -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git commit failed."; exit 1 }

# --- 7. Annotated tag --------------------------------------------------
git -C $ProjectRoot tag -a $tag -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git tag failed."; exit 1 }

# --- 8. Push commits + tag (triggers .github/workflows/release.yml) ----
git -C $ProjectRoot push origin HEAD
if ($LASTEXITCODE -ne 0) { Write-Error "git push (commits) failed."; exit 1 }
git -C $ProjectRoot push origin $tag
if ($LASTEXITCODE -ne 0) { Write-Error "git push (tag) failed."; exit 1 }

Write-Host "Released $tag" -ForegroundColor Green
