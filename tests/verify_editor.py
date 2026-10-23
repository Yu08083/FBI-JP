import ctypes
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
io_test = (root / "tests/verify_text_io.py").read_text(encoding="utf-8")
prefix = re.search('host_source = r"""(.*?)"""', io_test, re.S)[1]
editor = (root / "source/fbi/action/edittext.c").read_text(encoding="utf-8")
source_text = prefix + (root / "tests/editor_host.h").read_text(encoding="utf-8")
source_text += editor[editor.index("typedef struct {"):]
source_text += r"""
static file_info target;
void editor_open(const char* bytes, u32 size) {
    depth = 0;
    stage = allocations = allocation_failure = 0;
    memset(files, 0, sizeof(files));
    files[0].exists = true;
    files[0].size = size;
    files[0].attributes = 0x20;
    memcpy(files[0].data, bytes, size);
    memset(&target, 0, sizeof(target));
    string_copy(target.name, "メモ.txt", sizeof(target.name));
    string_copy(target.path, "/notes.txt", sizeof(target.path));
    target.attributes = 0x20;
    list_item selected = {0};
    selected.data = &target;
    linked_list items;
    linked_list_init(&items);
    action_edit_text(&items, &selected);
}
static edit_text_data* active_editor(void) { return depth > 0 ? stack[0]->data : NULL; }
void editor_keyboard(const char* text, int button) { string_copy(keyboard_text, text, sizeof(keyboard_text)); keyboard_button = button; }
void editor_input(u32 pressed, u32 kept, u16 x, u16 y) {
    down = pressed;
    held = kept;
    touch.px = x; touch.py = y;
    now += 500;
    if(depth > 0 && stack[depth - 1]->update) {
        ui_view* view = stack[depth - 1];
        view->update(view, view->data, 0, 20, 320, 220);
    }
    down = held = 0;
}
void editor_tick(u32 keys, u16 x, u16 y) { editor_input(keys, keys, x, y); }
void editor_respond(u32 response) {
    ui_view* view = stack[depth - 1];
    void (*callback)(ui_view*, void*, u32) = response_callback;
    response_callback = NULL;
    ui_pop();
    if(callback) callback(view, view->data, response);
    ui_destroy(view);
}
void editor_choose(u32 index) {
    ui_view* view = stack[depth - 1];
    down = KEY_A;
    menu_callback(view, view->data, &menu_items, linked_list_get(&menu_items, index), false);
    down = 0;
}
const char* editor_text(void) { return active_editor()->text; }
const char* editor_initial(void) { return keyboard_initial; }
const char* editor_message(void) { return message; }
size_t editor_cursor(void) { return active_editor()->cursor; }
size_t editor_lines(void) { return active_editor()->lineCount; }
size_t editor_scroll(void) { return active_editor()->scroll; }
bool editor_dirty(void) { return active_editor()->dirty; }
int editor_depth(void) { return depth; }
void editor_fail(int failure) { stage = failure; }
const char* saved_bytes(void) { return files[0].data; }
u32 saved_size(void) { return files[0].size; }
size_t editor_draw(int screen) {
    draw_count = 0;
    measure_calls = 0;
    draw_screen = screen;
    ui_view* view = stack[0];
    if(screen == 0) view->drawTop(view, view->data, 0, 20, 400, 220);
    else view->drawBottom(view, view->data, 0, 20, 320, 220);
    return draw_count;
}
u32 editor_measures(void) { return measure_calls; }
const char* draw_text(size_t index) { return draws[index].text; }
float draw_number(size_t index, int field) {
    Draw* draw = &draws[index];
    return field == 0 ? draw->x : field == 1 ? draw->y : field == 2 ? draw->scale : draw->wrap;
}
"""

