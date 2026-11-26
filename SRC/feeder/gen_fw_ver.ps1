# gen_fw_ver.ps1
Write-Host "===== Pre-build script start ====="
Write-Host "Current Path: $(Get-Location)"
$headerPath = Join-Path $PSScriptRoot "src/version.h"
$oldYYMM = 0
$oldDDBn = 0

$now = Get-Date
$yy = $now.ToString("yy")
$mm = $now.ToString("MM")
$dd = [int]$now.ToString("dd")
$currentYYMM = [int]"$yy$mm"

# 读取旧头文件，正则匹配末尾带U
if(Test-Path $headerPath){
    $content = Get-Content $headerPath
    $mYYMM = $content | Select-String -Pattern '#define FW_YYMM\s+(\d+)\s*U?'
    $mDDBn = $content | Select-String -Pattern '#define FW_DD_BN\s+(\d+)\s*U?'
    if($mYYMM){$oldYYMM = [int]$mYYMM.Matches.Groups[1].Value}
    if($mDDBn){$oldDDBn = [int]$mDDBn.Matches.Groups[1].Value}
}

# 从旧DDBN十进制数字，提取DD和BN
if($oldDDBn -ge 100){
    $oldDD = [math]::Floor($oldDDBn / 100)
    $oldBN = $oldDDBn % 100
}else{
    $oldDD = 0
    $oldBN = 0
}

# 判断：换年月 / 换日期 → 序号重置0；同一天序号+1
if($oldYYMM -ne $currentYYMM -or $oldDD -ne $dd){
    $bn = 0
}else{
    $bn = $oldBN + 1
    if($bn -gt 99){ $bn = 0 } # 十进制两位，最大99
}
# 十进制拼接：DD*100 + BN
$newDDBn = $dd * 100 + $bn

# 版本字符串固定 4 位：DD(2)+BN(2)，例如 2 日第 0 次构建 → "0200"
# 不能直接用 $newDDBn，否则 0201 会变成 201，文件名少一位日期。
$ddBnStr = '{0:0000}' -f $newDDBn

# 产物 hex 命名：DDF<yy><mm><dd><bn:00>  —— 与 FW 版本 yymm.ddbn 一一对应
# 例：2026-10-26 第 0 次构建 → DDF26102600.hex
$hexName = "DDF$yy$mm$ddBnStr.hex"

# 把产物名写到 build/.fw_hexname.txt（build 目录常驻，不随 build/Debug 清理；
# 构建中间产物，不进入固件 code 段，固件源码不引用本文件），
# 供 build.ps1 / eide 后处理 rename_hex.ps1 / verify_hex.ps1 读取。
$buildDir = Join-Path $PSScriptRoot "build"
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Force -Path $buildDir | Out-Null }
[IO.File]::WriteAllText("$buildDir/.fw_hexname.txt", $hexName, [Text.Encoding]::ASCII)

$out = @"
// Auto Generated, DO NOT EDIT!
#ifndef __FW_VER_H__
#define __FW_VER_H__

#define FW_YYMM    $($currentYYMM)U
#define FW_DD_BN   $($newDDBn)U
/* String copy of the two numeric macros. #x would keep the U suffix and cannot
   strip it, so the script emits plain string literals directly; they feed the
   boot line "DDFW:<yymm>.<ddbn>" static tag used by the host to read the package
   version from the hex image without a live bus query. */
#define FW_YYMM_STR "$($currentYYMM)"
#define FW_DD_BN_STR "$ddBnStr"

#endif
"@
$out | Out-File $headerPath -Encoding utf8
Write-Host "✅ FW_VER: YYMM=$currentYYMM , DD_BN=$newDDBn (DD=$dd, BuildNo=$bn), HEX=$hexName"

