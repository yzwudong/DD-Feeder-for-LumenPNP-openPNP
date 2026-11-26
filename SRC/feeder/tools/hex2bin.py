# hex2bin.py - minimal Intel HEX -> binary converter (stdlib only)
# Usage: python hex2bin.py <input.hex> <output.bin>
# No third-party dependencies. Missing addresses are padded with 0xFF.

import sys


def hex2bin(hex_path, bin_path):
    out = bytearray()
    base = 0
    with open(hex_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            try:
                count = int(line[1:3], 16)
                addr = int(line[3:7], 16)
                rtype = int(line[7:9], 16)
                data = bytes(int(line[i:i + 2], 16)
                             for i in range(9, 9 + count * 2, 2))
            except ValueError:
                continue
            if rtype == 0:      # data record
                offset = base + addr
                need = offset + count - len(out)
                if need > 0:
                    out.extend(b'\xff' * need)
                out[offset:offset + count] = data
            elif rtype == 2:    # extended segment address
                base = ((data[0] << 8) | data[1]) << 4
            elif rtype == 4:    # extended linear address
                base = ((data[0] << 8) | data[1]) << 16
            elif rtype == 1:    # end of file
                break
    with open(bin_path, 'wb') as f:
        f.write(out)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.stderr.write('usage: python hex2bin.py <input.hex> <output.bin>\n')
        sys.exit(1)
    hex2bin(sys.argv[1], sys.argv[2])
