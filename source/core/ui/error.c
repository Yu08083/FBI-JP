#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <curl/curl.h>

#include "error.h"
#include "prompt.h"
#include "../error.h"
#include "../screen.h"
#include "../../fbi/resources.h"

static const char* level_to_string(Result res) {
    switch(R_LEVEL(res)) {
        case RL_SUCCESS:
            return "完了";
        case RL_INFO:
            return "情報";
        case RL_FATAL:
            return "致命的";
        case RL_RESET:
            return "リセット";
        case RL_REINITIALIZE:
            return "再初期化";
        case RL_USAGE:
            return "使用方法";
        case RL_PERMANENT:
            return "永続的";
        case RL_TEMPORARY:
            return "一時的";
        case RL_STATUS:
            return "状態";
        default:
            return "<不明>";
    }
}

static const char* summary_to_string(Result res) {
    switch(R_SUMMARY(res)) {
        case RS_SUCCESS:
            return "完了";
        case RS_NOP:
            return "処理なし";
        case RS_WOULDBLOCK:
            return "処理待ち";
        case RS_OUTOFRESOURCE:
            return "リソース不足";
        case RS_NOTFOUND:
            return "見つかりません";
        case RS_INVALIDSTATE:
            return "状態が不正";
        case RS_NOTSUPPORTED:
            return "未対応";
        case RS_INVALIDARG:
            return "引数が不正";
        case RS_WRONGARG:
            return "引数が誤り";
        case RS_CANCELED:
            return "中止";
        case RS_STATUSCHANGED:
            return "状態が変化";
        case RS_INTERNAL:
            return "内部エラー";
        default:
            return "<不明>";
    }
}

static const char* module_to_string(Result res) {
    switch(R_MODULE(res)) {
        case RM_COMMON:
            return "共通";
        case RM_KERNEL:
            return "カーネル";
        case RM_UTIL:
            return "ユーティリティ";
        case RM_FILE_SERVER:
            return "ファイルサーバー";
        case RM_LOADER_SERVER:
            return "ローダーサーバー";
        case RM_TCB:
            return "TCB";
        case RM_OS:
            return "OS";
        case RM_DBG:
            return "DBG";
        case RM_DMNT:
            return "DMNT";
        case RM_PDN:
            return "PDN";
        case RM_GSP:
            return "GSP";
        case RM_I2C:
            return "I2C";
        case RM_GPIO:
            return "GPIO";
        case RM_DD:
            return "DD";
        case RM_CODEC:
            return "CODEC";
        case RM_SPI:
            return "SPI";
        case RM_PXI:
            return "PXI";
        case RM_FS:
            return "FS";
        case RM_DI:
            return "DI";
        case RM_HID:
            return "HID";
        case RM_CAM:
            return "CAM";
        case RM_PI:
            return "PI";
        case RM_PM:
            return "PM";
        case RM_PM_LOW:
            return "PMLOW";
        case RM_FSI:
            return "FSI";
        case RM_SRV:
            return "SRV";
        case RM_NDM:
            return "NDM";
        case RM_NWM:
            return "NWM";
        case RM_SOC:
            return "SOC";
        case RM_LDR:
            return "LDR";
        case RM_ACC:
            return "ACC";
        case RM_ROMFS:
            return "RomFS";
        case RM_AM:
            return "AM";
        case RM_HIO:
            return "HIO";
        case RM_UPDATER:
            return "更新処理";
        case RM_MIC:
            return "MIC";
        case RM_FND:
            return "FND";
        case RM_MP:
            return "MP";
        case RM_MPWL:
            return "MPWL";
        case RM_AC:
            return "AC";
        case RM_HTTP:
            return "HTTP";
        case RM_DSP:
            return "DSP";
        case RM_SND:
            return "SND";
        case RM_DLP:
            return "DLP";
        case RM_HIO_LOW:
            return "HIOLOW";
        case RM_CSND:
            return "CSND";
        case RM_SSL:
            return "SSL";
        case RM_AM_LOW:
            return "AMLOW";
        case RM_NEX:
            return "NEX";
        case RM_FRIENDS:
            return "フレンド";
        case RM_RDT:
            return "RDT";
        case RM_APPLET:
            return "アプレット";
        case RM_NIM:
            return "NIM";
        case RM_PTM:
            return "PTM";
        case RM_MIDI:
            return "MIDI";
        case RM_MC:
            return "MC";
        case RM_SWC:
            return "SWC";
        case RM_FATFS:
            return "FatFS";
        case RM_NGC:
            return "NGC";
        case RM_CARD:
            return "CARD";
        case RM_CARDNOR:
            return "CARDNOR";
        case RM_SDMC:
            return "SDMC";
        case RM_BOSS:
            return "BOSS";
        case RM_DBM:
            return "DBM";
        case RM_CONFIG:
            return "設定";
        case RM_PS:
            return "PS";
        case RM_CEC:
            return "CEC";
        case RM_IR:
            return "IR";
        case RM_UDS:
            return "UDS";
        case RM_PL:
            return "PL";
        case RM_CUP:
            return "CUP";
        case RM_GYROSCOPE:
            return "ジャイロセンサー";
        case RM_MCU:
            return "MCU";
        case RM_NS:
            return "NS";
        case RM_NEWS:
            return "NEWS";
        case RM_RO:
            return "RO";
        case RM_GD:
            return "GD";
        case RM_CARD_SPI:
            return "CARDSPI";
        case RM_EC:
            return "EC";
        case RM_WEB_BROWSER:
            return "ブラウザー";
        case RM_TEST:
            return "TEST";
        case RM_ENC:
            return "ENC";
        case RM_PIA:
            return "PIA";
        case RM_ACT:
            return "ACT";
        case RM_VCTL:
            return "VCTL";
        case RM_OLV:
            return "OLV";
        case RM_NEIA:
            return "NEIA";
        case RM_NPNS:
            return "NPNS";
        case RM_AVD:
            return "AVD";
        case RM_L2B:
            return "L2B";
        case RM_MVD:
            return "MVD";
        case RM_NFC:
            return "NFC";
        case RM_UART:
            return "UART";
        case RM_SPM:
            return "SPM";
        case RM_QTM:
            return "QTM";
        case RM_NFP:
            return "NFP";
        case RM_APPLICATION:
            return "アプリケーション";
        default:
            return "<不明>";
    }
}

