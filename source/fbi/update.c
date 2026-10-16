#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <jansson.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"

static bool update_is_newer(const char* tag, bool japanese) {
    unsigned int candidate[4] = {0};
    unsigned int current[4] = {VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, 0};
    int end = 0;
    const char* version = tag[0] == 'v' ? tag + 1 : tag;
    if(japanese) {
        if(sscanf(version, "%u.%u.%u-ja.%u%n", &candidate[0], &candidate[1], &candidate[2], &candidate[3], &end) != 4
           || version[end] != '\0'
           || sscanf(FBI_JA_VERSION, "%u.%u.%u-ja.%u", &current[0], &current[1], &current[2], &current[3]) != 4) {
            return false;
        }
    } else if(sscanf(version, "%u.%u.%u%n", &candidate[0], &candidate[1], &candidate[2], &end) != 3 || version[end] != '\0') {
        return false;
    }

    for(u32 i = 0; i < 4; i++) {
        if(candidate[i] != current[i]) {
            return candidate[i] > current[i];
        }
    }

    return false;
}

static Result update_find_release(bool japanese, bool* hasUpdate, char* updateURL) {
    const char* releaseURL = japanese ? "https://api.github.com/repos/Yu08083/FBI-JP/releases/latest"
                                      : "https://api.github.com/repos/Steveice10/FBI/releases/latest";
    Result res = 0;
    json_t* json = NULL;
    if(R_SUCCEEDED(res = http_download_json(releaseURL, &json, 64 * 1024))) {
        if(json_is_object(json)) {
            json_t* name = json_object_get(json, "tag_name");
            json_t* assets = json_object_get(json, "assets");

            if(json_is_string(name) && json_is_array(assets)) {
                if(update_is_newer(json_string_value(name), japanese)) {
                    const char* url = NULL;
                    const char* fileName = japanese ? (fs_get_3dsx_path() != NULL ? "FBI-JP.3dsx" : "FBI-JP.cia")
                                                    : (fs_get_3dsx_path() != NULL ? "FBI.3dsx" : "FBI.cia");

                    for(u32 i = 0; i < json_array_size(assets); i++) {
                        json_t* val = json_array_get(assets, i);
                        if(json_is_object(val)) {
                            json_t* assetName = json_object_get(val, "name");
                            json_t* assetUrl = json_object_get(val, "browser_download_url");

                            if(json_is_string(assetName) && json_is_string(assetUrl)) {
                                if(strcmp(json_string_value(assetName), fileName) == 0) {
                                    url = json_string_value(assetUrl);
                                    break;
                                }
                            }
                        }
                    }

                    if(url != NULL) {
                        string_copy(updateURL, url, DOWNLOAD_URL_MAX);
                        *hasUpdate = true;
                    } else {
                        res = R_APP_BAD_DATA;
                    }
                }
            } else {
                res = R_APP_BAD_DATA;
            }
        } else {
            res = R_APP_BAD_DATA;
        }

        json_decref(json);
    }

    return res == R_APP_HTTP_ERROR_BASE + 404 ? 0 : res;
}

static void update_check_update(ui_view* view, void* data, float* progress, char* text) {
    bool hasUpdate = false;
    bool upstream = false;
    char updateURL[DOWNLOAD_URL_MAX];

    Result res = update_find_release(true, &hasUpdate, updateURL);
    if(R_SUCCEEDED(res) && !hasUpdate) {
        res = update_find_release(false, &hasUpdate, updateURL);
        upstream = hasUpdate;
    }

    ui_pop();
    info_destroy(view);

    if(hasUpdate) {
        action_install_url(upstream ? "原版（英語版）FBIに更新しますか？" : "FBI日本語版を最新版に更新しますか？", updateURL, fs_get_3dsx_path(), NULL, NULL, NULL, NULL);
    } else {
        if(R_FAILED(res)) {
            error_display_res(NULL, NULL, res, "更新の確認に失敗しました。");
        } else {
            prompt_display_notify("完了", "利用できる更新はありません。", COLOR_TEXT, NULL, NULL, NULL);
        }
    }
}

void update_open() {
    info_display("更新を確認中", "", false, NULL, update_check_update, NULL);
}
