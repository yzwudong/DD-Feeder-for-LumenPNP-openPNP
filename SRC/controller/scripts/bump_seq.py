# -*- coding: utf-8 -*-
"""post-build 脚本: 编译成功后递增 .build_seq 序号 (供下次 pre 生成新版本)
只增序号, 不改 version.h —— 版本号在编译时(gen_version.py)确定, 与固件内容严格一致。
"""
try:
    Import("env")
except Exception:
    env = None

import datetime
import os
import time

try:
    PROJ = env.subst("$PROJECT_DIR")
except Exception:
    PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

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



seq = seq + 1
with open(seqfile, "w") as f:
    f.write("%s %02d\n" % (today, seq))

print("[SEQ] next=%02d" % seq)
