# 只编译 UART 最小测试：0x0000，不需要 Boot/App/IAP
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path -Parent $root
$out  = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$sdcc = (Get-Command sdcc -ErrorAction Stop).Source
$pack = (Get-Command packihx -ErrorAction Stop).Source

$args = @(
    "-mmcs51", "--iram-size", "256", "--xram-size", "1024", "--stack-auto",
    "-I", (Join-Path $proj "inc"),
    "-I", (Join-Path $proj "lib\FwLib_STC8-master"),
    "-I", (Join-Path $proj "lib\FwLib_STC8-master\include"),
    "-I", (Join-Path $proj "src"),
    "-I", $root,
    (Join-Path $root "uart_only.c"),
    "--code-loc", "0x0000", "--code-size", "0x1000",
    "-o", (Join-Path $out "uart.ihx")
)
& $sdcc @args 2>$null
if ($LASTEXITCODE -ne 0) { throw "uart compile failed" }
& $pack (Join-Path $out "uart.ihx") 2>$null | Set-Content (Join-Path $out "uart.hex") -Encoding ascii
Write-Host "OK: iap_test/build/uart.hex"
Get-Item (Join-Path $out "uart.hex") | Select-Object Length
