[CmdletBinding()]
param([switch]$AllowDirty)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

# Same function release-mod.yml validates the tag against, so a nightly and a
# tagged release can never read a different version out of CMakeLists.txt.
$version = Get-ProjectVersion -Source cmake -Path (Join-Path $ProjectRoot 'CMakeLists.txt')

# -NoNexusZip because this mod is installer-only: the payload is a proxy DLL that has to
# land beside the game exe, which no mod manager can deploy to, so the packager produces
# no Nexus archive. Publish-NightlyBuild treats a missing one as fatal by default.
Publish-NightlyBuild `
    -ModId 'bioshock-infinite' `
    -ModName 'BioShockInfiniteHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -NoNexusZip `
    -AllowDirty:$AllowDirty
