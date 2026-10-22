#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "action.h"
#include "../task/uitask.h"
#include "../../core/core.h"
#include "../../core/text.h"

typedef struct {
    file_info* target;
    text_format format;
    char* original;
    char* text;
    unsigned char* encoded;
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
        free(data->encoded);
        free(data);
    }
}

static SwkbdCallbackResult action_edit_text_validate(void* user, const char** message, const char* text, size_t length) {
    edit_text_data* data = user;
    size_t size = 0;
    if(!text_encode(text, data->format, data->encoded, TEXT_FILE_MAX_SIZE, &size)) {
        *message = "編集できるテキストは32KBまでです。";
        return SWKBD_CALLBACK_CONTINUE;
    }
    return SWKBD_CALLBACK_OK;
}

static void action_edit_text_update(ui_view* view, void* user, float x1, float y1, float x2, float y2) {
    edit_text_data* data = user;
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, TEXT_KEYBOARD_MAX_UNITS);
    swkbdSetHintText(&keyboard, "テキストを編集");
    swkbdSetInitialText(&keyboard, data->text);
    swkbdSetFeatures(&keyboard, SWKBD_MULTILINE | SWKBD_PREDICTIVE_INPUT);
    swkbdSetValidation(&keyboard, SWKBD_ANYTHING, 0, 0);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "キャンセル", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_CONFIRM, "保存", true);
    swkbdSetFilterCallback(&keyboard, action_edit_text_validate, data);
    SwkbdButton button = swkbdInputText(&keyboard, data->text, TEXT_KEYBOARD_BUFFER_SIZE);
    if(button == SWKBD_BUTTON_CONFIRM && strcmp(data->text, data->original) != 0) {
        size_t size = 0;
        if(!text_encode(data->text, data->format, data->encoded, TEXT_FILE_MAX_SIZE, &size)) {
            error_display(NULL, NULL, "編集できるテキストは32KBまでです。");
            return;
        }
        bool recovery = false;
        Result res = action_edit_text_write(data->target->archive, data->target->path, data->target->attributes, data->encoded, size, &recovery);
        if(R_FAILED(res)) {
            error_display_res(NULL, NULL, res, recovery ? "保存に失敗しました。\n元の内容は同名の.fbi-bakにあります。" : "テキストの保存に失敗しました。");
            return;
        }
        data->target->size = size;
    }
    ui_pop();
    action_edit_text_free(data);
    ui_destroy(view);
    if(button == SWKBD_BUTTON_CONFIRM) {
        prompt_display_notify("完了", "テキストを保存しました。", COLOR_TEXT, NULL, NULL, NULL);
    } else if(button == SWKBD_BUTTON_NONE) {
        error_display(NULL, NULL, "入力画面を開けませんでした。");
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
        data->text = calloc(1, TEXT_KEYBOARD_BUFFER_SIZE);
        data->encoded = malloc(TEXT_FILE_MAX_SIZE);
    }
    if(data == NULL || data->original == NULL || data->text == NULL || data->encoded == NULL) {
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
    string_copy(data->text, data->original, TEXT_KEYBOARD_BUFFER_SIZE);
    ui_view* view = ui_create();
    if(view == NULL) {
        action_edit_text_free(data);
        return;
    }
    view->name = "テキストを編集";
    view->info = "";
    view->data = data;
    view->update = action_edit_text_update;
    if(!ui_push(view)) {
        action_edit_text_free(data);
        ui_destroy(view);
        error_display(NULL, NULL, "編集画面を開けませんでした。");
    }
}
