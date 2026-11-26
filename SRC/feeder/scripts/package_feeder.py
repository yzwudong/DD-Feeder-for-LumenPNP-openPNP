#!/usr/bin/env python3
"""飞达发布包：生成
  build/release/DDF_<yymmddnn>_BL.hex
  build/release/DDF_<yymmddnn>_APP.hex
  build/release/DDF_<yymmddnn>_FULL.hex

前提：
  - Bootloader 已用 bootloader/build.ps1 生成 build/DDF_<ver>_BL.hex
  - App 已用 EIDE 编译，输出 build/Debug/ddfeedermcu.hex
  - App 链接 0x1800/0x5200，Boot 0x0000/0x1800
"""
import os
import re
import shutil
import sys

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJ, "tools"))
import merge_hex  # noqa: E402

def read_ver():
    p = os.path.join(PROJ, "src", "version.h")
    s = open(p, encoding="utf-8").read()
    yymm = re.search(r'FW_YYMM_STR\s+"(\d+)"', s)
    ddbn = re.search(r'FW_DD_BN_STR\s+"(\d+)"', s)
    if not yymm or not ddbn:
        raise SystemExit("version.h 缺少 FW_YYMM_STR/FW_DD_BN_STR")
    ver = yymm.group(1) + ddbn.group(1)
    if len(ver) != 8 or not ver.isdigit():
        raise SystemExit("飞达版本必须为 8 位 yymmddnn（日期 2 位 + 序号 2 位），实际为 %r" % ver)
    return ver

def main():
    ver = read_ver()
    out = os.path.join(PROJ, "build", "release")
    os.makedirs(out, exist_ok=True)

    bl = os.path.join(PROJ, "build", "DDF_%s_BL.hex" % ver)
    if not os.path.exists(bl):
        bl = os.path.join(PROJ, "bootloader", "build", "DDF_%s_BL.hex" % ver)
    app = os.path.join(PROJ, "build", "Debug", "ddfeedermcu.hex")
    if not os.path.exists(bl):
        raise SystemExit("缺少 %s，先运行 bootloader/build.ps1" % bl)
    if not os.path.exists(app):
        raise SystemExit("缺少 %s，先 EIDE 编译 App" % app)

    bl_d = merge_hex.parse(bl)
    app_d = merge_hex.parse(app)
    if min(bl_d) < 0 or max(bl_d) >= 0x1800:
        raise SystemExit("BL 超出 0x0000-0x17FF")
    if min(app_d) < 0x1800 or max(app_d) > 0x69FF:
        raise SystemExit("APP 不在 0x1800-0x69FF，先用 EIDE 以 0x1800/0x5200 重链")

    bl_out = os.path.join(out, "DDF_%s_BL.hex" % ver)
    app_out = os.path.join(out, "DDF_%s_APP.hex" % ver)
    full_out = os.path.join(out, "DDF_%s_FULL.hex" % ver)
    shutil.copyfile(bl, bl_out)
    shutil.copyfile(app, app_out)

    d = dict(bl_d)
    d.update(app_d)
    # FULL 包在 0x6A00 写 0：清 IAP flag（不擦 0x6C00 配置/0x6E00 ChipID）
    for i in range(0x6A00, 0x6A08):
        d[i] = 0
    merge_hex.emit(d, full_out)

    raw = bytes(d.get(i, 0xFF) for i in range(min(d), max(d) + 1))
    tag = "%s.%s" % (ver[:4], ver[4:])
    if b"DDFBL:" + tag.encode() not in raw:
        raise SystemExit("BL 缺少 DDFBL:%s 版本标志" % tag)
    if b"DDFW:" + tag.encode() not in raw:
        raise SystemExit("APP 缺少 DDFW:%s 版本标志" % tag)

    print("OK ver=%s" % ver)
    for f in (bl_out, app_out, full_out):
        print(" ", os.path.relpath(f, PROJ), os.path.getsize(f))

if __name__ == "__main__":
    main()



