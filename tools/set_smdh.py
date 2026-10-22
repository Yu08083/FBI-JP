import sys
from pathlib import Path


def set_titles(path, name, description, author):
    data = bytearray(path.read_bytes())
    if len(data) != 0x36C0 or data[:4] != b"SMDH":
        raise ValueError("Invalid SMDH")
    fields = ((0, 0x80, name), (0x80, 0x100, description), (0x180, 0x80, author))
    for language in range(16):
        base = 8 + language * 0x200
        for offset, size, text in fields:
            encoded = text.encode("utf-16le")
            if len(encoded) + 2 > size:
                raise ValueError("SMDH text too long")
            data[base + offset:base + offset + size] = encoded.ljust(size, b"\0")
    path.write_bytes(data)


if __name__ == "__main__":
    set_titles(Path(sys.argv[1]), *sys.argv[2:5])
