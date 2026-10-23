#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "action.h"
#include "../resources.h"
#include "../task/uitask.h"
#include "../../core/core.h"
#include "../../core/text.h"

typedef struct {
    file_info* target;
    text_format format;
    char* original;
    char* text;
    char* undo;
    char* candidate;
    char* input;
    unsigned char* encoded;
    size_t* offsets;
    size_t lineCount;
    size_t textSize;
    size_t cursor;
    size_t scroll;
    size_t rows;
    size_t previewPages[256];
    size_t previewPage;
    size_t previewNext;
    u64 nextMove;
    bool dirty;
    bool canUndo;
    bool editWhole;
    bool touching;
    bool touchMoved;
    bool touchSelected;
    u16 touchStart;
    u16 touchLast;
    char query[256];
    ui_view* view;
} edit_text_data;

static Result action_edit_text_write(FS_Archive archive, const char* path, u32 attributes, const void* buffer, u32 size, bool* recovery) {
    *recovery = false;
    char temporary[FILE_PATH_MAX];
    char backup[FILE_PATH_MAX];
    if(snprintf(temporary, sizeof(temporary), "%s.fbi-tmp", path) >= sizeof(temporary)
       || snprintf(backup, sizeof(backup), "%s.fbi-bak", path) >= sizeof(backup)) {
        return R_APP_OUT_OF_RANGE;
    }
    FS_Path* destinationPath = fs_make_path_utf8(path);
    FS_Path* temporaryPath = fs_make_path_utf8(temporary);
    FS_Path* backupPath = fs_make_path_utf8(backup);
    Result res = R_APP_OUT_OF_MEMORY;
    bool created = false;
    if(destinationPath != NULL && temporaryPath != NULL && backupPath != NULL) {
        res = FSUSER_CreateFile(archive, *temporaryPath, 0, size);
        created = R_SUCCEEDED(res);
        if(created) {
            Handle handle = 0;
            if(R_SUCCEEDED(res = FSUSER_OpenFile(&handle, archive, *temporaryPath, FS_OPEN_WRITE, 0))) {
                if(size > 0) {
                    u32 written = 0;
                    res = FSFILE_Write(handle, &written, 0, buffer, size, FS_WRITE_FLUSH | FS_WRITE_UPDATE_TIME);
                    if(R_SUCCEEDED(res) && written != size) {
                        res = R_APP_BAD_DATA;
                    }
                }
                if(R_SUCCEEDED(res)) {
                    res = FSFILE_SetAttributes(handle, attributes);
                }
                Result closeResult = FSFILE_Close(handle);
                if(R_SUCCEEDED(res)) {
                    res = closeResult;
                }
            }
            if(R_SUCCEEDED(res)) {
                res = FSUSER_RenameFile(archive, *destinationPath, archive, *backupPath);
                if(R_SUCCEEDED(res)) {
                    res = FSUSER_RenameFile(archive, *temporaryPath, archive, *destinationPath);
                    if(R_SUCCEEDED(res)) {
                        created = false;
                        FSUSER_DeleteFile(archive, *backupPath);
                    } else if(R_FAILED(FSUSER_RenameFile(archive, *backupPath, archive, *destinationPath))) {
                        *recovery = true;
                    }
                }
            }
        }
        if(created) {
            FSUSER_DeleteFile(archive, *temporaryPath);
        }
        FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    }
    if(destinationPath != NULL) {
        fs_free_path_utf8(destinationPath);
    }
    if(temporaryPath != NULL) {
        fs_free_path_utf8(temporaryPath);
    }
    if(backupPath != NULL) {
        fs_free_path_utf8(backupPath);
    }
    return res;
}

static void action_edit_text_free(edit_text_data* data) {
    if(data != NULL) {
        free(data->original);
        free(data->text);
        free(data->undo);
        free(data->candidate);
        free(data->input);
        free(data->encoded);
        free(data->offsets);
        free(data);
    }
}

static void action_edit_text_select(edit_text_data* data, size_t line) {
    if(line >= data->lineCount) {
        line = data->lineCount - 1;
    }
    if(line != data->cursor) {
        data->previewPage = 0;
        data->previewPages[0] = 0;
        data->previewNext = 0;
    }
    data->cursor = line;
    if(data->cursor < data->scroll) {
        data->scroll = data->cursor;
    }
    if(data->cursor >= data->scroll + data->rows) {
        data->scroll = data->cursor - data->rows + 1;
    }
}

