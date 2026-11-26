# 编译 H1K28 IAP/Bootloader 最小验证
# 输出：iap_test/build/boot.hex（0x0000）、app.hex（0x1000）

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path -Parent $root
$out  = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$sdcc = (Get-Command sdcc -ErrorAction Stop).Source
$pack = (Get-Command packihx -ErrorAction Stop).Source

$common = @(
    "-mmcs51",
    "--iram-size", "256",
    "--xram-size", "1024",
    "--stack-auto",
    "-I", (Join-Path $proj "inc"),
    "-I", (Join-Path $proj "lib\FwLib_STC8-master"),
    "-I", (Join-Path $proj "lib\FwLib_STC8-master\include"),
    "-I", (Join-Path $proj "src"),
    "-I", $root,
    (Join-Path $root "test_main.c")
)

# Boot: 0x0000-0x0FFF
& $sdcc @common "-DTEST_BOOT" "--code-loc" "0x0000" "--code-size" "0x1000" "-o" (Join-Path $out "boot.ihx") 2>$null
if ($LASTEXITCODE -ne 0) { throw "boot compile failed" }
& $pack (Join-Path $out "boot.ihx") 2>$null | Set-Content (Join-Path $out "boot.hex") -Encoding ascii

# App: 0x1800-0x69FF
& $sdcc @common "-DTEST_APP" "--code-loc" "0x1800" "--code-size" "0x5200" "-Wl-bHOME=0x1800" "-o" (Join-Path $out "app.ihx") 2>$null
if ($LASTEXITCODE -ne 0) { throw "app compile failed" }
& $pack (Join-Path $out "app.ihx") 2>$null | Set-Content (Join-Path $out "app.hex") -Encoding ascii

$merge = Join-Path $root "merge_hex.py"
& python $merge (Join-Path $out "boot.hex") (Join-Path $out "app.hex") (Join-Path $out "combined.hex") 2>$null
if ($LASTEXITCODE -ne 0) { throw "merge failed" }

Write-Host "OK:"
Get-ChildItem $out -Filter *.hex | Select-Object Name, Length








