#include <stdarg.h>
#include <math.h>
#include "text.h"
#include "linkedlist.h"
typedef uint16_t u16;
typedef struct ui_view_s ui_view;
struct ui_view_s {
    const char* name;
    const char* info;
    void* data;
    void (*update)(ui_view*, void*, float, float, float, float);
    void (*drawTop)(ui_view*, void*, float, float, float, float);
    void (*drawBottom)(ui_view*, void*, float, float, float, float);
};
typedef struct { char name[512]; u32 color; void* data; } list_item;
typedef struct { FS_Archive archive; char name[256]; char path[512]; u32 attributes; u64 size; } file_info;
typedef struct { u16 px, py; } touchPosition;
typedef int SwkbdButton;
typedef int SwkbdCallbackResult;
typedef struct {
    int limit;
    SwkbdCallbackResult (*filter)(void*, const char**, const char*, size_t);
    void* user;
} SwkbdState;
#define KEY_A (1U << 0)
#define KEY_B (1U << 1)
#define KEY_SELECT (1U << 2)
#define KEY_START (1U << 3)
#define KEY_RIGHT (1U << 4)
#define KEY_LEFT (1U << 5)
#define KEY_UP (1U << 6)
#define KEY_DOWN (1U << 7)
#define KEY_R (1U << 8)
#define KEY_L (1U << 9)
#define KEY_X (1U << 10)
#define KEY_Y (1U << 11)
#define KEY_TOUCH (1U << 20)
#define FS_OPEN_READ 1
#define FS_ATTRIBUTE_DIRECTORY 0x10
#define COLOR_TEXT 0
#define TEXTURE_SELECTION_OVERLAY 1
#define SWKBD_TYPE_NORMAL 0
#define SWKBD_TYPE_NUMPAD 2
#define SWKBD_MULTILINE 8
#define SWKBD_PREDICTIVE_INPUT 4
#define SWKBD_ANYTHING 0
#define SWKBD_NOTEMPTY_NOTBLANK 2
#define SWKBD_BUTTON_NONE -1
#define SWKBD_BUTTON_LEFT 0
#define SWKBD_BUTTON_CONFIRM 2
#define SWKBD_CALLBACK_OK 0
#define SWKBD_CALLBACK_CONTINUE 2
static ui_view* stack[16];
static int depth;
static u32 down, held;
static u64 now;
static touchPosition touch;
static char keyboard_text[98305];
static int keyboard_button;
static char keyboard_initial[98305];
static char message[1024];
static void (*response_callback)(ui_view*, void*, u32);
static void (*menu_callback)(ui_view*, void*, linked_list*, list_item*, bool);
static linked_list menu_items;
static ui_view* ui_create(void) { return calloc(1, sizeof(ui_view)); }
static bool ui_push(ui_view* view) { stack[depth++] = view; return true; }
static void ui_pop(void) { depth--; }
static void ui_destroy(ui_view* view) { free(view); }
static u32 hidKeysDown(void) { return down; }
static u32 hidKeysHeld(void) { return held; }
static u64 osGetTime(void) { return now; }
static void hidTouchRead(touchPosition* output) { *output = touch; }
static void string_copy(char* dest, const char* source, size_t capacity) { snprintf(dest, capacity, "%s", source); }
static Result FSFILE_GetSize(Handle file, u64* size) { *size = files[file - 1].size; return 0; }
static Result FSFILE_Read(Handle file, u32* read, u64 offset, void* output, u32 size) { memcpy(output, files[file - 1].data + offset, size); *read = size; return 0; }
static void swkbdInit(SwkbdState* keyboard, int type, int buttons, int maximum) { memset(keyboard, 0, sizeof(*keyboard)); keyboard->limit = maximum; }
static void swkbdSetHintText(SwkbdState* keyboard, const char* hint) {}
static void swkbdSetInitialText(SwkbdState* keyboard, const char* text) { string_copy(keyboard_initial, text, sizeof(keyboard_initial)); }
static void swkbdSetFeatures(SwkbdState* keyboard, u32 features) {}
static void swkbdSetValidation(SwkbdState* keyboard, int mode, u32 flags, int digits) {}
static void swkbdSetButton(SwkbdState* keyboard, int button, const char* label, bool submit) {}
static void swkbdSetFilterCallback(SwkbdState* keyboard, SwkbdCallbackResult (*filter)(void*, const char**, const char*, size_t), void* user) { keyboard->filter = filter; keyboard->user = user; }
static SwkbdButton swkbdInputText(SwkbdState* keyboard, char* output, size_t capacity) {
    if(keyboard_button == SWKBD_BUTTON_CONFIRM && keyboard->filter != NULL) {
        const char* error = NULL;
        if(keyboard->filter(keyboard->user, &error, keyboard_text, strlen(keyboard_text)) != SWKBD_CALLBACK_OK) {
            string_copy(message, error, sizeof(message));
            return SWKBD_BUTTON_LEFT;
        }
    }
    string_copy(output, keyboard_text, capacity);
    return keyboard_button;
}
static ui_view* notify(const char* text, void* data, void (*callback)(ui_view*, void*, u32)) {
    string_copy(message, text, sizeof(message));
    ui_view* view = ui_create();
    view->data = data;
    response_callback = callback;
    ui_push(view);
    return view;
}
static ui_view* error_display(void* data, void* draw, const char* format, ...) { return notify(format, data, NULL); }
static ui_view* error_display_res(void* data, void* draw, Result result, const char* format, ...) { return notify(format, data, NULL); }
static ui_view* prompt_display_notify(const char* name, const char* text, u32 color, void* data, void* draw, void (*callback)(ui_view*, void*, u32)) { return notify(text, data, callback); }
static ui_view* prompt_display_multi_choice(const char* name, const char* text, u32 color, const char** options, u32* buttons, u32 count, void* data, void* draw, void (*callback)(ui_view*, void*, u32)) { return notify(text, data, callback); }
static ui_view* list_display(const char* name, const char* info, void* data, void (*callback)(ui_view*, void*, linked_list*, list_item*, bool), void* draw) {
    ui_view* view = ui_create();
    view->data = data;
    menu_callback = callback;
    linked_list_init(&menu_items);
    ui_push(view);
    callback(view, data, &menu_items, NULL, false);
    return view;
}
static void list_destroy(ui_view* view) { linked_list_destroy(&menu_items); ui_destroy(view); }
typedef struct { char text[2048]; float x, y, scale, wrap; int screen; } Draw;
static Draw draws[100];
static size_t draw_count;
static int draw_screen;
static u32 measure_calls;
static void measure(const char* text, float scale, float wrap, float* width, float* height) {
    measure_calls++;
    float x = 0, maximum = 0;
    int lines = 1;
    for(size_t i = 0; text[i] != '\0';) {
        unsigned char value = text[i++];
        if(value == '\n') { if(x > maximum) maximum = x; x = 0; lines++; continue; }
        size_t extra = value < 0x80 ? 0 : value < 0xE0 ? 1 : value < 0xF0 ? 2 : 3;
        i += extra;
        float step = (extra == 0 ? 16 : 30) * scale;
        if(wrap > 0 && x + step > wrap && x > 0) { if(x > maximum) maximum = x; x = 0; lines++; }
        x += step;
    }
    if(width) *width = x > maximum ? x : maximum;
    if(height) *height = lines * 30 * scale;
}
static float screen_get_font_height(float scale) { return 30 * scale; }
static void screen_get_string_size(float* width, float* height, const char* text, float sx, float sy) { measure(text, sx, 0, width, height); }
static void screen_get_string_size_wrap(float* width, float* height, const char* text, float sx, float sy, float wrap) { measure(text, sx, wrap, width, height); }
static void screen_get_texture_size(u32* width, u32* height, u32 texture) { if(width) *width = 320; if(height) *height = 18; }
static void screen_draw_texture(u32 texture, float x, float y, float width, float height) {}
static void screen_draw_string_wrap(const char* text, float x, float y, float sx, float sy, u32 color, bool center, float wrap) {
    if(draw_count < 100) {
        Draw* draw = &draws[draw_count++];
        string_copy(draw->text, text, sizeof(draw->text));
        draw->x = x; draw->y = y; draw->scale = sx; draw->wrap = wrap; draw->screen = draw_screen;
    }
}
static void screen_draw_string(const char* text, float x, float y, float sx, float sy, u32 color, bool center) { screen_draw_string_wrap(text, x, y, sx, sy, color, center, 0); }
