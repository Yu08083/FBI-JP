import ctypes
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]


class Format(ctypes.Structure):
    _fields_ = [("bom", ctypes.c_bool), ("newline", ctypes.c_int)]


with tempfile.TemporaryDirectory(prefix="fbi-text-check-") as temporary:
    library_path = Path(temporary) / ("text.dll" if os.name == "nt" else "text.so")
    arguments = [shutil.which("gcc"), "-std=gnu11", "-shared", "-O2", str(root / "source/core/text.c"), "-o", str(library_path)]
    if os.name != "nt":
        arguments.append("-fPIC")
    subprocess.run(arguments, check=True)
    library = ctypes.CDLL(str(library_path))
    library.text_is_txt.argtypes = [ctypes.c_char_p]
    library.text_is_txt.restype = ctypes.c_bool
    library.text_decode.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Format)]
    library.text_decode.restype = ctypes.c_bool
    library.text_encode.argtypes = [ctypes.c_char_p, Format, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    library.text_encode.restype = ctypes.c_bool
    for name in (b"notes.txt", b"NOTES.TXT", b"mixed.TxT", "日本語.txt".encode(), b".txt"):
        assert library.text_is_txt(name)
    for name in (None, b"", b"text", b"text.txt.bak", b"text.txt/", b"text.cia"):
        assert not library.text_is_txt(name)
    checks = 11
    for sample in ("", "a", "日本語とASCII 😀\n次の行\n", "\n\n", "a\tb\n", "x" * 32768):
        for newline, separator in enumerate(("\n", "\r\n", "\r")):
            for bom in (False, True):
                payload = (b"\xEF\xBB\xBF" if bom else b"") + sample.replace("\n", separator).encode()
                if len(payload) > 32768:
                    continue
                output = ctypes.create_string_buffer(32769)
                fmt = Format()
                assert library.text_decode(payload, len(payload), output, len(output), ctypes.byref(fmt))
                assert output.value.decode() == sample
                assert fmt.bom == bom
                if "\n" in sample:
                    assert fmt.newline == newline
                encoded = ctypes.create_string_buffer(32770)
                encoded[32768] = b"Q"
                size = ctypes.c_size_t()
                assert library.text_encode(output.value, fmt, encoded, 32768, ctypes.byref(size))
                assert encoded.raw[:size.value] == payload
                assert encoded.raw[32768:32769] == b"Q"
                checks += 1
    for payload in (b"\0", b"x\0y", b"\xC0\x80", b"\xED\xA0\x80", b"\xF4\x90\x80\x80", b"\xE3\x81", b"\x80", "日本語".encode("shift_jis"), b"x" * 32769):
        output = ctypes.create_string_buffer(32769)
        assert not library.text_decode(payload, len(payload), output, len(output), ctypes.byref(Format()))
        checks += 1
    output = ctypes.create_string_buffer(8)
    for sample, fmt in ((b"x" * 32769, Format(False, 0)), (b"\n" * 16385, Format(False, 1)), (b"x" * 32768, Format(True, 0))):
        storage = ctypes.create_string_buffer(32770)
        storage[32768] = b"Q"
        assert not library.text_encode(sample, fmt, storage, 32768, ctypes.byref(ctypes.c_size_t()))
        assert storage.raw[32768:32769] == b"Q"
        checks += 1
    assert not library.text_decode(b"abc", 3, output, 3, ctypes.byref(Format()))
    assert library.text_decode(b"a\r\nb\nc\rd", 8, output, 8, ctypes.byref(Format()))
    assert output.value == b"a\nb\nc\nd"
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print(f"Validated {checks + 2} text cases, including Japanese, BOM, line endings and size limits.")