static const char* description_to_string(Result res) {
    int module = R_MODULE(res);
    int description = R_DESCRIPTION(res);

    switch(module) {
        case RM_KERNEL:
            switch(description) {
                case 2:
                    return "DMAバッファーの権限が不正";
                default:
                    break;
            }

            break;
        case RM_OS:
            switch(description) {
                case 1:
                    return "同期オブジェクト不足";
                case 2:
                    return "共有メモリーオブジェクト不足";
                case 9:
                    return "セッションオブジェクト不足";
                case 10:
                    return "メモリー不足";
                case 20:
                    return "アクセス権限が不正";
                case 26:
                    return "接続先がセッションを終了";
                case 47:
                    return "コマンドヘッダーが不正";
                case 52:
                    return "ポートの接続数上限を超過";
                default:
                    break;
            }

            break;
        case RM_FS:
            switch(description) {
                case 101:
                    return "アーカイブが未マウント";
                case 120:
                    return "存在しないか、開けません";
                case 141:
                    return "ゲームカードが未挿入";
                case 171:
                    return "バスが使用中、またはデータ不足";
                case 172:
                    return "バスの機能が不正";
                case 190:
                    return "既に存在するか、作成できません";
                case 210:
                    return "パーティションの空き容量不足";
                case 230:
                    return "操作が不正、またはファイル使用中";
                case 231:
                    return "リソースがロック中";
                case 250:
                    return "FAT操作が拒否されました";
                case 265:
                    return "バスのタイムアウト";
                case 331:
                    return "バスエラー、またはTWL領域が不正";
                case 332:
                    return "バスのストップビットエラー";
                case 391:
                    return "ハッシュ検証に失敗";
                case 392:
                    return "RSA署名またはハッシュ検証に失敗";
                case 395:
                    return "RomFSまたはセーブのハッシュが不正";
                case 630:
                    return "アーカイブのアクセス権限なし";
                case 702:
                    return "パスが不正、またはアクセス不可";
                case 705:
                    return "オフセットが範囲外";
                case 721:
                    return "ファイルサイズ上限に到達";
                case 760:
                    return "未対応の操作";
                case 761:
                    return "ExeFSの読み込みサイズ不一致";
                default:
                    break;
            }

            break;
        case RM_SRV:
            switch(description) {
                case 5:
                    return "サービス名の長さが不正";
                case 6:
                    return "サービスへのアクセスが拒否";
                case 7:
                    return "文字列サイズ不一致";
                default:
                    break;
            }

            break;
        case RM_AM:
            switch(description) {
                case 4:
                    return "インストール状態が不正";
                case 37:
                    return "NCCHが不正";
                case 39:
                    return "タイトルのバージョンが不正または古い";
                case 41:
                    return "エラー種別 1";
                case 43:
                    return "データベースが存在しません";
                case 44:
                    return "システムタイトルの削除は禁止";
                case 101:
                    return "エラー種別 -1";
                case 102:
                    return "エラー種別 -2";
                case 103:
                    return "エラー種別 -3";
                case 104:
                    return "エラー種別 -4";
                case 105:
                    return "エラー種別 -5";
                case 106:
                    return "証明書の署名またはハッシュが不正";
                case 107:
                    return "エラー種別 -7";
                case 108:
                    return "エラー種別 -8";
                case 109:
                    return "エラー種別 -9";
                case 110:
                    return "エラー種別 -10";
                case 111:
                    return "エラー種別 -11";
                case 112:
                    return "エラー種別 -12";
                case 113:
                    return "エラー種別 -13";
                case 114:
                    return "エラー種別 -14";
                case 393:
                    return "データベースが不正";
                default:
                    break;
            }

            break;
        case RM_HTTP:
            switch(description) {
                case 60:
                    return "TLS証明書の検証に失敗";
                case 70:
                    return "ネットワークが利用できません";
                case 102:
                    return "コンテキストハンドルが不正";
                case 105:
                    return "要求がタイムアウト";
                default:
                    break;
            }

            break;
        case RM_SSL:
            switch(description) {
                case 20:
                    return "信頼されていないルート証明書";
                case 54:
                    return "ルート証明書のハンドルが見つかりません";
                default:
                    break;
            }

            break;
        case RM_SDMC:
            switch(description) {
                case 1:
                    return "バスのBit23エラー";
                case 2:
                    return "バスの受信準備エラー";
                case 3:
                    return "バスのBit28エラー";
                case 4:
                    return "バスのBit27エラー";
                default:
                    break;
            }

            break;
        case RM_MVD:
            switch(description) {
                case 271:
                    return "設定が不正";
                default:
                    break;
            }

            break;
        case RM_NFC:
            switch(description) {
                case 512:
                    return "NFCの状態が不正";
                default:
                    break;
            }

            break;
        case RM_QTM:
            switch(description) {
                case 8:
                    return "カメラが使用中";
                default:
                    break;
            }

            break;
        case RM_APPLICATION:
            switch(res) {
                case R_APP_INVALID_ARGUMENT:
                    return "引数が不正";
                case R_APP_CANCELLED:
                    return "処理を中止";
                case R_APP_SKIPPED:
                    return "処理をスキップ";
                case R_APP_THREAD_CREATE_FAILED:
                    return "スレッドの作成に失敗";
                case R_APP_PARSE_FAILED:
                    return "解析に失敗";
                case R_APP_BAD_DATA:
                    return "データが不正";
                case R_APP_HTTP_TOO_MANY_REDIRECTS:
                    return "リダイレクト回数が過剰";
                default:
                    if(res >= R_APP_HTTP_ERROR_BASE && res < R_APP_HTTP_ERROR_END) {
                        switch(res - R_APP_HTTP_ERROR_BASE) {
                            case 100:
                                return "HTTP 100: 続行";
                            case 101:
                                return "HTTP 101: プロトコル切替";
                            case 102:
                                return "HTTP 102: 処理中";
                            case 103:
                                return "HTTP 103: 事前情報";
                            case 200:
                                return "HTTP 200: 成功";
                            case 201:
                                return "HTTP 201: 作成済み";
                            case 202:
                                return "HTTP 202: 受付済み";
                            case 203:
                                return "HTTP 203: 非公式な情報";
                            case 204:
                                return "HTTP 204: 内容なし";
                            case 205:
                                return "HTTP 205: 内容をリセット";
                            case 206:
                                return "HTTP 206: 部分的な内容";
                            case 207:
                                return "HTTP 207: 複数の状態";
                            case 208:
                                return "HTTP 208: 報告済み";
                            case 226:
                                return "HTTP 226: 差分適用済み";
                            case 300:
                                return "HTTP 300: 複数の選択肢";
                            case 301:
                                return "HTTP 301: 恒久的に移動";
                            case 302:
                                return "HTTP 302: 移動先あり";
                            case 303:
                                return "HTTP 303: 別の場所を参照";
                            case 304:
                                return "HTTP 304: 変更なし";
                            case 305:
                                return "HTTP 305: プロキシを使用";
                            case 306:
                                return "HTTP 306: プロキシを切替";
                            case 307:
                                return "HTTP 307: 一時的に転送";
                            case 308:
                                return "HTTP 308: 恒久的に転送";
                            case 400:
                                return "HTTP 400: 要求が不正";
                            case 401:
                                return "HTTP 401: 認証が必要";
                            case 402:
                                return "HTTP 402: 支払いが必要";
                            case 403:
                                return "HTTP 403: アクセス禁止";
                            case 404:
                                return "HTTP 404: 見つかりません";
                            case 405:
                                return "HTTP 405: メソッドが未許可";
                            case 406:
                                return "HTTP 406: 応答形式が未対応";
                            case 407:
                                return "HTTP 407: プロキシ認証が必要";
                            case 408:
                                return "HTTP 408: 要求のタイムアウト";
                            case 409:
                                return "HTTP 409: 要求が競合";
                            case 410:
                                return "HTTP 410: 削除済み";
                            case 411:
                                return "HTTP 411: 長さの指定が必要";
                            case 412:
                                return "HTTP 412: 前提条件を満たしていません";
                            case 413:
                                return "HTTP 413: 送信データが過大";
                            case 414:
                                return "HTTP 414: URIが長すぎます";
                            case 415:
                                return "HTTP 415: データ形式が未対応";
                            case 416:
                                return "HTTP 416: 要求範囲が不正";
                            case 417:
                                return "HTTP 417: 期待条件を満たしていません";
                            case 418:
                                return "HTTP 418: ティーポットです";
                            case 421:
                                return "HTTP 421: 要求先が不正";
                            case 422:
                                return "HTTP 422: 処理できない内容";
                            case 423:
                                return "HTTP 423: ロック中";
                            case 424:
                                return "HTTP 424: 依存処理に失敗";
                            case 426:
                                return "HTTP 426: アップグレードが必要";
                            case 428:
                                return "HTTP 428: 前提条件が必要";
                            case 429:
                                return "HTTP 429: 要求回数が過剰";
                            case 431:
                                return "HTTP 431: 要求ヘッダーが過大";
                            case 451:
                                return "HTTP 451: 法的な理由で利用不可";
                            case 500:
                                return "HTTP 500: サーバー内部エラー";
                            case 501:
                                return "HTTP 501: 未実装";
                            case 502:
                                return "HTTP 502: ゲートウェイエラー";
                            case 503:
                                return "HTTP 503: サービス停止中";
                            case 504:
                                return "HTTP 504: ゲートウェイのタイムアウト";
                            case 505:
                                return "HTTP 505: HTTPバージョンが未対応";
                            case 506:
                                return "HTTP 506: コンテンツ交渉のエラー";
                            case 507:
                                return "HTTP 507: 保存容量不足";
                            case 508:
                                return "HTTP 508: ループを検出";
                            case 510:
                                return "HTTP 510: 拡張が必要";
                            case 511:
                                return "HTTP 511: ネットワーク認証が必要";
                            default:
                                return "HTTP: 不明な応答コード";
                        }
                    } else if(res >= R_APP_CURL_ERROR_BASE && res < R_APP_CURL_ERROR_END) {
                        return curl_easy_strerror(res - R_APP_CURL_ERROR_BASE);
                    }

                    break;
            }
        default:
            break;
    }

    switch(description) {
        case RD_SUCCESS:
            return "完了";
        case RD_TIMEOUT:
            return "タイムアウト";
        case RD_OUT_OF_RANGE:
            return "範囲外";
        case RD_ALREADY_EXISTS:
            return "既に存在します";
        case RD_CANCEL_REQUESTED:
            return "中止要求あり";
        case RD_NOT_FOUND:
            return "見つかりません";
        case RD_ALREADY_INITIALIZED:
            return "初期化済み";
        case RD_NOT_INITIALIZED:
            return "未初期化";
        case RD_INVALID_HANDLE:
            return "ハンドルが不正";
        case RD_INVALID_POINTER:
            return "ポインターが不正";
        case RD_INVALID_ADDRESS:
            return "アドレスが不正";
        case RD_NOT_IMPLEMENTED:
            return "未実装";
        case RD_OUT_OF_MEMORY:
            return "メモリー不足";
        case RD_MISALIGNED_SIZE:
            return "サイズのアライメントが不正";
        case RD_MISALIGNED_ADDRESS:
            return "アドレスのアライメントが不正";
        case RD_BUSY:
            return "使用中";
        case RD_NO_DATA:
            return "データなし";
        case RD_INVALID_COMBINATION:
            return "組み合わせが不正";
        case RD_INVALID_ENUM_VALUE:
            return "列挙値が不正";
        case RD_INVALID_SIZE:
            return "サイズが不正";
        case RD_ALREADY_DONE:
            return "処理済み";
        case RD_NOT_AUTHORIZED:
            return "権限なし";
        case RD_TOO_LARGE:
            return "サイズが過大";
        case RD_INVALID_SELECTION:
            return "選択が不正";
        default:
            return "<不明>";
    }
}

