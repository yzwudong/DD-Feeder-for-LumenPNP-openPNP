# EIDE 重建 App -> BL -> APP/FULL
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

& (Join-Path $root "scripts\build_app.ps1")
& (Join-Path $root "scripts\package_release.ps1")
