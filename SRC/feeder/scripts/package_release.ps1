# 生成飞达发布包：BL + APP + FULL
# 菜单：Terminal -> Run Task -> "package feeder"
# 前提：App 已用 EIDE 编译到 build/Debug/ddfeedermcu.hex
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $py) { $py = (Get-Command py -ErrorAction SilentlyContinue).Source }
if (-not $py) { throw "python/py not found" }

# BL 必须与当前 src/version.h 同版本
& (Join-Path $root "bootloader\build.ps1")

$verH = Get-Content (Join-Path $root "src\version.h") -Raw
$yymm = [regex]::Match($verH, 'FW_YYMM_STR\s+"(\d+)"').Groups[1].Value
$ddbn = [regex]::Match($verH, 'FW_DD_BN_STR\s+"(\d+)"').Groups[1].Value
$bl = Join-Path $root "bootloader\build\DDF_${yymm}${ddbn}_BL.hex"
if (-not (Test-Path $bl)) {
    throw "BL build failed: $bl"
}

& $py (Join-Path $root "scripts\package_feeder.py")
if ($LASTEXITCODE -ne 0) { throw "package_feeder failed" }

