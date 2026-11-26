# -*- coding: utf-8 -*-
"""提取界面用到的全部汉字，生成可粘贴进 TJC 字库「指定字符」的清单。

用法:
    python tools/提取字库字符.py            # 打印 + 写入 docs/字库汉字清单.txt
    python tools/提取字库字符.py --check    # 只检查是否有新增未覆盖的字

数据来源:
    data/lang/zh.txt           界面文案（全部中文词条）
    data/lang/en.txt           忽略
    生成器里的字面量            页面写死的文字（如 "问题飞达 1/2"）
"""
import os, re, sys

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZH   = os.path.join(BASE, "data", "lang", "zh.txt")
OUT  = os.path.join(BASE, "docs", "字库汉字清单.txt")
GEN  = [os.path.join(BASE, "tools", "gen_screen.py")]

def collect():
    texts = []
    if os.path.exists(ZH):
        for line in open(ZH, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                texts.append(line.split("=", 1)[1])
    for g in GEN:
        if os.path.exists(g):
            src = open(g, encoding="utf-8").read()
            texts += re.findall(r'"([^"\n]*[\u4e00-\u9fff][^"\n]*)"', src)
    chars = set()
    for t in texts:
        for ch in t:
            if "\u4e00" <= ch <= "\u9fff":
                chars.add(ch)
    return "".join(sorted(chars))

def old_set():
    if not os.path.exists(OUT):
        return set()
    for line in open(OUT, encoding="utf-8"):
        if len(line.strip()) > 20:
            return set(line.strip())
    return set()

def main():
    s = collect()
    if "--check" in sys.argv:
        new = set(s) - old_set()
        if new:
            print("新增未覆盖的字（%d 个）: %s" % (len(new), "".join(sorted(new))))
            sys.exit(1)
        print("OK: 全部 %d 个字都在清单里" % len(s))
        return
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("# 界面用到的全部汉字（%d 个）\n" % len(s))
        f.write("# 建字库时: 字符集选「指定字符」, 粘贴下面两行, 再勾上 ASCII\n\n")
        for i in range(0, len(s), 60):
            f.write(s[i:i+60] + "\n")
    print("汉字 %d 个 -> %s" % (len(s), OUT))

if __name__ == "__main__":
    main()