static void action_edit_text_reindex(edit_text_data* data) {
    data->textSize = strlen(data->text);
    data->lineCount = text_index_lines(data->text, data->offsets, TEXT_FILE_MAX_SIZE + 1);
    data->dirty = strcmp(data->text, data->original) != 0;
    data->previewPage = 0;
    data->previewPages[0] = 0;
    data->previewNext = 0;
    action_edit_text_select(data, data->cursor);
}

static size_t action_edit_text_line_size(edit_text_data* data, size_t line) {
    return (line + 1 < data->lineCount ? data->offsets[line + 1] - 1 : data->textSize) - data->offsets[line];
}

static void action_edit_text_accept(edit_text_data* data) {
    if(strcmp(data->candidate, data->text) != 0) {
        string_copy(data->undo, data->text, TEXT_FILE_MAX_SIZE + 1);
        string_copy(data->text, data->candidate, TEXT_FILE_MAX_SIZE + 1);
        data->canUndo = true;
        action_edit_text_reindex(data);
    }
}

static bool action_edit_text_candidate(edit_text_data* data, const char* text) {
    if(data->editWhole) {
        if(strlen(text) > TEXT_FILE_MAX_SIZE) {
            return false;
        }
        string_copy(data->candidate, text, TEXT_FILE_MAX_SIZE + 1);
    } else {
        string_copy(data->candidate, data->text, TEXT_FILE_MAX_SIZE + 1);
        if(!text_replace_line(data->candidate, TEXT_FILE_MAX_SIZE + 1, data->cursor, text)) {
            return false;
        }
    }
    size_t size = 0;
    return text_encode(data->candidate, data->format, data->encoded, TEXT_FILE_MAX_SIZE, &size);
}

static SwkbdCallbackResult action_edit_text_validate(void* user, const char** message, const char* text, size_t length) {
    edit_text_data* data = user;
    if(!action_edit_text_candidate(data, text)) {
        *message = "編集できるテキストは32KBまでです。";
        return SWKBD_CALLBACK_CONTINUE;
    }
    return SWKBD_CALLBACK_OK;
}

static void action_edit_text_keyboard(edit_text_data* data, bool whole) {
    data->editWhole = whole;
    data->touching = false;
    if(whole) {
        string_copy(data->input, data->text, TEXT_KEYBOARD_BUFFER_SIZE);
    } else {
        size_t size = action_edit_text_line_size(data, data->cursor);
        memcpy(data->input, data->text + data->offsets[data->cursor], size);
        data->input[size] = '\0';
    }
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, TEXT_KEYBOARD_MAX_UNITS);
    swkbdSetHintText(&keyboard, whole ? "全文を編集" : "選択した行を編集");
    swkbdSetInitialText(&keyboard, data->input);
    swkbdSetFeatures(&keyboard, SWKBD_MULTILINE | SWKBD_PREDICTIVE_INPUT);
    swkbdSetValidation(&keyboard, SWKBD_ANYTHING, 0, 0);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "キャンセル", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_CONFIRM, "反映", true);
    swkbdSetFilterCallback(&keyboard, action_edit_text_validate, data);
    SwkbdButton button = swkbdInputText(&keyboard, data->input, TEXT_KEYBOARD_BUFFER_SIZE);
    if(button == SWKBD_BUTTON_CONFIRM) {
        if(!action_edit_text_candidate(data, data->input)) {
            error_display(NULL, NULL, "編集できるテキストは32KBまでです。");
            return;
        }
        action_edit_text_accept(data);
    } else if(button == SWKBD_BUTTON_NONE) {
        error_display(NULL, NULL, "入力画面を開けませんでした。");
    }
}

static bool action_edit_text_save(edit_text_data* data) {
    if(!data->dirty) {
        return true;
    }
    size_t size = 0;
    if(!text_encode(data->text, data->format, data->encoded, TEXT_FILE_MAX_SIZE, &size)) {
        error_display(NULL, NULL, "編集できるテキストは32KBまでです。");
        return false;
    }
    bool recovery = false;
    Result res = action_edit_text_write(data->target->archive, data->target->path, data->target->attributes, data->encoded, size, &recovery);
    if(R_FAILED(res)) {
        error_display_res(NULL, NULL, res, recovery ? "保存に失敗しました。\n元の内容は同名の.fbi-bakにあります。" : "テキストの保存に失敗しました。");
        return false;
    }
    data->target->size = size;
    string_copy(data->original, data->text, TEXT_FILE_MAX_SIZE + 1);
    data->dirty = false;
    return true;
}

