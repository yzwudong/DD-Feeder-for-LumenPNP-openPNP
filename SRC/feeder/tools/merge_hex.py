#!/usr/bin/env python3
"""合并两个 Intel HEX，按绝对地址去重；用于 Boot(0x0000) + App(0x1000)。"""
import sys

def parse(path):
    data = {}
    base = 0
    with open(path, "r", encoding="ascii", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            if len(raw) < 5:
                continue
            n = raw[0]
            addr = (raw[1] << 8) | raw[2]
            typ = raw[3]
            payload = raw[4:4 + n]
            if (sum(raw) & 0xFF) != 0:
                raise ValueError("checksum error: " + line)
            if typ == 0x00:
                for i, b in enumerate(payload):
                    data[base + addr + i] = b
            elif typ == 0x01:
                break
            elif typ == 0x04:
                base = ((payload[0] << 8) | payload[1]) << 16
    return data

def rec(addr, typ, payload):
    raw = [len(payload), (addr >> 8) & 0xFF, addr & 0xFF, typ] + list(payload)
    chk = (-sum(raw)) & 0xFF
    return ":" + "".join(f"{b:02X}" for b in raw + [chk])

def emit(data, path):
    out = []
    high = None
    addrs = sorted(data)
    i = 0
    while i < len(addrs):
        a = addrs[i]
        if a > 0xFFFF:
            raise ValueError("address > 0xFFFF not supported")
        block = []
        start = a
        while i < len(addrs) and addrs[i] == a + len(block) and len(block) < 16:
            block.append(data[addrs[i]])
            i += 1
        h = a >> 16
        if h != high:
            out.append(rec(0, 0x04, [(h >> 8) & 0xFF, h & 0xFF]))
            high = h
        out.append(rec(a & 0xFFFF, 0x00, block))
    out.append(":00000001FF")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out) + "\n")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("usage: merge_hex.py boot.hex app.hex combined.hex", file=sys.stderr)
        sys.exit(2)
    d = parse(sys.argv[1])
    d.update(parse(sys.argv[2]))
    emit(d, sys.argv[3])
    print(f"merged {len(d)} bytes -> {sys.argv[3]}")
