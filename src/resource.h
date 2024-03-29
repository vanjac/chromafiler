#pragma once
#include "dialog.h"

#define CHROMAFILER_VERSION        0,7,1,0
#define CHROMAFILER_VERSION_STRING "0.7.1\0"

#define IDR_RT_MANIFEST1 1 // must be 1 for an exe

#define IDR_ITEM_ACCEL      102
#define IDR_ITEM_MENU       111
#define IDM_NEXT_WINDOW     1001
#define IDM_PREV_WINDOW     1002
#define IDM_CLOSE_WINDOW    1003
#define IDM_REFRESH         1004
#define IDM_HELP            1005
#define IDM_PROXY_MENU      1006
#define IDM_NEW_FOLDER      1007
#define IDM_RENAME_PROXY    1008
#define IDM_SETTINGS        1009
#define IDM_PARENT_MENU     1010
#define IDM_NEW_ITEM_MENU   1011
#define IDM_VIEW_MENU       1012
#define IDM_DETACH          1013
#define IDM_CLOSE_PARENT    1014
#define IDM_NEW_TEXT_FILE   1015
#define IDM_DELETE_PROXY    1016
#define IDM_DEBUG_NAMES     1017
#define IDM_CONTEXT_MENU    1018
#define IDM_PROXY_BUTTON    1019

#define IDM_SHELL_FIRST     0x4000
#define IDM_SHELL_LAST      0x7FFF

#define IDR_TEXT_ACCEL      107
#define IDM_SAVE            1100
#define IDM_FIND            1101
#define IDM_FIND_NEXT       1102
#define IDM_FIND_PREV       1103
#define IDM_REPLACE         1104
#define IDM_WORD_WRAP       1105
#define IDM_ZOOM_IN         1106
#define IDM_ZOOM_OUT        1107
#define IDM_ZOOM_RESET      1108
#define IDM_LINE_SELECT     1109

#define IDR_TEXT_MENU       108
#define IDM_UNDO            1200
#define IDM_REDO            1201
#define IDM_CUT             1202
#define IDM_COPY            1203
#define IDM_PASTE           1204
#define IDM_DELETE          1205
#define IDM_SELECT_ALL      1206

#define IDR_ICON_FONT   103
// https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
#define MDL2_CHEVRON_LEFT_MED       L"\uE973"
#define MDL2_REFRESH                L"\uE72C"
#define MDL2_MORE                   L"\uE712"
#define MDL2_CALCULATOR_ADDITION    L"\uE948"
#define MDL2_VIEW                   L"\uE890"
#define MDL2_SAVE                   L"\uE74E"
#define MDL2_DELETE                 L"\uE74D"

#define IDC_RIGHT_SIDE  150

#define IDS_SETTINGS_CAPTION    200
#define IDS_MENU_COMMAND        201
#define IDS_REFRESH_COMMAND     202
#define IDS_NEW_ITEM_COMMAND    203
#define IDS_VIEW_COMMAND        204
#define IDS_SAVE_COMMAND        205
#define IDS_DELETE_COMMAND      206
#define IDS_UNSAVED_CAPTION     207
#define IDS_SAVE_BUTTON         208
#define IDS_DONT_SAVE_BUTTON    209
#define IDS_DELETE_BUTTON       210
#define IDS_SUCCESS_CAPTION     211
#define IDS_BROWSER_SET_FAILED  212
#define IDS_BROWSER_SET         213
#define IDS_BROWSER_RESET       214
#define IDS_REQUIRE_CONTEXT     215
#define IDS_BROWSER_SET_CONFIRM 216
#define IDS_APP_NAME            217
#define IDS_WELCOME_HEADER      218
#define IDS_WELCOME_BODY        219
#define IDS_WELCOME_TUTORIAL    220
#define IDS_WELCOME_TRAY        221
#define IDS_WELCOME_BROWSER     222
#define IDS_CONFIRM_CAPTION     223
#define IDS_NO_UPDATE_CAPTION   224
#define IDS_NO_UPDATE           225
#define IDS_UPDATE_ERROR        226
#define IDS_WELCOME_UPDATE      227
#define IDS_OPEN_PARENT_COMMAND 228
#define IDS_SAVE_ERROR          229
#define IDS_ERROR_CAPTION       230
#define IDS_CHROMATEXT          231
#define IDS_TEXT_CANT_FIND      232
#define IDS_UPDATE_NOTIF_TITLE  233
#define IDS_UPDATE_NOTIF_INFO   234
#define IDS_CANT_FIND_ITEM      235
#define IDS_FOLDER_STATUS       236
#define IDS_FOLDER_STATUS_SEL   237
#define IDS_SAVE_PROMPT         238
#define IDS_DELETE_PROMPT       239
#define IDS_TEXT_STATUS         240
#define IDS_TEXT_STATUS_SEL     241
#define IDS_TEXT_STATUS_REPLACE 242
#define IDS_TEXT_UNDO           243
#define IDS_TEXT_REDO           244
#define IDS_FONT_NAME           245
#define IDS_UNKNOWN_ERROR       246
#define IDS_LEGAL_INFO          247
#define IDS_FOLDER_ERROR        248
#define IDS_TEXT_LOADING        249
#define IDS_INVALID_CHARS       250
#define IDS_ADMIN_WARNING       251
#define IDS_DONT_ASK            252

// corresponds to UNDONAMEID
#define IDS_TEXT_UNDO_UNKNOWN   300
#define IDS_TEXT_UNDO_TYPING    301
#define IDS_TEXT_UNDO_DELETE    302
#define IDS_TEXT_UNDO_DRAGDROP  303
#define IDS_TEXT_UNDO_CUT       304
#define IDS_TEXT_UNDO_PASTE     305
#define IDS_TEXT_UNDO_AUTOTABLE 306

#define IDS_NEWLINES_CRLF       320
#define IDS_NEWLINES_LF         321
#define IDS_NEWLINES_CR         322
#define IDS_NEWLINES_COUNT      3

#define IDS_ENCODING_UTF8       330
#define IDS_ENCODING_UTF8BOM    331
#define IDS_ENCODING_UTF16LE    332
#define IDS_ENCODING_UTF16BE    333
#define IDS_ENCODING_ANSI       334
#define IDS_ENCODING_COUNT      5
