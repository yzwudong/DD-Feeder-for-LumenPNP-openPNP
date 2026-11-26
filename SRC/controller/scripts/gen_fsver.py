# -*- coding: utf-8 -*-
"""pre-build 脚本: 生成页面(FS)版本号 -> data/fsver.txt

版本号 = YYYYMMDD + data/ 全部文件内容 MD5 前 8 位 (纯数字/哈希, 不含前缀)
例:   20261010a3f2c1d0
打包产物文件名 = DDfs + 版本号 (package_bins.py 拼前缀, 仅用于文件类型区分)
特性: 页面内容不变 → 哈希不变 → fsver.txt 内容不变 → buildfs 不重复构建;
      页面有改动 → 版本自动变 → 可据此判断"线上页面 vs 设备页面"是否一致。
"""
try:
    Import("env")
except Exception:
    env = None

import datetime
import hashlib
import os

try:
    PROJ = env.subst("$PROJECT_DIR")
except Exception:
    PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DATA = os.path.join(PROJ, "data")

h = hashlib.md5()
paths = []
for root, _, names in os.walk(DATA):
    for n in sorted(names):
        if n == "fsver.txt":
            continue              # 版本文件自身不参与哈希
        paths.append(os.path.join(root, n))
for p in sorted(paths):
    try:
        with open(p, "rb") as f:
            h.update(f.read())
    except Exception:
        pass

fsver = datetime.date.today().strftime("%Y%m%d") + h.hexdigest()[:8]   # 版本号本身不含 DDfs 前缀

vf = os.path.join(DATA, "fsver.txt")
try:
    old = open(vf, "r").read().strip()
except Exception:
    old = ""
if old != fsver:
    with open(vf, "w") as f:
        f.write(fsver)
    print("[FSVER] %s (页面版本已更新)" % fsver)
else:
    print("[FSVER] %s (无变化)" % fsver)
