import ctypes
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
editor = (root / "source/fbi/action/edittext.c").read_text(encoding="utf-8")
write = editor[editor.index("static Result action_edit_text_write("):editor.index("static void action_edit_text_free(")]
host_source = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t Result;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int FS_Archive;
typedef unsigned Handle;
typedef struct { const char* data; } FS_Path;
#define FILE_PATH_MAX 512
#define R_SUCCEEDED(r) ((r) >= 0)
#define R_FAILED(r) ((r) < 0)
#define R_APP_OUT_OF_MEMORY -1
#define R_APP_OUT_OF_RANGE -2
#define R_APP_BAD_DATA -3
#define FS_OPEN_WRITE 2
#define FS_WRITE_FLUSH 1
#define FS_WRITE_UPDATE_TIME 2
#define ARCHIVE_ACTION_COMMIT_SAVE_DATA 0
typedef struct { bool exists; char data[32768]; u32 size; u32 attributes; } File;
static File files[3];
static int stage;
static int allocation_failure;
static int allocations;
static bool recovered;
static int index_of(const char* path) {
    if(strstr(path, ".fbi-tmp")) return 1;
    if(strstr(path, ".fbi-bak")) return 2;
    return 0;
}
static FS_Path* fs_make_path_utf8(const char* path) {
    if(++allocations == allocation_failure) return NULL;
    FS_Path* result = malloc(sizeof(FS_Path));
    result->data = strdup(path);
    return result;
}
static void fs_free_path_utf8(FS_Path* path) { free((void*) path->data); free(path); }
static Result FSUSER_CreateFile(FS_Archive archive, FS_Path path, u32 attributes, u64 size) {
    int index = index_of(path.data);
    if(stage == 1 || files[index].exists) return -10;
    files[index].exists = true;
    files[index].size = size;
    files[index].attributes = attributes;
    return 0;
}
static Result FSUSER_OpenFile(Handle* handle, FS_Archive archive, FS_Path path, u32 flags, u32 attributes) {
    if(stage == 2) return -10;
    *handle = index_of(path.data) + 1;
    return 0;
}
static Result FSFILE_Write(Handle handle, u32* written, u64 offset, const void* buffer, u32 size, u32 flags) {
    if(stage == 3) return -10;
    *written = stage == 4 ? size - 1 : size;
    memcpy(files[handle - 1].data, buffer, *written);
    return 0;
}
static Result FSFILE_SetAttributes(Handle handle, u32 attributes) {
    if(stage == 5) return -10;
    files[handle - 1].attributes = attributes;
    return 0;
}
static Result FSFILE_Close(Handle handle) { return stage == 6 ? -10 : 0; }
static Result FSUSER_RenameFile(FS_Archive a, FS_Path from, FS_Archive b, FS_Path to) {
    int source = index_of(from.data), target = index_of(to.data);
    if((source == 0 && stage == 7) || (source == 1 && (stage == 8 || stage == 9)) || (source == 2 && stage == 9)) return -10;
    if(!files[source].exists || files[target].exists) return -10;
    files[target] = files[source];
    files[source].exists = false;
    return 0;
}
static Result FSUSER_DeleteFile(FS_Archive archive, FS_Path path) { files[index_of(path.data)].exists = false; return 0; }
static Result FSUSER_ControlArchive(FS_Archive archive, int action, void* a, u32 b, void* c, u32 d) { return 0; }
""" + write + r"""
Result save_text(int failure, bool temporary_exists, bool backup_exists, int allocationFailure, const char* newText, u32 size) {
    memset(files, 0, sizeof(files));
    files[0].exists = true;
    memcpy(files[0].data, "original", 8);
    files[0].size = 8;
    files[0].attributes = 0x20;
    files[1].exists = temporary_exists;
    files[2].exists = backup_exists;
    memcpy(files[1].data, "existing-temp", 13);
    memcpy(files[2].data, "existing-backup", 15);
    files[1].size = 13;
    files[2].size = 15;
    stage = failure;
    allocations = 0;
    allocation_failure = allocationFailure;
    return action_edit_text_write(0, "/notes.txt", 0x20, newText, size, &recovered);
}
bool file_exists(int index) { return files[index].exists; }
const char* file_data(int index) { return files[index].data; }
u32 file_size(int index) { return files[index].size; }
u32 file_attributes(int index) { return files[index].attributes; }
bool recovery_required(void) { return recovered; }
"""

with tempfile.TemporaryDirectory(prefix="fbi-text-io-") as temporary:
    temporary = Path(temporary)
    source = temporary / "save.c"
    library_path = temporary / ("save.dll" if os.name == "nt" else "save.so")
    source.write_text(host_source, encoding="utf-8")
    arguments = [shutil.which("gcc"), "-std=gnu11", "-shared", "-O2", str(source), "-o", str(library_path)]
    if os.name != "nt":
        arguments.append("-fPIC")
    subprocess.run(arguments, check=True)
    library = ctypes.CDLL(str(library_path))
    library.save_text.argtypes = [ctypes.c_int, ctypes.c_bool, ctypes.c_bool, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint32]
    library.file_exists.restype = library.recovery_required.restype = ctypes.c_bool
    library.file_data.restype = ctypes.c_void_p
    for payload in (b"", b"x", "日本語\r\n".encode(), b"x" * 32768):
        assert library.save_text(0, False, False, 0, payload, len(payload)) == 0
        assert library.file_exists(0) and not library.file_exists(1) and not library.file_exists(2)
        assert library.file_size(0) == len(payload) and library.file_attributes(0) == 0x20
        assert ctypes.string_at(library.file_data(0), len(payload)) == payload
    for stage in range(1, 10):
        assert library.save_text(stage, False, False, 0, b"changed", 7) < 0
        location = 2 if stage == 9 else 0
        assert library.file_exists(location)
        assert ctypes.string_at(library.file_data(location), library.file_size(location)) == b"original"
        assert library.recovery_required() == (stage == 9)
        assert not library.file_exists(1)
    for allocation in (1, 2, 3):
        assert library.save_text(0, False, False, allocation, b"changed", 7) < 0
        assert ctypes.string_at(library.file_data(0), 8) == b"original"
    for temporary, backup in ((True, False), (False, True)):
        assert library.save_text(0, temporary, backup, 0, b"changed", 7) < 0
        assert ctypes.string_at(library.file_data(0), 8) == b"original"
        existing = 1 if temporary else 2
        assert library.file_exists(existing)
        assert ctypes.string_at(library.file_data(existing), library.file_size(existing)) == (b"existing-temp" if temporary else b"existing-backup")
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print("Validated 18 text-save cases, including short writes, rollback, allocation failures and existing backup files.")