static void action_edit_text_close(edit_text_data* data) {
    ui_view* view = data->view;
    ui_pop();
    action_edit_text_free(data);
    ui_destroy(view);
}

static void action_edit_text_exit_response(ui_view* view, void* user, u32 response) {
    edit_text_data* data = user;
    if(response == 1 || (response == 0 && action_edit_text_save(data))) {
        action_edit_text_close(data);
    }
}

static void action_edit_text_undo(edit_text_data* data) {
    if(data->canUndo) {
        char* previous = data->text;
        data->text = data->undo;
        data->undo = previous;
        data->canUndo = false;
        action_edit_text_reindex(data);
    }
}

static void action_edit_text_find(edit_text_data* data, bool next) {
    size_t line = 0;
    if(text_find_line(data->text, data->query, next ? data->cursor + 1 : 0, &line)) {
        action_edit_text_select(data, line);
    } else {
        prompt_display_notify("検索", "一致する文字が見つかりません。", COLOR_TEXT, NULL, NULL, NULL);
    }
}

static void action_edit_text_search(edit_text_data* data) {
    string_copy(data->input, data->query, TEXT_KEYBOARD_BUFFER_SIZE);
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 80);
    swkbdSetHintText(&keyboard, "検索する文字");
    swkbdSetInitialText(&keyboard, data->input);
    swkbdSetFeatures(&keyboard, SWKBD_PREDICTIVE_INPUT);
    swkbdSetValidation(&keyboard, SWKBD_ANYTHING, 0, 0);
    if(swkbdInputText(&keyboard, data->input, TEXT_KEYBOARD_BUFFER_SIZE) == SWKBD_BUTTON_CONFIRM && data->input[0] != '\0') {
        bool next = strcmp(data->query, data->input) == 0;
        string_copy(data->query, data->input, sizeof(data->query));
        action_edit_text_find(data, next);
    }
}

static void action_edit_text_jump(edit_text_data* data) {
    snprintf(data->input, TEXT_KEYBOARD_BUFFER_SIZE, "%lu", (u32) data->cursor + 1);
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NUMPAD, 2, 5);
    swkbdSetHintText(&keyboard, "移動する行番号");
    swkbdSetInitialText(&keyboard, data->input);
    swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if(swkbdInputText(&keyboard, data->input, TEXT_KEYBOARD_BUFFER_SIZE) == SWKBD_BUTTON_CONFIRM) {
        unsigned int line = 0;
        char extra = 0;
        if(sscanf(data->input, "%u%c", &line, &extra) == 1 && line > 0 && line <= data->lineCount) {
            action_edit_text_select(data, line - 1);
        } else {
            error_display(NULL, NULL, "範囲内の行番号を入力してください。");
        }
    }
}

static list_item edit_text_actions[] = {
    {"保存", COLOR_TEXT, NULL},
    {"前に行を追加", COLOR_TEXT, NULL},
    {"後に行を追加", COLOR_TEXT, NULL},
    {"行を削除", COLOR_TEXT, NULL},
    {"元に戻す", COLOR_TEXT, NULL},
    {"次の一致へ移動", COLOR_TEXT, NULL},
    {"行番号へ移動", COLOR_TEXT, NULL},
    {"全文を編集", COLOR_TEXT, NULL}
};

static void action_edit_text_menu(ui_view* view, void* user, linked_list* items, list_item* selected, bool touched) {
    edit_text_data* data = user;
    u32 down = hidKeysDown();
    if((down & KEY_B) || (selected != NULL && ((down & KEY_A) || touched))) {
        size_t action = selected != NULL ? (size_t) (selected - edit_text_actions) : 0;
        ui_pop();
        list_destroy(view);
        if(down & KEY_B) {
            return;
        }
        if(action == 0) {
            if(action_edit_text_save(data)) {
                prompt_display_notify("完了", "テキストを保存しました。", COLOR_TEXT, NULL, NULL, NULL);
            }
        } else if(action <= 3) {
            string_copy(data->candidate, data->text, TEXT_FILE_MAX_SIZE + 1);
            bool succeeded = action == 3 ? text_delete_line(data->candidate, TEXT_FILE_MAX_SIZE + 1, data->cursor)
                                         : text_insert_line(data->candidate, TEXT_FILE_MAX_SIZE + 1, data->cursor, action == 2);
            size_t size = 0;
            if(succeeded && text_encode(data->candidate, data->format, data->encoded, TEXT_FILE_MAX_SIZE, &size)) {
                action_edit_text_accept(data);
                if(action == 2) {
                    action_edit_text_select(data, data->cursor + 1);
                }
            } else {
                error_display(NULL, NULL, "編集できるテキストは32KBまでです。");
            }
        } else if(action == 4) {
            action_edit_text_undo(data);
        } else if(action == 5) {
            if(data->query[0] != '\0') {
                action_edit_text_find(data, true);
            } else {
                action_edit_text_search(data);
            }
        } else if(action == 6) {
            action_edit_text_jump(data);
        } else {
            action_edit_text_keyboard(data, true);
        }
        return;
    }
    if(linked_list_size(items) == 0) {
        for(size_t i = 0; i < sizeof(edit_text_actions) / sizeof(edit_text_actions[0]); i++) {
            linked_list_add(items, &edit_text_actions[i]);
        }
    }
}

