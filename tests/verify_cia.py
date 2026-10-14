import hashlib
import json
import struct
import sys
from pathlib import Path


def aligned(value):
    return (value + 63) & ~63


def signed_body(data):
    signature_type = struct.unpack_from(">I", data)[0]
    sizes = {
        0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
        0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80,
    }
    return data[sizes[signature_type]:]


def verify(path):
    data = path.read_bytes()
    header_size, _, _, certificate_size, ticket_size, tmd_size, _, content_size = struct.unpack_from("<IHHIIIIQ", data)
    ticket_offset = aligned(header_size) + aligned(certificate_size)
    tmd_offset = ticket_offset + aligned(ticket_size)
    content_offset = tmd_offset + aligned(tmd_size)
    ticket = signed_body(data[ticket_offset:ticket_offset + ticket_size])
    tmd = signed_body(data[tmd_offset:tmd_offset + tmd_size])
    ticket_id = struct.unpack_from(">Q", ticket, 0x9C)[0]
    tmd_id = struct.unpack_from(">Q", tmd, 0x4C)[0]
    ticket_version = struct.unpack_from(">H", ticket, 0xA6)[0]
    tmd_version = struct.unpack_from(">H", tmd, 0x9C)[0]
    expected_id = 0x000400000F800100
    expected_version = (2 << 10) | (6 << 4) | 1
    if ticket_id != expected_id or tmd_id != expected_id:
        raise ValueError(f"Incorrect title ID: ticket={ticket_id:016X}, TMD={tmd_id:016X}")
    if ticket_version != expected_version or tmd_version != expected_version:
        raise ValueError(f"Incorrect CIA version: ticket={ticket_version}, TMD={tmd_version}, expected={expected_version} (2.6.1)")
    content_count = struct.unpack_from(">H", tmd, 0x9E)[0]
    if content_count != 1:
        raise ValueError(f"Unexpected content count: {content_count}")
    record = tmd[0x9C4:0x9F4]
    _, index, flags, recorded_size = struct.unpack_from(">IHHQ", record)
    if index != 0 or flags & 1 or recorded_size != content_size:
        raise ValueError("Unexpected CIA content layout")
    content = data[content_offset:content_offset + content_size]
    if hashlib.sha256(content).digest() != record[16:48]:
        raise ValueError("CIA content hash does not match the TMD")
    if content[0x100:0x104] != b"NCCH":
        raise ValueError("Missing NCCH content")
    if struct.unpack_from("<Q", content, 0x118)[0] != expected_id:
        raise ValueError("NCCH program ID does not match the title ID")
    return {"title_id": f"{expected_id:016X}", "ticket_version": ticket_version, "tmd_version": tmd_version, "version": "2.6.1", "content_hash": "valid"}


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1]))))
