# -*- coding: utf-8 -*-
"""M997 -> M788: 飞达 ISP 复位命令统一编号(避开 ESP32 本地 M9xx 段)。"""
import io, glob, os

DOCS = r"C:\Users\Administrator\Doubao\chats\2026-10-02\new-chat\smt_feeder_controller\docs"
DATA = r"C:\Users\Administrator\Doubao\chats\2026-10-02\new-chat\smt_feeder_controller\data"

total = 0
for root in (DOCS, DATA):
    for p in glob.glob(os.path.join(root, "**", "*.md"), recursive=True) + \
             glob.glob(os.path.join(root, "**", "*.html"), recursive=True) + \
             glob.glob(os.path.join(root, "**", "*.js"), recursive=True):
        with io.open(p, 'r', encoding='utf-8') as f:
            t = f.read()
        n = t.count("M997")
        if n:
            t = t.replace("M997", "M788")
        n2 = t.count("| 997  |")
        if n2:
            t = t.replace("| 997  |", "| 788  |")
        if n or n2:
            with io.open(p, 'w', encoding='utf-8', newline='') as f:
                f.write(t)
            total += n + n2
            print('%s: M997 x%d, table 997 x%d' % (os.path.relpath(p), n, n2))
print('total replaced:', total)
