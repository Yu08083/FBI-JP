import ctypes
import os
import random
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
    library.text_index_lines.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t]
    library.text_index_lines.restype = ctypes.c_size_t
    library.text_line_span.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    library.text_line_span.restype = ctypes.c_bool
    library.text_replace_line.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_char_p]
    library.text_replace_line.restype = ctypes.c_bool
    library.text_insert_line.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_bool]
    library.text_insert_line.restype = ctypes.c_bool
    library.text_delete_line.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t]
    library.text_delete_line.restype = ctypes.c_bool
    library.text_find_line.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    library.text_find_line.restype = ctypes.c_bool
    rng = random.Random(20261024)
    document = "先頭\n設定=true\n日本語\n\n末尾\n"
    for iteration in range(400):
        lines = document.split("\n")
        encoded = document.encode()
        offsets = (ctypes.c_size_t * (len(lines) + 1))()
        assert library.text_index_lines(encoded, offsets, len(lines)) == len(lines)
        expected_offsets = [0]
        for line in lines[:-1]:
            expected_offsets.append(expected_offsets[-1] + len(line.encode()) + 1)
        assert list(offsets[:len(lines)]) == expected_offsets
        selected = rng.randrange(len(lines))
        start, length = ctypes.c_size_t(), ctypes.c_size_t()
        assert library.text_line_span(encoded, selected, ctypes.byref(start), ctypes.byref(length))
        assert start.value == expected_offsets[selected] and length.value == len(lines[selected].encode())
        buffer = ctypes.create_string_buffer(32770)
        buffer.value = encoded
        buffer[32769] = b"Q"
        operation = rng.randrange(3)
        if operation == 0:
            replacement = rng.choice(("", "設定", "value=123", "one\ntwo", "😀"))
            assert library.text_replace_line(buffer, 32769, selected, replacement.encode())
            lines[selected] = replacement
        elif operation == 1:
            after = bool(rng.randrange(2))
            assert library.text_insert_line(buffer, 32769, selected, after)
            lines.insert(selected + int(after), "")
        else:
            assert library.text_delete_line(buffer, 32769, selected)
            del lines[selected]
        document = "\n".join(lines)
        assert buffer.value.decode() == document
        assert buffer.raw[32769:32770] == b"Q"
        query = rng.choice(("設定", "value", "😀", "missing", ""))
        start_line = rng.randrange(len(document.split("\n")) + 2)
        search_offsets = [0] + [i + 1 for i, value in enumerate(document.encode()) if value == 10]
        search_start = search_offsets[start_line] if start_line < len(search_offsets) else 0
        match = document.encode().find(query.encode(), search_start) if query else -1
        if match < 0 and query:
            match = document.encode().find(query.encode())
        found = ctypes.c_size_t()
        assert library.text_find_line(document.encode(), query.encode(), start_line, ctypes.byref(found)) == (match >= 0)
        if match >= 0:
            assert found.value == document.encode()[:match].count(b"\n")
        checks += 1
    buffer = ctypes.create_string_buffer(b"x" * 32768, 32770)
    assert not library.text_insert_line(buffer, 32769, 0, True)
    assert buffer.value == b"x" * 32768
    assert not library.text_replace_line(buffer, 32769, 0, b"\xED\xA0\x80")
    assert buffer.value == b"x" * 32768
    assert not library.text_delete_line(buffer, 32769, 1)
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print(f"Validated {checks + 2} text cases, including Japanese, BOM, line endings and size limits.")