static size_t action_edit_text_prefix(char* output, size_t capacity, const char* input, size_t size) {
    size_t position = 0;
    size_t written = 0;
    while(position < size) {
        size_t next = position + 1;
        while(((unsigned char) input[next] & 0xC0) == 0x80) {
            next++;
        }
        size_t count = input[position] == '\t' ? 4 : next - position;
        if(next > size || written + count >= capacity) {
            break;
        }
        if(input[position] == '\t') {
            memset(output + written, ' ', count);
        } else {
            memcpy(output + written, input + position, count);
        }
        written += count;
        position = next;
    }
    output[written] = '\0';
    return position;
}

static size_t action_edit_text_fit(char* output, size_t capacity, const char* input, size_t size, float width, float height, float scale, bool wrap) {
    size_t low = 0;
    size_t high = size < capacity - 1 ? size : capacity - 1;
    size_t best = 0;
    while(low <= high) {
        size_t middle = low + (high - low) / 2;
        size_t used = action_edit_text_prefix(output, capacity, input, middle);
        float measuredWidth = 0;
        float measuredHeight = 0;
        if(wrap) {
            screen_get_string_size_wrap(&measuredWidth, &measuredHeight, output, scale, scale, width);
        } else {
            screen_get_string_size(&measuredWidth, &measuredHeight, output, scale, scale);
        }
        if(measuredHeight <= height && (wrap || measuredWidth <= width)) {
            best = used;
            low = middle + 1;
        } else if(middle > 0) {
            high = middle - 1;
        } else {
            break;
        }
    }
    return action_edit_text_prefix(output, capacity, input, best);
}

static void action_edit_text_draw_top(ui_view* view, void* user, float x1, float y1, float x2, float y2) {
    edit_text_data* data = user;
    char output[1024];
    action_edit_text_fit(output, sizeof(output), data->target->name, strlen(data->target->name), x2 - x1 - 16, 20, 0.45f, false);
    screen_draw_string(output, x1 + 8, y1 + 6, 0.45f, 0.45f, COLOR_TEXT, false);
    snprintf(output, sizeof(output), "行 %lu / %lu%s   START: 保存   L/R: 本文", (u32) data->cursor + 1, (u32) data->lineCount, data->dirty ? "  未保存" : "");
    screen_draw_string(output, x1 + 8, y1 + 24, 0.4f, 0.4f, COLOR_TEXT, false);
    float y = y1 + 43;
    float lineHeight = screen_get_font_height(0.45f);
    for(size_t line = data->cursor; line < data->lineCount && y + lineHeight <= y2 - 4; line++) {
        size_t length = action_edit_text_line_size(data, line);
        size_t skip = line == data->cursor ? data->previewPages[data->previewPage] : 0;
        size_t used = action_edit_text_fit(output, sizeof(output), data->text + data->offsets[line] + skip, length - skip,
                                         x2 - x1 - 62, y2 - y - 4, 0.45f, true);
        if(line == data->cursor) {
            data->previewNext = skip + used;
        }
        char number[24];
        snprintf(number, sizeof(number), "%lu", (u32) line + 1);
        screen_draw_string(number, x1 + 8, y, 0.4f, 0.4f, COLOR_TEXT, false);
        screen_draw_string_wrap(length == 0 ? "（空行）" : output, x1 + 52, y, 0.45f, 0.45f, COLOR_TEXT, false, x2 - 10);
        float drawnHeight = 0;
        screen_get_string_size_wrap(NULL, &drawnHeight, length == 0 ? "（空行）" : output, 0.45f, 0.45f, x2 - x1 - 62);
        y += drawnHeight + 5;
        if(used < length - skip) {
            break;
        }
    }
}