typedef struct {
    char fullText[4096];
    void* data;
    void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2);
} error_data;

static void error_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    error_data* errorData = (error_data*) data;

    if(errorData->drawTop != NULL) {
        errorData->drawTop(view, errorData->data, x1, y1, x2, y2);
    }
}

static void error_onresponse(ui_view* view, void* data, u32 response) {
    free(data);
}

ui_view* error_display(void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2), const char* text, ...) {
    error_data* errorData = (error_data*) calloc(1, sizeof(error_data));
    if(errorData == NULL) {

        return NULL;
    }

    errorData->data = data;
    errorData->drawTop = drawTop;

    va_list list;
    va_start(list, text);
    vsnprintf(errorData->fullText, 4096, text, list);
    va_end(list);

    return prompt_display_notify("エラー", errorData->fullText, COLOR_TEXT, errorData, error_draw_top, error_onresponse);
}

ui_view* error_display_res(void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2), Result result, const char* text, ...) {
    error_data* errorData = (error_data*) calloc(1, sizeof(error_data));
    if(errorData == NULL) {

        return NULL;
    }

    errorData->data = data;
    errorData->drawTop = drawTop;

    char textBuf[1024];
    va_list list;
    va_start(list, text);
    vsnprintf(textBuf, 1024, text, list);
    va_end(list);

    int level = R_LEVEL(result);
    int summary = R_SUMMARY(result);
    int module = R_MODULE(result);
    int description = R_DESCRIPTION(result);

    snprintf(errorData->fullText, 4096, "%s\n結果コード: 0x%08lX\nレベル: %s (%d)\n概要: %s (%d)\nモジュール: %s (%d)\n詳細: %s (%d)", textBuf, result, level_to_string(result), level, summary_to_string(result), summary, module_to_string(result), module, description_to_string(result), description);

    return prompt_display_notify("エラー", errorData->fullText, COLOR_TEXT, errorData, error_draw_top, error_onresponse);
}

ui_view* error_display_errno(void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2), int err, const char* text, ...) {
    error_data* errorData = (error_data*) calloc(1, sizeof(error_data));
    if(errorData == NULL) {

        return NULL;
    }

    errorData->data = data;
    errorData->drawTop = drawTop;

    char textBuf[1024];
    va_list list;
    va_start(list, text);
    vsnprintf(textBuf, 1024, text, list);
    va_end(list);

    if(err < 0) {
        err = -err;
    }

    snprintf(errorData->fullText, 4096, "%s\n入出力エラー: %s (%d)", textBuf, strerror(err), err);

    return prompt_display_notify("エラー", errorData->fullText, COLOR_TEXT, errorData, error_draw_top, error_onresponse);
}