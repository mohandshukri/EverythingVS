#pragma once

// Application & Icon IDs
#define IDI_APP_ICON                    101
#define IDR_MAIN_MENU                   102
#define IDR_ACCEL                       103

// Control IDs
#define IDC_SEARCH_EDIT                 1001
#define IDC_FILTER_COMBO                1002
#define IDC_RESULT_LIST                 1003
#define IDC_STATUS_BAR                  1004
#define IDC_BTN_COPY_FILE               1005
#define IDC_BTN_COPY_PATH               1006
#define IDC_BTN_OPEN_PATH               1007

// Custom Window Messages
#define WM_APP_INDEX_PROGRESS           (WM_APP + 1)
#define WM_APP_INDEX_COMPLETE           (WM_APP + 2)
#define WM_APP_INDEX_UPDATED            (WM_APP + 3)
#define WM_APP_TRAY_ICON                (WM_APP + 4)
#define WM_APP_SEARCH_DONE              (WM_APP + 5)

// File Menu
#define IDM_FILE_NEW_WINDOW             2001
#define IDM_FILE_EXPORT                 2002
#define IDM_FILE_OPEN                   2003
#define IDM_FILE_OPEN_PATH              2004
#define IDM_FILE_PROPERTIES             2005
#define IDM_FILE_DELETE                 2006
#define IDM_FILE_EXIT                   2007

// Edit Menu
#define IDM_EDIT_COPY_FILES             2100
#define IDM_EDIT_COPY_FULL_PATH         2101
#define IDM_EDIT_COPY_NAME              2102
#define IDM_EDIT_SELECT_ALL             2103
#define IDM_EDIT_INVERT_SELECTION       2104
#define IDM_EDIT_COPY_FOLDER_PATH       2105

// View Menu
#define IDM_VIEW_FILTER_ALL             2201
#define IDM_VIEW_FILTER_AUDIO           2202
#define IDM_VIEW_FILTER_COMPRESSED      2203
#define IDM_VIEW_FILTER_DOCUMENT        2204
#define IDM_VIEW_FILTER_EXECUTABLE      2205
#define IDM_VIEW_FILTER_FOLDER          2206
#define IDM_VIEW_FILTER_PICTURE         2207
#define IDM_VIEW_FILTER_VIDEO           2208
#define IDM_VIEW_ALWAYS_ON_TOP          2210
#define IDM_VIEW_REFRESH                2211
#define IDM_VIEW_FULLSCREEN             2212
#define IDM_VIEW_DARK_MODE              2213
#define IDM_VIEW_LANG_EN                2214
#define IDM_VIEW_LANG_AR                2215

// Search Menu
#define IDM_SEARCH_MATCH_CASE           2301
#define IDM_SEARCH_MATCH_WHOLE_WORD     2302
#define IDM_SEARCH_MATCH_PATH           2303
#define IDM_SEARCH_REGEX                2304

// Tools Menu
#define IDM_TOOLS_HTTP_SERVER           2401
#define IDM_TOOLS_REBUILD_INDEX         2402
#define IDM_TOOLS_RUN_AS_ADMIN          2403
#define IDM_TOOLS_OPEN_INI              2404
#define IDM_TOOLS_OPEN_HTTP_URL         2405

// Help Menu
#define IDM_HELP_SYNTAX                 2501
#define IDM_HELP_ABOUT                  2502

// Tray Menu
#define IDM_TRAY_SHOW                   2601
#define IDM_TRAY_EXIT                   2602
