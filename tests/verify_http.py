import ctypes
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
http = (root / "source/core/http.c").read_text(encoding="utf-8")
start = http.index("typedef struct {\n    void* buf;\n    size_t size;")
end = http.index("static Result FSUSER_AddSeed(")
update = (root / "source/fbi/update.c").read_text(encoding="utf-8")
limit = re.search(r'http_download_json\([^,]+, &json, (\d+) \* 1024\)', update)
capacity = int(limit[1]) * 1024
host_source = r"""
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;
#define R_SUCCEEDED(result) ((result) >= 0)
#define R_APP_INVALID_ARGUMENT -1
#define R_APP_OUT_OF_RANGE -2
#define R_APP_PARSE_FAILED -3
#define R_APP_OUT_OF_MEMORY -4
static const char* payload;
static size_t payload_size;
static size_t chunk_size;
static Result transport_result;
Result http_download_callback(const char* url, u32 bufferSize, void* userData,
        Result (*callback)(void*, void*, size_t), Result (*checkRunning)(void*),
        Result (*progress)(void*, u64, u64)) {
    if(transport_result < 0) return transport_result;
    for(size_t pos = 0; pos < payload_size;) {
        size_t length = payload_size - pos;
        if(length > chunk_size) length = chunk_size;
        if(length > bufferSize) length = bufferSize;
        Result result = callback(userData, (void*) (payload + pos), length);
        if(result < 0) return result;
        pos += length;
    }
    return 0;
}
""" + http[start:end] + r"""
Result parse_payload(const char* bytes, size_t length, size_t capacity, size_t chunk, Result transport) {
    payload = bytes;
    payload_size = length;
    chunk_size = chunk;
    transport_result = transport;
    json_t* parsed = NULL;
    Result result = http_download_json("https://example.com/release", &parsed, capacity);
    if(result == 0) {
        json_error_t error;
        json_t* expected = json_loadb(bytes, length, 0, &error);
        if(!json_equal(parsed, expected)) result = -5;
        json_decref(expected);
        json_decref(parsed);
    } else if(parsed != NULL) {
        result = -6;
    }
    return result;
}
Result read_seed(const char* bytes, size_t length, size_t chunk, u32* downloaded) {
    payload = bytes;
    payload_size = length;
    chunk_size = chunk;
    transport_result = 0;
    u8 output[18];
    memset(output, 0xA5, sizeof(output));
    Result result = http_download_buffer("https://example.com/seed", downloaded, output + 1, 16);
    if(output[0] != 0xA5 || output[17] != 0xA5) return -7;
    if(result == 0 && memcmp(output + 1, bytes, length) != 0) return -8;
    return result;
}
"""

compiler = shutil.which("gcc")
with tempfile.TemporaryDirectory(prefix="fbi-http-check-") as temporary:
    temporary = Path(temporary)
    source = temporary / "http.c"
    library_path = temporary / ("http.dll" if os.name == "nt" else "http.so")
    source.write_text(host_source, encoding="utf-8")
    arguments = [compiler, "-std=gnu11", "-shared", "-O2", str(source), "-o", str(library_path), "-ljansson"]
    if os.name != "nt":
        arguments.append("-fPIC")
    subprocess.run(arguments, check=True)
    library = ctypes.CDLL(str(library_path))
    library.parse_payload.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_int32]
    library.parse_payload.restype = ctypes.c_int32
    release = {
        "tag_name": "2.6.1-ja.3",
        "assets": [{"name": name, "browser_download_url": f"https://github.com/Yu08083/FBI-JP/releases/download/2.6.1-ja.3/{name}"} for name in ("FBI-JP.cia", "FBI-JP.3dsx")],
        "body": "更新確認の修正版。" * 1000,
    }
    encoded = json.dumps(release, ensure_ascii=False).encode("utf-8")
    assert 16 * 1024 < len(encoded) < capacity
    checks = 0
    for chunk in (1, 31, 4096, capacity):
        for payload in (encoded, b"{}" + b" " * (capacity - 2)):
            assert library.parse_payload(payload, len(payload), capacity, chunk, 0) == 0
            checks += 1
        for payload in (encoded, b"{}" + b" " * (capacity - 1)):
            smaller = 16 * 1024 if payload == encoded else capacity
            assert library.parse_payload(payload, len(payload), smaller, chunk, 0) == -2
            checks += 1
        for payload in (b"", b'{"tag_name":', b"{}\x00garbage"):
            assert library.parse_payload(payload, len(payload), capacity, chunk, 0) == -3
            checks += 1
    assert library.parse_payload(encoded, len(encoded), capacity, 4096, -9) == -9
    assert library.parse_payload(encoded, len(encoded), 0, 4096, 0) == -1
    library.read_seed.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint32)]
    library.read_seed.restype = ctypes.c_int32
    for length in (0, 15, 16, 17, 32):
        for chunk in (1, 7, 32):
            downloaded = ctypes.c_uint32(999)
            payload = bytes(range(length))
            result = library.read_seed(payload, length, chunk, ctypes.byref(downloaded))
            assert result == (0 if length <= 16 else -2)
            assert downloaded.value == (length if result == 0 else 999)
            checks += 1
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print(f"Validated {checks + 2} HTTP and JSON cases, including releases larger than 16 KiB.")
