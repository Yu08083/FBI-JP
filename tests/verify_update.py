import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
version = re.search(r"JA_VERSION := (\S+)", (root / "Makefile").read_text(encoding="utf-8"))[1]
release_version = version
installed_ref = sys.argv[1] if len(sys.argv) > 1 else None
if installed_ref:
    version = re.search(r"JA_VERSION := (\S+)", subprocess.check_output(["git", "show", f"{installed_ref}:Makefile"], cwd=root).decode("utf-8"))[1]
major, minor, micro, revision = map(int, re.fullmatch(r"(\d+)\.(\d+)\.(\d+)-ja\.(\d+)", version).groups())
update = (root / "source/fbi/update.c").read_text(encoding="utf-8")
if installed_ref:
    update = subprocess.check_output(["git", "show", f"{installed_ref}:source/fbi/update.c"], cwd=root).decode("utf-8")
host_source = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <jansson.h>
typedef uint32_t u32;
typedef int32_t Result;
typedef void ui_view;
#define R_SUCCEEDED(result) ((result) >= 0)
#define R_FAILED(result) ((result) < 0)
#define R_APP_BAD_DATA -1
#define R_APP_PARSE_FAILED -2
#define R_APP_HTTP_ERROR_BASE -10000
#define DOWNLOAD_URL_MAX 4096
#define COLOR_TEXT 0
static const char* responses[2];
static Result results[2];
static int calls[2];
static bool launch_3dsx;
static int outcome;
static char installed_url[DOWNLOAD_URL_MAX];
static char installed_prompt[256];
Result http_download_json(const char* url, json_t** result, size_t maxSize) {
    int index = strstr(url, "/Yu08083/FBI-JP/") != NULL ? 0 : 1;
    calls[index]++;
    if(results[index] < 0) return results[index];
    json_error_t error;
    *result = json_loads(responses[index], 0, &error);
    return *result != NULL ? 0 : R_APP_PARSE_FAILED;
}
const char* fs_get_3dsx_path(void) { return launch_3dsx ? "/3ds/FBI/FBI.3dsx" : NULL; }
void string_copy(char* dest, const char* source, size_t size) { snprintf(dest, size, "%s", source); }
void ui_pop(void) {}
void info_destroy(ui_view* view) {}
void action_install_url(const char* text, const char* url, const char* path, void* a, void* b, void* c, ...) {
    outcome = 2;
    string_copy(installed_url, url, sizeof(installed_url));
    string_copy(installed_prompt, text, sizeof(installed_prompt));
}
void error_display_res(void* a, void* b, Result result, const char* text) { outcome = 3; }
void prompt_display_notify(const char* title, const char* text, int color, void* a, void* b, void* c) { outcome = 1; }
void info_display(const char* title, const char* text, bool flag, void* data, void (*callback)(ui_view*, void*, float*, char*), void* draw) {}
"""
host_source += f'#define FBI_JA_VERSION "{version}"\n#define VERSION_MAJOR {major}\n#define VERSION_MINOR {minor}\n#define VERSION_MICRO {micro}\n'
host_source += update[update.index("static bool update_is_newer("):]
host_source += r"""
int run_update(const char* japanese, const char* upstream, Result japaneseResult, Result upstreamResult, bool use3dsx) {
    responses[0] = japanese;
    responses[1] = upstream;
    results[0] = japaneseResult;
    results[1] = upstreamResult;
    calls[0] = calls[1] = outcome = 0;
    installed_url[0] = installed_prompt[0] = '\0';
    launch_3dsx = use3dsx;
    update_check_update(NULL, NULL, NULL, NULL);
    return outcome;
}
int request_count(int index) { return calls[index]; }
const char* selected_url(void) { return installed_url; }
const char* selected_prompt(void) { return installed_prompt; }
"""

def release(tag, japanese, assets=True):
    names = ("FBI-JP.cia", "FBI-JP.3dsx") if japanese else ("FBI.cia", "FBI.3dsx")
    return json.dumps({"tag_name": tag, "assets": [{"name": name, "browser_download_url": f"https://example.com/{tag}/{name}"} for name in names] if assets else []}).encode()

with tempfile.TemporaryDirectory(prefix="fbi-update-check-") as temporary:
    temporary = Path(temporary)
    source = temporary / "update.c"
    library_path = temporary / ("update.dll" if os.name == "nt" else "update.so")
    source.write_text(host_source, encoding="utf-8")
    arguments = [shutil.which("gcc"), "-std=gnu11", "-shared", "-O2", str(source), "-o", str(library_path), "-ljansson"]
    if os.name != "nt":
        arguments.append("-fPIC")
    subprocess.run(arguments, check=True)
    library = ctypes.CDLL(str(library_path))
    library.run_update.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_bool]
    library.run_update.restype = ctypes.c_int
    library.selected_url.restype = library.selected_prompt.restype = ctypes.c_char_p
    current = release(version, True)
    newer_tag = f"{major}.{minor}.{micro}-ja.{revision + 1}"
    newer = release(newer_tag, True)
    old = release(f"{major}.{minor}.{micro}-ja.{revision - 1}", True)
    upstream_current = release(f"{major}.{minor}.{micro}", False)
    upstream_newer_tag = f"{major}.{minor}.{micro + 1}"
    upstream_newer = release(upstream_newer_tag, False)
    cases = [
        (release(version + "+20261023", True), upstream_current, 0, 0, False, 1, 1, ""),
        (release(newer_tag + "+20261023", True), upstream_newer, 0, 0, False, 2, 0, f"{newer_tag}+20261023/FBI-JP.cia"),
        (newer, upstream_newer, 0, 0, False, 2, 0, f"{newer_tag}/FBI-JP.cia"),
        (newer, upstream_newer, 0, 0, True, 2, 0, f"{newer_tag}/FBI-JP.3dsx"),
        (current, upstream_newer, 0, 0, False, 2, 1, f"{upstream_newer_tag}/FBI.cia"),
        (current, upstream_newer, 0, 0, True, 2, 1, f"{upstream_newer_tag}/FBI.3dsx"),
        (current, upstream_current, 0, 0, False, 1, 1, ""),
        (old, upstream_current, 0, 0, False, 1, 1, ""),
        (current, release(f"v{major}.{minor + 1}.0", False), 0, 0, False, 2, 1, f"v{major}.{minor + 1}.0/FBI.cia"),
        (current, upstream_newer, -9596, 0, False, 2, 1, f"{upstream_newer_tag}/FBI.cia"),
        (current, upstream_current, 0, -9596, False, 1, 1, ""),
        (current, upstream_newer, -2, 0, False, 3, 0, ""),
        (current, upstream_newer, 0, -2, False, 3, 1, ""),
        (release(newer_tag, True, False), upstream_newer, 0, 0, False, 3, 0, ""),
    ]
    if installed_ref:
        latest = release(release_version, True)
        cases = [
            (latest, upstream_newer, 0, 0, False, 2, 0, f"{release_version}/FBI-JP.cia"),
            (latest, upstream_newer, 0, 0, True, 2, 0, f"{release_version}/FBI-JP.3dsx"),
        ]
    for japanese, upstream, jp_result, original_result, use3dsx, expected, upstream_calls, suffix in cases:
        assert library.run_update(japanese, upstream, jp_result, original_result, use3dsx) == expected
        assert library.request_count(0) == 1 and library.request_count(1) == upstream_calls
        assert library.selected_url().decode().endswith(suffix)
        if expected == 2:
            assert ("英語版" in library.selected_prompt().decode()) == bool(upstream_calls)
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print(f"Validated {len(cases)} update cases for CIA and 3DSX from {installed_ref or version}.")
