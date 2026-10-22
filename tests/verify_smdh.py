import struct
import sys
from pathlib import Path

from verify_cia import aligned


def verify(data):
    assert len(data) == 0x36C0 and data[:4] == b"SMDH"
    for language in range(16):
        base = 8 + language * 0x200
        assert data[base:base + 0x80].decode("utf-16le").rstrip("\0") == "FBI 日本語版"
        assert data[base + 0x80:base + 0x180].decode("utf-16le").rstrip("\0") == "FBI 日本語版 / Original: Steveice10"
        assert data[base + 0x180:base + 0x200].decode("utf-16le").rstrip("\0") == "yu_"


cia = Path(sys.argv[1]).read_bytes()
header, _, _, certificates, ticket, tmd, _, _ = struct.unpack_from("<IHHIIIIQ", cia)
content = cia[aligned(header) + aligned(certificates) + aligned(ticket) + aligned(tmd):]
exefs_offset = struct.unpack_from("<I", content, 0x1A0)[0] * 0x200
exefs = content[exefs_offset:]
for entry in range(10):
    name, offset, size = struct.unpack_from("<8sII", exefs, entry * 16)
    if name.rstrip(b"\0") == b"icon":
        verify(exefs[0x200 + offset:0x200 + offset + size])
        break
else:
    raise ValueError("CIA icon missing")
three_dsx = Path(sys.argv[2]).read_bytes()
assert three_dsx[:4] == b"3DSX"
smdh_offset, smdh_size = struct.unpack_from("<II", three_dsx, 0x20)
verify(three_dsx[smdh_offset:smdh_offset + smdh_size])
verify(Path(sys.argv[3]).read_bytes())
print("Validated FBI Japanese name and yu_ credits in CIA, 3DSX and SMDH for all 16 languages.")
