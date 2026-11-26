# 生成飞达 Bootloader: build/DDF_<yymmddnn>_BL.hex
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path -Parent $root
$out  = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# VS Code task 的 PATH 可能没有 EIDE 自带 SDCC；显式补上
$eideSdcc = "C:\Users\Administrator\.eide\tools\sdcc\bin"
if ($env:USERPROFILE) {
    $p = Join-Path $env:USERPROFILE ".eide\tools\sdcc\bin"
    if (Test-Path (Join-Path $p "sdcc.exe")) { $eideSdcc = $p }
}
if (Test-Path (Join-Path $eideSdcc "sdcc.exe")) {
    $env:PATH = "$eideSdcc;$env:PATH"
}

$verH = Get-Content (Join-Path $proj "src\version.h") -Raw
$yymm = [regex]::Match($verH, 'FW_YYMM_STR\s+"(\d+)"').Groups[1].Value
$ddbn = [regex]::Match($verH, 'FW_DD_BN_STR\s+"(\d+)"').Groups[1].Value
if (-not $yymm -or -not $ddbn) { throw "version.h missing FW_YYMM_STR/FW_DD_BN_STR" }
$ver = "$yymm$ddbn"

$sdcc = (Get-Command sdcc -ErrorAction Stop).Source
$sdas = (Get-Command sdas8051 -ErrorAction Stop).Source
$pack = (Get-Command packihx -ErrorAction Stop).Source
$tmp  = Join-Path $out "obj"
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$inc = @(
    "-I", (Join-Path $proj "inc"),
    "-I", (Join-Path $proj "lib\FwLib_STC8-master\include"),
    "-I", (Join-Path $proj "src"),
    "-I", (Join-Path $root "src")
)
$mods = @("boot_main", "boot_uart", "boot_iap", "boot_proto", "boot_flag")
$rels = @()
foreach ($m in $mods) {
    $rel = Join-Path $tmp "$m.rel"
    & $sdcc -mmcs51 --iram-size 256 --xram-size 1024 --code-size 6144 --stack-auto @inc `
        -c (Join-Path $root "src\$m.c") -o $rel
    if ($LASTEXITCODE -ne 0) { throw "$m compile failed" }
    $rels += $rel
}
$vecRel = Join-Path $tmp "boot_vectors.rel"

Push-Location $tmp
& $sdas -plosgff -o $vecRel (Join-Path $root "src\boot_vectors.asm")
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "boot_vectors assemble failed" }
Pop-Location
$rels += $vecRel


$ihx = Join-Path $out "bootloader.ihx"
& $sdcc -mmcs51 --iram-size 256 --xram-size 1024 --code-loc 0x0100 --code-size 0x1800 --stack-auto @rels -o $ihx
if ($LASTEXITCODE -ne 0) { throw "bootloader link failed" }

$hex = Join-Path $out "DDF_${ver}_BL.hex"
& $pack $ihx 2>$null | Set-Content $hex -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw "packihx failed" }

Write-Host "OK: $hex"
Get-Item $hex | Select-Object Name, Length
exit 0








