# Downloads the Tectonic binary that PlaciDocs bundles, into third_party/tectonic.
# Usage:  pwsh scripts/fetch-tectonic.ps1 [-Version 0.17.0]
#
# This is a developer step (the binary is not stored in the repository).
# The LaTeX packages are bundled separately by the CMake target `texcache`:
#   cmake --build build --target texcache
# End users get both with the app and never need this script or the network.
param([string]$Version = "0.17.0")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party/tectonic"
New-Item -ItemType Directory -Force $dest | Out-Null

if ($IsWindows -or $env:OS -eq "Windows_NT") {
    $asset = "tectonic-$Version-x86_64-pc-windows-msvc.zip"
} elseif ($IsMacOS) {
    $asset = "tectonic-$Version-x86_64-apple-darwin.tar.gz"
} else {
    $asset = "tectonic-$Version-x86_64-unknown-linux-gnu.tar.gz"
}
$url = "https://github.com/tectonic-typesetting/tectonic/releases/download/tectonic%40$Version/$asset"
$archive = Join-Path ([IO.Path]::GetTempPath()) $asset
Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $archive
if ($asset.EndsWith(".zip")) {
    Expand-Archive -Force $archive -DestinationPath $dest
} else {
    tar -xzf $archive -C $dest
}
Remove-Item $archive
Write-Host "Tectonic installed in $dest"
