import ctypes
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "source/fbi/action/installurl.c").read_text(encoding="utf-8")
structure = source[source.index("typedef enum content_type_e"):source.index("static void action_install_url_free_data(")]
free = source[source.index("static void action_install_url_free_data("):source.index("static void action_install_url_n3ds_onresponse(")]
source_url = source[source.index("static Result action_install_url_get_src_url("):source.index("static Result action_install_url_open_dst(")]
ui = source[source.index("static bool action_install_url_error("):]
host_source = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;
typedef void ui_view;
typedef struct { int dummy; } ticket_info;
typedef struct data_op_data_s {
    void* data;
    u32 total, processed, op, bufferSize;
    u64 currTotal, currProcessed, bytesPerSecond, estimatedRemainingSeconds;
    bool finished;
    Result result;
    int cancelEvent;
    Result (*getSrcUrl)(void*, u32, char*, size_t);
    Result (*openDst)(void*, u32, void*, u64, u32*);
    Result (*closeDst)(void*, u32, bool, u32);
    Result (*writeDst)(void*, u32, u32*, void*, u64, u32);
    Result (*suspend)(void*, u32);
    Result (*restore)(void*, u32);
    bool (*error)(void*, u32, Result, ui_view**);
} data_op_data;
#define INSTALL_URLS_MAX 8
#define DOWNLOAD_URL_MAX 4096
#define FILE_PATH_MAX 512
#define PROGRESS_TEXT_MAX 512
#define COLOR_TEXT 0
#define KEY_B 2
#define PROMPT_YES 0
#define DATAOP_DOWNLOAD 1
#define R_SUCCEEDED(result) ((result) >= 0)
static void* current;
static data_op_data* transfer;
static bool leak;
static int screens;
static int cancelled;
static const char* secret = "https://private.example/token-secret";
static void record(const char* name, const char* text, void* draw) {
    screens++;
    if(draw != NULL || (name && strstr(name, secret)) || (text && strstr(text, secret))) leak = true;
}
static ui_view* prompt_display_yes_no(const char* name, const char* text, int color, void* data, void* draw, void* response) { current = data; record(name, text, draw); return NULL; }
static ui_view* prompt_display_notify(const char* name, const char* text, int color, void* data, void* draw, void* response) { record(name, text, draw); return NULL; }
static ui_view* error_display_res(void* data, void* draw, Result result, const char* text) { record(NULL, text, draw); return NULL; }
static ui_view* error_display(void* data, void* draw, const char* text) { record(NULL, text, draw); return NULL; }
static void info_display(const char* name, const char* text, bool bar, void* data, void* update, void* draw) { record(name, text, draw); }
static Result task_data_op(data_op_data* data) { transfer = data; data->finished = false; return 0; }
static void ui_pop(void) {}
static void info_destroy(ui_view* view) {}
static u32 hidKeysDown(void) { return cancelled ? KEY_B : 0; }
static void svcSignalEvent(int handle) { cancelled = 2; }
static double ui_get_display_size(u64 size) { return size / 1024.0; }
static const char* ui_get_display_size_units(u64 size) { return "KiB"; }
static const char* ui_get_display_eta(u64 seconds) { return "1s"; }
static void string_copy(char* destination, const char* input, size_t size) { snprintf(destination, size, "%s", input); }
static Result action_install_url_open_dst(void* data, u32 index, void* initial, u64 size, u32* handle) { return 0; }
static Result action_install_url_close_dst(void* data, u32 index, bool succeeded, u32 handle) { return 0; }
static Result action_install_url_write_dst(void* data, u32 handle, u32* written, void* buffer, u64 offset, u32 size) { return 0; }
static Result action_install_url_suspend(void* data, u32 index) { return 0; }
static Result action_install_url_restore(void* data, u32 index) { return 0; }
""" + structure + free + source_url + ui + r"""
int test_flow(int mode, bool multiple) {
    leak = false; screens = cancelled = 0;
    const char* urls = multiple ? "https://private.example/token-secret/one.cia\nhttps://private.example/token-secret/two.cia" : "https://private.example/token-secret/one.cia";
    action_install_url("インストールしますか？", urls, NULL, NULL, NULL, NULL);
    if(mode == 0) {
        action_install_url_confirm_onresponse(NULL, current, 1);
        return !leak && screens == 1;
    }
    action_install_url_confirm_onresponse(NULL, current, PROMPT_YES);
    char url[DOWNLOAD_URL_MAX];
    transfer->getSrcUrl(current, 0, url, sizeof(url));
    if(strstr(url, secret) == NULL) return 0;
    transfer->currProcessed = 4096; transfer->currTotal = 8192;
    transfer->processed = 0;
    float progress = 0;
    char text[PROGRESS_TEXT_MAX];
    cancelled = mode == 2;
    action_install_url_install_update(NULL, current, &progress, text);
    if(strstr(text, secret) != NULL || progress != 0.5f || (mode == 2 && cancelled != 2)) return 0;
    if(mode == 3) {
        ui_view* error;
        action_install_url_error(current, 0, -1, &error);
    }
    transfer->processed = transfer->total;
    transfer->finished = true;
    transfer->result = mode == 3 ? -1 : 0;
    action_install_url_install_update(NULL, current, &progress, text);
    return !leak && screens >= 2;
}
"""

with tempfile.TemporaryDirectory(prefix="fbi-download-ui-") as temporary:
    temporary = Path(temporary)
    source_path = temporary / "download.c"
    library_path = temporary / ("download.dll" if os.name == "nt" else "download.so")
    source_path.write_text(host_source, encoding="utf-8")
    arguments = [shutil.which("gcc"), "-std=gnu11", "-shared", "-O2", str(source_path), "-o", str(library_path)]
    if os.name != "nt":
        arguments.append("-fPIC")
    subprocess.run(arguments, check=True)
    library = ctypes.CDLL(str(library_path))
    library.test_flow.argtypes = [ctypes.c_int, ctypes.c_bool]
    for mode in range(4):
        for multiple in (False, True):
            assert library.test_flow(mode, multiple), (mode, multiple)
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print("Validated 8 download UI flows without URL text or upper-screen callbacks, including confirmation, progress, errors and cancellation.")
