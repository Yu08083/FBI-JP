import ast
import ctypes
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
translations = json.loads((root / "translations/ja.json").read_text(encoding="utf-8"))
formats = re.compile(r"%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn%]")
for original, japanese in translations.items():
    assert formats.findall(original) == formats.findall(japanese), original
    japanese.encode("utf-8").decode("utf-8")

tokens = re.compile(r'//(?:\\\n|[^\n])*|/\*[\s\S]*?\*/|\'(?:[^\'\\]|\\[\s\S])*\'|"(?:[^"\\]|\\[\s\S])*"')
def structure(text):
    result = re.sub(r"\s+", "", tokens.sub(lambda m: '""' if m.group().startswith('"') else "" if m.group().startswith(("//", "/*")) else m.group(), text))
    for suffix in ("Begin", "Finish", "Abort"):
        result = result.replace(f"AMNET_InstallTicket{suffix}", f"AM_InstallTicket{suffix}")
    for current, original in {
        "AM_GetNumImportTitleContexts": "AM_GetPendingTitleCount",
        "AM_GetImportTitleContextList": "AM_GetPendingTitleList",
        "AM_PENDING_NON_SYSTEM": "AM_STATUS_MASK_INSTALLING",
        "AM_PENDING_SYSTEM": "AM_STATUS_MASK_AWAITING_FINALIZATION",
    }.items():
        result = result.replace(current, original)
    return result

changed_logic = {
    "source/core/http.c",
    "source/core/screen.c", "source/core/stringutil.c", "source/core/stringutil.h",
    "source/core/ui/prompt.c", "source/core/ui/ui.c", "source/fbi/update.c",
    "source/fbi/task/uitask.c",
    "source/core/text.c", "source/core/text.h", "source/fbi/files.c",
    "source/fbi/action/action.h", "source/fbi/action/edittext.c", "source/fbi/action/installurl.c",
}
for path in (root / "source").rglob("*"):
    if path.suffix not in (".c", ".h"):
        continue
    current_text = path.read_text(encoding="utf-8")
    assert not any(token.group().startswith(("//", "/*")) for token in tokens.finditer(current_text)), path
    relative = path.relative_to(root).as_posix()
    if relative in changed_logic:
        continue
    original = subprocess.check_output(["git", "show", f"2.6.1:{relative}"], cwd=root).decode("utf-8")
    assert structure(original) == structure(current_text), relative
    if "libs" in path.parts:
        assert [m.group() for m in tokens.finditer(original) if m.group().startswith('"')] == [m.group() for m in tokens.finditer(current_text) if m.group().startswith('"')], relative

for path in (root / "servefiles").rglob("*.py"):
    relative = path.relative_to(root).as_posix()
    original = subprocess.check_output(["git", "show", f"2.6.1:{relative}"], cwd=root).decode("utf-8")
    assert ast.dump(ast.parse(original)) == ast.dump(ast.parse(path.read_text(encoding="utf-8"))), relative

shader = root / "source/core/default.v.pica"
assert ";" not in shader.read_text(encoding="utf-8")
assert "Copyright (c) 2017 Sean Barrett" in (root / "LICENSES/stb_image.txt").read_text(encoding="utf-8")

license_text = (root / "LICENSE.txt").read_text(encoding="utf-8")
original_license = subprocess.check_output(["git", "show", "2.6.1:LICENSE.txt"], cwd=root).decode("utf-8").replace("\r\n", "\n")
assert license_text.replace("Copyright (C) 2026 yu_ (Japanese localization and associated changes)\n", "") == original_license
makefile = (root / "Makefile").read_text(encoding="utf-8")
assert "AUTHOR := yu_" in makefile
assert "UNIQUE_ID := 0xF8001" in makefile
assert "VERSION_MICRO := 1" in makefile
update = (root / "source/fbi/update.c").read_text(encoding="utf-8")
assert "repos/Yu08083/FBI-JP/releases/latest" in update
assert 'json_object_get(json, "tag_name")' in update
assert 'strcmp(json_string_value(assetName)' in update