keys = {"A": 1, "B": 2, "SELECT": 4, "START": 8, "RIGHT": 16, "LEFT": 32, "UP": 64, "DOWN": 128, "R": 256, "L": 512, "X": 1024, "Y": 2048, "TOUCH": 1 << 20}
with tempfile.TemporaryDirectory(prefix="fbi-editor-check-") as temporary:
    temporary = Path(temporary)
    source = temporary / "editor.c"
    library_path = temporary / ("editor.dll" if os.name == "nt" else "editor.so")
    source.write_text(source_text, encoding="utf-8")
    arguments = [shutil.which("gcc"), "-std=gnu11", "-shared", "-O2", "-I", str(root / "source/core"), str(source), str(root / "source/core/text.c"), str(root / "source/core/linkedlist.c"), "-o", str(library_path)]
    if os.name != "nt":
        arguments.append("-fPIC")
    subprocess.run(arguments, check=True)
    library = ctypes.CDLL(str(library_path))
    library.editor_open.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
    library.editor_keyboard.argtypes = [ctypes.c_char_p, ctypes.c_int]
    library.editor_tick.argtypes = [ctypes.c_uint32, ctypes.c_uint16, ctypes.c_uint16]
    library.editor_input.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint16, ctypes.c_uint16]
    for name in ("editor_text", "editor_initial", "editor_message", "draw_text"):
        getattr(library, name).restype = ctypes.c_char_p
    for name in ("editor_cursor", "editor_lines", "editor_scroll", "editor_draw"):
        getattr(library, name).restype = ctypes.c_size_t
    library.editor_dirty.restype = ctypes.c_bool
    library.saved_bytes.restype = ctypes.c_void_p
    library.draw_number.argtypes = [ctypes.c_size_t, ctypes.c_int]
    library.draw_number.restype = ctypes.c_float

    def tick(key=0, x=0, y=0):
        library.editor_tick(keys.get(key, key), x, y)

    def keyboard(text, confirm=True):
        library.editor_keyboard(text.encode(), 2 if confirm else 0)

    def saved():
        return ctypes.string_at(library.saved_bytes(), library.saved_size())

    def close_discard():
        tick("B")
        if library.editor_depth() == 2:
            library.editor_respond(1)
        assert library.editor_depth() == 0

    def choose(index):
        tick("X")
        library.editor_choose(index)

    def snapshot(screen):
        count = library.editor_draw(screen)
        calls = []
        for i in range(count):
            text = library.draw_text(i).decode()
            x, y, scale, wrap = [library.draw_number(i, j) for j in range(4)]
            assert 0 <= x < (400 if screen == 0 else 320) and 20 <= y < 220
            calls.append(dict(text=text, x=x, y=y, scale=scale, wrap=wrap))
        return calls

    initial = b"\xEF\xBB\xBF" + "先頭\r\n設定=true\r\n日本語\r\n末尾\r\n".encode()
    library.editor_open(initial, len(initial))
    tick()
    assert library.editor_lines() == 5 and not library.editor_dirty()
    top = snapshot(0)
    assert any("設定=true" in call["text"] for call in top)
    keyboard("日本語")
    tick("Y")
    assert library.editor_cursor() == 2 and saved() == initial
    keyboard("日本語を編集")
    tick("A")
    assert library.editor_initial().decode() == "日本語"
    assert library.editor_dirty() and "日本語を編集" in library.editor_text().decode()
    assert saved() == initial
    tick("B")
    assert library.editor_depth() == 2
    library.editor_respond(2)
    assert library.editor_depth() == 1 and library.editor_dirty()
    tick("SELECT")
    assert not library.editor_dirty() and saved() == initial
    keyboard("保存する日本語")
    tick("A")
    tick("START")
    assert saved() == initial.replace("日本語".encode(), "保存する日本語".encode())
    assert not library.editor_dirty()
    library.editor_respond(0)
    close_discard()

    library.editor_open(b"one\ntwo\nthree", 13)
    tick()
    keyboard("3")
    choose(6)
    assert library.editor_cursor() == 2
    choose(1)
    assert library.editor_text() == b"one\ntwo\n\nthree"
    choose(3)
    assert library.editor_text() == b"one\ntwo\nthree"
    choose(4)
    assert library.editor_text() == b"one\ntwo\n\nthree"
    keyboard("full\nreplacement")
    choose(7)
    assert library.editor_text() == b"full\nreplacement"
    library.editor_fail(3)
    tick("START")
    assert saved() == b"one\ntwo\nthree" and library.editor_dirty()
    library.editor_respond(0)
    library.editor_fail(0)
    tick("B")
    library.editor_respond(0)
    assert library.editor_depth() == 0 and saved() == b"full\nreplacement"

    library.editor_open(b"original", 8)
    tick()
    keyboard("cancelled", False)
    tick("A")
    assert library.editor_text() == b"original"
    keyboard("unsaved")
    tick("A")
    close_discard()
    assert saved() == b"original"

    touch_document = "\n".join(f"line {i}" for i in range(50)).encode()
    library.editor_open(touch_document, len(touch_document))
    tick()
    tick("TOUCH", 100, 57)
    tick()
    assert library.editor_cursor() == 2 and not library.editor_dirty()
    keyboard("touch edit")
    tick("TOUCH", 100, 57)
    tick()
    assert library.editor_text().split(b"\n")[2] == b"touch edit"
    tick("TOUCH", 100, 150)
    library.editor_input(0, keys["TOUCH"], 100, 40)
    tick()
    assert library.editor_scroll() > 0 and library.editor_cursor() >= library.editor_scroll()
    assert library.editor_text().split(b"\n")[2] == b"touch edit"
    close_discard()

    document = "長い行のプレビューを確認します。" * 200 + "\n次の行\n末尾"
    payload = document.encode()
    library.editor_open(payload, len(payload))
    tick()
    first = snapshot(0)
    assert library.editor_measures() < 80
    tick("R")
    second = snapshot(0)
    assert first != second
    tick("L")
    assert snapshot(0) == first
    assert len(snapshot(1)) < 40
    close_discard()

    fixture = "タイトル\n\n[設定]\n言語=日本語\n効果音=ON\n表示=標準\n\n[メモ]\n行を選んでAで編集します。\nYで文字を検索できます。\n"
    library.editor_open(fixture.encode(), len(fixture.encode()))
    tick()
    keyboard("言語")
    tick("Y")
    preview = {"top": snapshot(0), "bottom": snapshot(1), "selected": library.editor_cursor(), "scroll": library.editor_scroll()}
    if os.environ.get("FBI_EDITOR_PREVIEW"):
        Path(os.environ["FBI_EDITOR_PREVIEW"]).write_text(json.dumps(preview, ensure_ascii=False), encoding="utf-8")
    close_discard()

    library.editor_open(b"\n" * 32768, 32768)
    tick()
    assert library.editor_lines() == 32769
    keyboard("32769")
    choose(6)
    assert library.editor_cursor() == 32768 and len(snapshot(1)) <= 30
    close_discard()
    if os.name == "nt":
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(library._handle)

print("Validated editor navigation, Japanese search, line changes, undo, save/discard/cancel, failed saves and previews up to 32769 lines.")