static void action_edit_text_draw_bottom(ui_view* view, void* user, float x1, float y1, float x2, float y2) {
    edit_text_data* data = user;
    float rowHeight = screen_get_font_height(0.5f) + 3;
    for(size_t line = data->scroll; line < data->lineCount; line++) {
        float y = y1 + (line - data->scroll) * rowHeight;
        if(y + rowHeight > y2) {
            break;
        }
        char number[24];
        snprintf(number, sizeof(number), "%lu", (u32) line + 1);
        screen_draw_string(number, x1 + 4, y, 0.4f, 0.4f, COLOR_TEXT, false);
        char output[512];
        size_t length = action_edit_text_line_size(data, line);
        size_t used = action_edit_text_fit(output, sizeof(output), data->text + data->offsets[line], length,
                                         x2 - x1 - 58, rowHeight, 0.5f, false);
        screen_draw_string(length == 0 ? "（空行）" : output, x1 + 48, y, 0.5f, 0.5f, COLOR_TEXT, false);
        if(used < length) {
            screen_draw_string("…", x2 - 14, y, 0.5f, 0.5f, COLOR_TEXT, false);
        }
        if(line == data->cursor) {
            u32 width = 0;
            screen_get_texture_size(&width, NULL, TEXTURE_SELECTION_OVERLAY);
            screen_draw_texture(TEXTURE_SELECTION_OVERLAY, (x1 + x2 - width) / 2, y, width, rowHeight);
        }
    }
}

static void action_edit_text_update(ui_view* view, void* user, float x1, float y1, float x2, float y2) {
    edit_text_data* data = user;
    float rowHeight = screen_get_font_height(0.5f) + 3;
    data->rows = (size_t) ((y2 - y1) / rowHeight);
    if(data->rows == 0) {
        data->rows = 1;
    }
    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();
    if((down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) || ((held & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) && osGetTime() >= data->nextMove)) {
        size_t step = (held | down) & (KEY_LEFT | KEY_RIGHT) ? data->rows : 1;
        bool previous = (held | down) & (KEY_UP | KEY_LEFT);
        action_edit_text_select(data, previous ? (data->cursor > step ? data->cursor - step : 0) : data->cursor + step);
        data->nextMove = osGetTime() + (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT) ? 400 : 80);
    }
    if(down & KEY_L) {
        if(data->previewPage > 0) {
            data->previewPage--;
        }
    } else if((down & KEY_R) && data->previewNext > data->previewPages[data->previewPage]
              && data->previewNext < action_edit_text_line_size(data, data->cursor)) {
        if(data->previewPage == 255) {
            memmove(data->previewPages, data->previewPages + 1, 255 * sizeof(size_t));
            data->previewPage--;
        }
        data->previewPages[++data->previewPage] = data->previewNext;
    }
    if(down & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        data->touching = touch.py >= y1 && touch.py < y2;
        data->touchMoved = false;
        data->touchStart = data->touchLast = touch.py;
        size_t line = data->touching ? data->scroll + (size_t) ((touch.py - y1) / rowHeight) : data->cursor;
        data->touchSelected = line == data->cursor;
        if(data->touching && line < data->lineCount) {
            action_edit_text_select(data, line);
        }
    } else if(data->touching && (held & KEY_TOUCH)) {
        touchPosition touch;
        hidTouchRead(&touch);
        int movement = (int) data->touchLast - (int) touch.py;
        int distance = (int) data->touchStart - (int) touch.py;
        if(distance > 5 || distance < -5) {
            data->touchMoved = true;
        }
        if(movement >= rowHeight || movement <= -rowHeight) {
            int scroll = (int) data->scroll + movement / rowHeight;
            size_t maximum = data->lineCount > data->rows ? data->lineCount - data->rows : 0;
            data->scroll = scroll < 0 ? 0 : (size_t) scroll > maximum ? maximum : (size_t) scroll;
            data->touchLast = touch.py;
            data->touchMoved = true;
        }
    } else if(data->touching && !(held & KEY_TOUCH)) {
        data->touching = false;
        if(data->touchMoved) {
            action_edit_text_select(data, data->scroll + data->rows / 2);
        } else if(data->touchSelected) {
            action_edit_text_keyboard(data, false);
            return;
        }
    }
    if(down & KEY_A) {
        action_edit_text_keyboard(data, false);
    } else if(down & KEY_X) {
        list_display("テキストの操作", "A: 選択  B: 戻る", data, action_edit_text_menu, NULL);
    } else if(down & KEY_Y) {
        action_edit_text_search(data);
    } else if(down & KEY_SELECT) {
        action_edit_text_undo(data);
    } else if(down & KEY_START) {
        if(action_edit_text_save(data)) {
            prompt_display_notify("完了", "テキストを保存しました。", COLOR_TEXT, NULL, NULL, NULL);
        }
    } else if(down & KEY_B) {
        if(data->dirty) {
            const char* choices[] = {"保存", "破棄", "戻る"};
            u32 buttons[] = {KEY_A, KEY_X, KEY_B};
            prompt_display_multi_choice("未保存の変更", "保存して閉じますか？", COLOR_TEXT, choices, buttons, 3, data, NULL, action_edit_text_exit_response);
        } else {
            action_edit_text_close(data);
        }
    }
}

