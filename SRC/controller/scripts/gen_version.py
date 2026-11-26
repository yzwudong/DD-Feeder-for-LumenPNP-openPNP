# -*- coding: utf-8 -*-
"""pre-build 脚本: 生成 include/version.h (日期 + 当日2位序号)
只读取当前序号生成版本号, 不递增 —— 递增由 post 脚本 bump_seq.py 负责,
保证 version.h 与"本次实际编译进固件的版本"一致, 增量编译无变化时不跳号。
版本格式: YYYYMMDD + 2位当日编译序号 (00~99, 跨天重置, 用尽报错)
完整名:  DDfeeder<ver>    例: DDfeeder2026101007
计数文件: 工程根 .build_seq (内容: "YYYYMMDD NN")
"""
try:
    Import("env")
except Exception:
    env = None

import datetime
import os

try:
    PROJ = env.subst("$PROJECT_DIR")
except Exception:
    PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREFIX = "DDfeeder"

today = datetime.date.today().strftime("%Y%m%d")
seqfile = os.path.join(PROJ, ".build_seq")

seq = 0
try:
    with open(seqfile, "r") as f:
        d, s = f.read().strip().split()
        if d == today:
            seq = int(s)
except Exception:
    pass

# 约定: 序号恒为 2 位 (00~99)。99 之后不回绕、不突破位数 —— 用尽直接报错中止编译
# (避免版本倒退或格式漂移; 跨天自动重置)
if seq > 99:
    print("[VER] 错误: 当天编译序号已用尽 (当前 seq=%d, 上限 99)。请明天再编译。" % seq)
    raise SystemExit(1)

ver = "%s%02d" % (today, seq)

vh = os.path.join(PROJ, "include", "version.h")
with open(vh, "w") as f:
    f.write("#pragma once\n")
    f.write("/* 自动生成: scripts/gen_version.py — 勿手改 */\n")
    f.write('#define FW_VER_DATE "%s"\n' % today)
    f.write('#define FW_VER_SEQ  "%02d"\n' % seq)
    f.write('#define FW_VER_STR  "%s"\n' % ver)              # 纯版本号: 2026101007
    f.write('#define FW_VER_FULL "%s%s"\n' % (PREFIX, ver))  # 文件前缀名: DDfeeder2026101007

print("[VER] %s%s (seq=%02d)" % (PREFIX, ver, seq))