screen = (root / "source/core/screen.c").read_text(encoding="utf-8")
wrap = screen[screen.index("#define MAX_LINES"):screen.index("void screen_get_string_size(")]
strings = (root / "source/core/stringutil.c").read_text(encoding="utf-8")
copy = strings[strings.index("void string_copy_utf8("):strings.index("void string_get_file_name(")]
host_source = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
typedef uint8_t u8;
typedef uint32_t u32;
static float font_scale = 1.0f;
typedef struct { float lineFeed; } FontInfo;
typedef struct { float charWidth; } CharWidth;
static FontInfo font_info = {30};
static FontInfo* fontGetInfo(void* font) { return &font_info; }
static u32 fontGlyphIndexFromCodePoint(void* font, u32 code) { return code; }
static CharWidth* fontGetCharWidthInfo(void* font, u32 code) {
    static CharWidth width;
    width.charWidth = code >= 0x2E80 ? 30 : 16;
    return &width;
}
static ssize_t decode_utf8(u32* out, const uint8_t* p) {
    if(p[0] < 0x80) { *out = p[0]; return 1; }
    if(p[0] < 0xE0) { *out = ((p[0] & 31) << 6) | (p[1] & 63); return 2; }
    if(p[0] < 0xF0) { *out = ((p[0] & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); return 3; }
    *out = ((p[0] & 7) << 18) | ((p[1] & 63) << 12) | ((p[2] & 63) << 6) | (p[3] & 63);
    return 4;
}
""" + wrap + copy + r"""
u32 measure(const char* text, float limit, float scale, float* width, float* height, u32* lines) {
    u32 count = 0;
    screen_wrap_string(lines, NULL, NULL, &count, width, height, text, MAX_LINES, limit, scale, scale, true);
    return count;
}
"""

compiler = shutil.which("gcc")
if compiler is None and os.name == "nt":
    compiler = "C:/msys64/ucrt64/bin/gcc.exe"
with tempfile.TemporaryDirectory(prefix="fbi-ja-check-") as temporary:
    temporary = Path(temporary)
    source = temporary / "layout.c"
    library_path = temporary / ("layout.dll" if os.name == "nt" else "layout.so")
    source.write_text(host_source, encoding="utf-8")
    arguments = [compiler, "-std=gnu11", "-shared", "-O2", str(source), "-o", str(library_path)]
    if os.name != "nt":
        arguments.append("-fPIC")
    subprocess.run(arguments, check=True)
    library = ctypes.CDLL(str(library_path))
    library.measure.argtypes = [ctypes.c_char_p, ctypes.c_float, ctypes.c_float, ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_uint32)]
    library.measure.restype = ctypes.c_uint32
    samples = [
        "", "短い文", "長い日本語の確認文を画面幅に合わせて正しく折り返します。",
        "日本語とASCIIの混在 FBI 2.6.1-ja.1を確認します。",
        "longwordwithoutanyspaces0123456789" * 3,
        "line one\n\n二行目\n", "A: 選択 B: 戻る X: 更新 SELECT: 設定",
        "one two three four five six seven", "a\tb\t日本語",
    ] + list(translations.values())
    checks = 0
    for sample in samples:
        for limit in (120, 304, 384):
            width = ctypes.c_float()
            height = ctypes.c_float()
            lines = (ctypes.c_uint32 * 64)()
            count = library.measure(sample.encode("utf-8"), limit, 0.5, ctypes.byref(width), ctypes.byref(height), lines)
            assert width.value <= limit + 0.01, (sample, limit, width.value)
            assert sum(lines[:count]) == len(sample), (sample, list(lines[:count]))
            assert all(length > 0 for length in lines[:count]), sample
            checks += 1
    library.string_copy_utf8.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    for sample in ("名前.cia", "あいうえお", "ASCII", "a😀b", ""):
        encoded = sample.encode("utf-8")
        for size in range(len(encoded) + 3):
            buffer = ctypes.create_string_buffer(max(size, 1))
            library.string_copy_utf8(buffer, encoded, size)
            result = buffer.value.decode("utf-8")
            assert sample.startswith(result)
            assert len(buffer.value) < size or size == 0
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print(f"Validated {len(translations)} translations, {checks} wrapping cases, UTF-8 truncation, license and unchanged operations.")