void action_edit_text(linked_list* items, list_item* selected) {
    file_info* target = selected->data;
    if((target->attributes & FS_ATTRIBUTE_DIRECTORY) || !text_is_txt(target->name)) {
        return;
    }
    edit_text_data* data = calloc(1, sizeof(edit_text_data));
    if(data != NULL) {
        data->target = target;
        data->original = calloc(1, TEXT_FILE_MAX_SIZE + 1);
        data->text = calloc(1, TEXT_FILE_MAX_SIZE + 1);
        data->undo = calloc(1, TEXT_FILE_MAX_SIZE + 1);
        data->candidate = calloc(1, TEXT_FILE_MAX_SIZE + 1);
        data->input = calloc(1, TEXT_KEYBOARD_BUFFER_SIZE);
        data->encoded = malloc(TEXT_FILE_MAX_SIZE);
        data->offsets = malloc((TEXT_FILE_MAX_SIZE + 1) * sizeof(size_t));
        data->rows = 1;
    }
    if(data == NULL || data->original == NULL || data->text == NULL || data->undo == NULL || data->candidate == NULL
       || data->input == NULL || data->encoded == NULL || data->offsets == NULL) {
        action_edit_text_free(data);
        error_display(NULL, NULL, "メモリーの確保に失敗しました。");
        return;
    }
    Result res = R_APP_OUT_OF_MEMORY;
    FS_Path* path = fs_make_path_utf8(target->path);
    u64 size = 0;
    if(path != NULL) {
        Handle file = 0;
        if(R_SUCCEEDED(res = FSUSER_OpenFile(&file, target->archive, *path, FS_OPEN_READ, 0))) {
            if(R_SUCCEEDED(res = FSFILE_GetSize(file, &size))) {
                if(size > TEXT_FILE_MAX_SIZE) {
                    res = R_APP_OUT_OF_RANGE;
                } else if(size > 0) {
                    u32 read = 0;
                    res = FSFILE_Read(file, &read, 0, data->encoded, size);
                    if(R_SUCCEEDED(res) && read != size) {
                        res = R_APP_BAD_DATA;
                    }
                }
            }
            Result closeResult = FSFILE_Close(file);
            if(R_SUCCEEDED(res)) {
                res = closeResult;
            }
        }
        fs_free_path_utf8(path);
    }
    if(R_FAILED(res)) {
        action_edit_text_free(data);
        if(res == R_APP_OUT_OF_RANGE) {
            error_display(NULL, NULL, "編集できるテキストは32KBまでです。");
        } else {
            error_display_res(NULL, NULL, res, "テキストの読み込みに失敗しました。");
        }
        return;
    }
    if(!text_decode(data->encoded, size, data->original, TEXT_FILE_MAX_SIZE + 1, &data->format)) {
        action_edit_text_free(data);
        error_display(NULL, NULL, "UTF-8のテキストを選択してください。");
        return;
    }
    string_copy(data->text, data->original, TEXT_FILE_MAX_SIZE + 1);
    action_edit_text_reindex(data);
    ui_view* view = ui_create();
    if(view == NULL) {
        action_edit_text_free(data);
        return;
    }
    data->view = view;
    view->name = "テキスト編集";
    view->info = "A:編集 X:操作 Y:検索 B:戻る";
    view->data = data;
    view->update = action_edit_text_update;
    view->drawTop = action_edit_text_draw_top;
    view->drawBottom = action_edit_text_draw_bottom;
    if(!ui_push(view)) {
        action_edit_text_free(data);
        ui_destroy(view);
        error_display(NULL, NULL, "編集画面を開けませんでした。");
    }
}
