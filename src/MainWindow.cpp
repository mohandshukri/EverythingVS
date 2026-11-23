#include "MainWindow.h"
#include "resource.h"
#include <commdlg.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <chrono>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comdlg32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static constexpr wchar_t kWndClass[] = L"EverythingVSMainWindow";
static constexpr UINT_PTR kProgressTimerId = 102;
static constexpr UINT_PTR kToastTimerId    = 103;

static constexpr COLORREF kDarkBg   = RGB(24, 24, 27);
static constexpr COLORREF kDarkEdit = RGB(36, 36, 42);
static constexpr COLORREF kDarkText = RGB(241, 245, 249);

MainWindow::MainWindow(HINSTANCE hInst)
    : m_hInst(hInst), m_httpServer(m_index), m_activeSnap(std::make_shared<const IndexData>()) {}

MainWindow::~MainWindow() {
    StopSearchWorker();
    if (m_uiFont) DeleteObject(m_uiFont);
    if (m_darkBgBrush) DeleteObject(m_darkBgBrush);
    if (m_darkEditBrush) DeleteObject(m_darkEditBrush);
    if (m_hIconBig) DestroyIcon(m_hIconBig);
    if (m_hIconSmall) DestroyIcon(m_hIconSmall);
}

bool MainWindow::Create(int nCmdShow) {
    m_config.Load();
    m_searchOpts.matchCase      = m_config.match_case;
    m_searchOpts.matchWholeWord = m_config.match_whole_word;
    m_searchOpts.matchPath      = m_config.match_path;
    m_searchOpts.matchRegex     = m_config.match_regex;
    m_searchOpts.sortColumn     = m_config.sort_column;
    m_searchOpts.sortAscending  = m_config.sort_ascending;

    m_darkBgBrush   = CreateSolidBrush(kDarkBg);
    m_darkEditBrush = CreateSolidBrush(kDarkEdit);

    m_hIconBig = static_cast<HICON>(LoadImageW(
        m_hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR
    ));
    m_hIconSmall = static_cast<HICON>(LoadImageW(
        m_hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR
    ));

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = m_hInst;
    wc.hIcon         = m_hIconBig ? m_hIconBig : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAIN_MENU);
    wc.lpszClassName = kWndClass;
    wc.hIconSm       = m_hIconSmall ? m_hIconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD exStyle = m_config.always_on_top ? WS_EX_TOPMOST : 0;
    m_hwnd = CreateWindowExW(
        exStyle,
        kWndClass,
        L"EverythingVS",
        WS_OVERLAPPEDWINDOW,
        m_config.window_x, m_config.window_y,
        m_config.window_w, m_config.window_h,
        nullptr,
        nullptr,
        m_hInst,
        this
    );
    if (!m_hwnd) return false;

    if (m_hIconBig)   SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_hIconBig));
    if (m_hIconSmall) SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_hIconSmall));

    ApplyTheme();
    ApplyLanguage();

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->WndProc(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::SearchEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    auto* self = reinterpret_cast<MainWindow*>(dwRefData);
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_DOWN) {
            SetFocus(self->m_listView);
            if (ListView_GetItemCount(self->m_listView) > 0 && ListView_GetNextItem(self->m_listView, -1, LVNI_SELECTED) == -1) {
                ListView_SetItemState(self->m_listView, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (GetWindowTextLengthW(hwnd) > 0) {
                SetWindowTextW(hwnd, L"");
            } else {
                ShowWindow(self->m_hwnd, SW_HIDE);
            }
            return 0;
        }
        if (wParam == VK_RETURN) {
            if (ListView_GetItemCount(self->m_listView) > 0) {
                if (ListView_GetNextItem(self->m_listView, -1, LVNI_SELECTED) == -1) {
                    ListView_SetItemState(self->m_listView, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
                if (GetKeyState(VK_CONTROL) & 0x8000) self->OpenSelectedPath();
                else self->OpenSelected();
            }
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void MainWindow::StartSearchWorker() {
    m_searchStop = false;
    m_searchThread = std::thread(&MainWindow::SearchWorkerLoop, this);
}

void MainWindow::StopSearchWorker() {
    {
        std::lock_guard<std::mutex> lock(m_searchReqMutex);
        m_searchStop = true;
        m_searchGen.fetch_add(1, std::memory_order_relaxed);
    }
    m_searchCv.notify_all();
    if (m_searchThread.joinable()) m_searchThread.join();
}

void MainWindow::SearchWorkerLoop() {
    while (true) {
        std::wstring query;
        SearchOptions opts;
        uint64_t myGen = 0;

        {
            std::unique_lock<std::mutex> lock(m_searchReqMutex);
            m_searchCv.wait(lock, [&]() { return m_searchStop || m_searchPending; });
            if (m_searchStop) break;
            query = m_pendingQuery;
            opts = m_pendingOpts;
            myGen = m_searchGen.load(std::memory_order_relaxed);
            m_searchPending = false;
        }

        auto t0 = std::chrono::steady_clock::now();
        IndexDataPtr snap = m_index.GetSnapshot();
        std::vector<uint32_t> hits;
        if (snap && !snap->entries.empty()) {
            hits = SearchEngine::Execute(*snap, query, opts, &m_searchGen, myGen);
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (m_searchGen.load(std::memory_order_relaxed) != myGen) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_searchResMutex);
            m_completedSnap = std::move(snap);
            m_completedIndices = std::move(hits);
            m_completedMs = elapsedMs;
            m_completedGen = myGen;
        }
        if (m_hwnd) {
            PostMessageW(m_hwnd, WM_APP_SEARCH_DONE, 0, 0);
        }
    }
}

void MainWindow::TriggerSearch() {
    int len = GetWindowTextLengthW(m_searchEdit);
    std::wstring query(len, L'\0');
    if (len > 0) GetWindowTextW(m_searchEdit, query.data(), len + 1);

    {
        std::lock_guard<std::mutex> lock(m_searchReqMutex);
        m_pendingQuery = std::move(query);
        m_pendingOpts = m_searchOpts;
        m_searchGen.fetch_add(1, std::memory_order_relaxed);
        m_searchPending = true;
    }
    m_searchCv.notify_one();
}

void MainWindow::ApplyCompletedSearch() {
    bool snapChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_searchResMutex);
        if (m_completedGen != m_searchGen.load(std::memory_order_relaxed)) {
            return;
        }
        if (m_activeSnap.get() != m_completedSnap.get()) {
            snapChanged = true;
        }
        m_activeSnap = std::move(m_completedSnap);
        m_filteredIndices = std::move(m_completedIndices);
        m_lastSearchMs = m_completedMs;
    }

    if (snapChanged) {
        m_metaCache.clear();
    }

    ListView_SetItemCountEx(m_listView, static_cast<int>(m_filteredIndices.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(m_listView, nullptr, FALSE);
    UpdateActionButtons();
    UpdateStatusBar();
}

void MainWindow::OnCreate() {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_uiFont = CreateFontIndirectW(&ncm.lfMessageFont);

    m_searchEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 100, 26,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SEARCH_EDIT), m_hInst, nullptr
    );
    SendMessageW(m_searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    SetWindowSubclass(m_searchEdit, SearchEditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    m_filterCombo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 125, 220,
        m_hwnd, reinterpret_cast<HMENU>(IDC_FILTER_COMBO), m_hInst, nullptr
    );
    SendMessageW(m_filterCombo, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

    m_btnCopyFile = CreateWindowExW(
        0, L"BUTTON", L"Copy File",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 88, 26,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_COPY_FILE), m_hInst, nullptr
    );
    SendMessageW(m_btnCopyFile, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

    m_btnCopyPath = CreateWindowExW(
        0, L"BUTTON", L"Copy Path",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 88, 26,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_COPY_PATH), m_hInst, nullptr
    );
    SendMessageW(m_btnCopyPath, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

    m_btnOpenPath = CreateWindowExW(
        0, L"BUTTON", L"Open Folder",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 96, 26,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_OPEN_PATH), m_hInst, nullptr
    );
    SendMessageW(m_btnOpenPath, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

    m_listView = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
        0, 0, 100, 100,
        m_hwnd, reinterpret_cast<HMENU>(IDC_RESULT_LIST), m_hInst, nullptr
    );
    SendMessageW(m_listView, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    ListView_SetExtendedListViewStyle(
        m_listView,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP
    );
    SetWindowTheme(m_listView, L"Explorer", nullptr);

    SHFILEINFOW sfi = {};
    HIMAGELIST hSysImg = reinterpret_cast<HIMAGELIST>(
        SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON)
    );
    if (hSysImg) {
        ListView_SetImageList(m_listView, hSysImg, LVSIL_SMALL);
    }
    SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    m_folderIconIdx = sfi.iIcon;
    SHGetFileInfoW(L"file", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    m_genericFileIconIdx = sfi.iIcon;

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    col.fmt = LVCFMT_LEFT;  col.cx = m_config.col_name_w; col.pszText = const_cast<LPWSTR>(L"Name");          ListView_InsertColumn(m_listView, 0, &col);
    col.fmt = LVCFMT_LEFT;  col.cx = m_config.col_path_w; col.pszText = const_cast<LPWSTR>(L"Path");          ListView_InsertColumn(m_listView, 1, &col);
    col.fmt = LVCFMT_RIGHT; col.cx = m_config.col_size_w; col.pszText = const_cast<LPWSTR>(L"Size");          ListView_InsertColumn(m_listView, 2, &col);
    col.fmt = LVCFMT_LEFT;  col.cx = m_config.col_date_w; col.pszText = const_cast<LPWSTR>(L"Date Modified"); ListView_InsertColumn(m_listView, 3, &col);

    m_statusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        m_hwnd, reinterpret_cast<HMENU>(IDC_STATUS_BAR), m_hInst, nullptr
    );
    SendMessageW(m_statusBar, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

    ShowTrayIcon();
    UpdateActionButtons();
    UpdateMenuChecks();
    UpdateSortArrow();
    StartSearchWorker();

    // Instant bulk DB load (<0.2s for 2,000,000 files!)
    std::wstring dbPath = AppConfig::GetAppDataDirectory() + L"\\Everything.db";
    if (m_index.LoadDatabase(dbPath)) {
        TriggerSearch();
    }

    m_index.StartIndexing(m_hwnd);
    SetTimer(m_hwnd, kProgressTimerId, 120, nullptr);

    if (m_config.allow_http_server) {
        m_httpServer.Start(m_config.http_server_port);
        UpdateMenuChecks();
    }

    SetFocus(m_searchEdit);
    m_initialFocusDone = true;
}

void MainWindow::OnSize(int width, int height) {
    SendMessageW(m_statusBar, WM_SIZE, 0, 0);
    RECT sbRect = {};
    GetWindowRect(m_statusBar, &sbRect);
    int sbH = sbRect.bottom - sbRect.top;

    int parts[3] = { width - 290, width - 130, -1 };
    SendMessageW(m_statusBar, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));

    constexpr int pad = 5;
    constexpr int topPad = 5;
    constexpr int barH = 26;
    constexpr int comboW = 122;
    constexpr int btnCopyFileW = 86;
    constexpr int btnCopyPathW = 86;
    constexpr int btnOpenW = 94;

    int rightBlock = comboW + btnCopyFileW + btnCopyPathW + btnOpenW + pad * 5;
    int editW = width - rightBlock - pad;
    if (editW < 140) editW = 140;

    int x = pad;
    MoveWindow(m_searchEdit, x, topPad, editW, barH, TRUE);
    x += editW + pad;
    MoveWindow(m_filterCombo, x, topPad, comboW, 240, TRUE);
    x += comboW + pad;
    MoveWindow(m_btnCopyFile, x, topPad, btnCopyFileW, barH, TRUE);
    x += btnCopyFileW + pad;
    MoveWindow(m_btnCopyPath, x, topPad, btnCopyPathW, barH, TRUE);
    x += btnCopyPathW + pad;
    MoveWindow(m_btnOpenPath, x, topPad, btnOpenW, barH, TRUE);

    int listY = topPad + barH + 5;
    int listH = height - listY - sbH;
    if (listH < 0) listH = 0;
    MoveWindow(m_listView, 0, listY, width, listH, TRUE);
}

void MainWindow::UpdateActionButtons() {
    BOOL hasSel = (GetFirstSelectedRow() >= 0) ? TRUE : FALSE;
    if (m_btnCopyFile) EnableWindow(m_btnCopyFile, hasSel);
    if (m_btnCopyPath) EnableWindow(m_btnCopyPath, hasSel);
    if (m_btnOpenPath) EnableWindow(m_btnOpenPath, hasSel);
}

void MainWindow::ShowStatusToast(const std::wstring& message) {
    m_toastMessage = message;
    KillTimer(m_hwnd, kToastTimerId);
    SetTimer(m_hwnd, kToastTimerId, 2500, nullptr);
    UpdateStatusBar();
}

void MainWindow::ApplyTheme() {
    BOOL dark = m_config.dark_mode ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    if (m_listView) {
        if (dark) {
            SetWindowTheme(m_listView, L"DarkMode_Explorer", nullptr);
            ListView_SetBkColor(m_listView, kDarkBg);
            ListView_SetTextBkColor(m_listView, kDarkBg);
            ListView_SetTextColor(m_listView, kDarkText);
        } else {
            SetWindowTheme(m_listView, L"Explorer", nullptr);
            ListView_SetBkColor(m_listView, GetSysColor(COLOR_WINDOW));
            ListView_SetTextBkColor(m_listView, GetSysColor(COLOR_WINDOW));
            ListView_SetTextColor(m_listView, GetSysColor(COLOR_WINDOWTEXT));
        }
        InvalidateRect(m_listView, nullptr, TRUE);
    }

    if (m_btnCopyFile) SetWindowTheme(m_btnCopyFile, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (m_btnCopyPath) SetWindowTheme(m_btnCopyPath, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (m_btnOpenPath) SetWindowTheme(m_btnOpenPath, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (m_filterCombo) SetWindowTheme(m_filterCombo, dark ? L"DarkMode_CFD" : L"Explorer", nullptr);

    InvalidateRect(m_hwnd, nullptr, TRUE);
    UpdateMenuChecks();
}

void MainWindow::ApplyLanguage() {
    const bool ar = IsArabic();

    if (m_searchEdit) {
        const wchar_t* cue = ar
            ? L"ابحث في الملفات والمجلدات (يدعم ext:, size:, folder:, !, |, *, regex:)..."
            : L"Search files & folders (supports ext:, size:, folder:, file:, !, |, *, regex:)...";
        SendMessageW(m_searchEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
    }

    if (m_btnCopyFile) SetWindowTextW(m_btnCopyFile, ar ? L"نسخ الملف"  : L"Copy File");
    if (m_btnCopyPath) SetWindowTextW(m_btnCopyPath, ar ? L"نسخ المسار" : L"Copy Path");
    if (m_btnOpenPath) SetWindowTextW(m_btnOpenPath, ar ? L"فتح المجلد" : L"Open Folder");

    if (m_filterCombo) {
        int curSel = static_cast<int>(m_searchOpts.filter);
        SendMessageW(m_filterCombo, CB_RESETCONTENT, 0, 0);
        const wchar_t* kEn[] = {
            L"Everything", L"Audio", L"Compressed", L"Document",
            L"Executable", L"Folder", L"Picture", L"Video"
        };
        const wchar_t* kAr[] = {
            L"الكل", L"صوتيات", L"مضغوط", L"مستندات",
            L"تنفيذي", L"مجلدات", L"صور", L"فيديو"
        };
        const wchar_t** items = ar ? kAr : kEn;
        for (int i = 0; i < 8; ++i) {
            SendMessageW(m_filterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
        }
        SendMessageW(m_filterCombo, CB_SETCURSEL, curSel, 0);
    }

    if (m_listView) {
        const wchar_t* colsEn[4] = { L"Name", L"Path", L"Size", L"Date Modified" };
        const wchar_t* colsAr[4] = { L"الاسم", L"المسار", L"الحجم", L"تاريخ التعديل" };
        const wchar_t** cols = ar ? colsAr : colsEn;
        for (int i = 0; i < 4; ++i) {
            LVCOLUMNW c = {};
            c.mask = LVCF_TEXT;
            c.pszText = const_cast<LPWSTR>(cols[i]);
            ListView_SetColumn(m_listView, i, &c);
        }
    }

    UpdateMenuChecks();
    UpdateStatusBar();
}

void MainWindow::UpdateStatusBar() {
    const bool ar = IsArabic();
    std::wostringstream left;
    if (!m_toastMessage.empty()) {
        left << m_toastMessage;
    } else if (m_index.IsIndexing()) {
        if (ar) left << L"جارٍ فحص الأقراص... (" << m_index.IndexedCount() << L" عنصر)";
        else    left << L"Scanning drives... (" << m_index.IndexedCount() << L" objects)";
    } else {
        if (ar) left << m_filteredIndices.size() << L" عنصر";
        else    left << m_filteredIndices.size() << L" objects";
        int selCount = ListView_GetSelectedCount(m_listView);
        if (selCount > 0) {
            if (ar) left << L" (" << selCount << L" محدد)";
            else    left << L" (" << selCount << L" selected)";
        }
        left << std::fixed << std::setprecision(1) << L"  (" << m_lastSearchMs << L" ms)";
        if (m_index.UsedNtfsMft()) {
            left << L"  [NTFS MFT]";
        }
    }
    SendMessageW(m_statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(left.str().c_str()));

    std::wstring mods;
    if (m_searchOpts.matchCase)      mods += L"CASE ";
    if (m_searchOpts.matchWholeWord) mods += L"WORD ";
    if (m_searchOpts.matchPath)      mods += L"PATH ";
    if (m_searchOpts.matchRegex)     mods += L"REGEX ";
    SendMessageW(m_statusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(mods.c_str()));

    std::wstring httpStatus = m_httpServer.IsRunning()
        ? (L"HTTP :" + std::to_wstring(m_httpServer.Port()))
        : (ar ? L"HTTP متوقف" : L"HTTP Off");
    SendMessageW(m_statusBar, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(httpStatus.c_str()));
}

void MainWindow::UpdateMenuChecks() {
    HMENU hMenu = GetMenu(m_hwnd);
    if (!hMenu) return;
    CheckMenuItem(hMenu, IDM_SEARCH_MATCH_CASE,       MF_BYCOMMAND | (m_searchOpts.matchCase ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_SEARCH_MATCH_WHOLE_WORD, MF_BYCOMMAND | (m_searchOpts.matchWholeWord ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_SEARCH_MATCH_PATH,       MF_BYCOMMAND | (m_searchOpts.matchPath ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_SEARCH_REGEX,            MF_BYCOMMAND | (m_searchOpts.matchRegex ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_ALWAYS_ON_TOP,      MF_BYCOMMAND | (m_config.always_on_top ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_DARK_MODE,          MF_BYCOMMAND | (m_config.dark_mode ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_TOOLS_HTTP_SERVER,       MF_BYCOMMAND | (m_httpServer.IsRunning() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(hMenu, IDM_VIEW_FILTER_ALL, IDM_VIEW_FILTER_VIDEO,
                       IDM_VIEW_FILTER_ALL + static_cast<UINT>(m_searchOpts.filter), MF_BYCOMMAND);
    CheckMenuRadioItem(hMenu, IDM_VIEW_LANG_EN, IDM_VIEW_LANG_AR,
                       IsArabic() ? IDM_VIEW_LANG_AR : IDM_VIEW_LANG_EN, MF_BYCOMMAND);
}

void MainWindow::UpdateSortArrow() {
    HWND hHeader = ListView_GetHeader(m_listView);
    if (!hHeader) return;
    for (int i = 0; i < 4; ++i) {
        HDITEMW hdi = {};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hHeader, i, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == m_searchOpts.sortColumn) {
            hdi.fmt |= m_searchOpts.sortAscending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(hHeader, i, &hdi);
    }
}

void MainWindow::ShowTrayIcon() {
    m_nid = {};
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_APP_TRAY_ICON;
    m_nid.hIcon = m_hIconSmall ? m_hIconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(m_nid.szTip, L"EverythingVS - Instant File Search");
    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
}

void MainWindow::RemoveTrayIcon() {
    if (m_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_trayAdded = false;
    }
}

const MainWindow::LazyMeta& MainWindow::GetOrFetchMeta(const IndexData& data, uint32_t idx) {
    auto it = m_metaCache.find(idx);
    if (it != m_metaCache.end()) return it->second;

    if (m_metaCache.size() > 4096) m_metaCache.clear();

    LazyMeta meta;
    std::wstring full = data.GetFullPath(idx);
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fad)) {
        meta.size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        meta.dateModified = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) | fad.ftLastWriteTime.dwLowDateTime;
        meta.valid = true;
    }
    return m_metaCache.emplace(idx, meta).first->second;
}

int MainWindow::GetIconIndex(const IndexData& data, uint32_t idx) {
    const auto& e = data.entries[idx];
    if (e.isFolder) return m_folderIconIdx;
    const wchar_t* extPtr = SearchEngine::GetExtensionPtr(data.LowerPtr(idx), e.nameLen);
    if (!extPtr) return m_genericFileIconIdx;

    std::wstring ext(extPtr);
    auto it = m_extIconCache.find(ext);
    if (it != m_extIconCache.end()) return it->second;

    std::wstring dummy = L"file." + ext;
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(dummy.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES)) {
        m_extIconCache[ext] = sfi.iIcon;
        return sfi.iIcon;
    }
    return m_genericFileIconIdx;
}

std::wstring MainWindow::FormatSize(uint64_t bytes, bool isFolder) {
    if (isFolder || bytes == 0) return L"";
    wchar_t buf[64] = {};
    if (bytes < 1024) swprintf_s(buf, L"%llu B", bytes);
    else if (bytes < 1024ULL * 1024) swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ULL * 1024 * 1024) swprintf_s(buf, L"%.2f MB", bytes / (1024.0 * 1024.0));
    else swprintf_s(buf, L"%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

std::wstring MainWindow::FormatFileTime(uint64_t ft64) {
    if (ft64 == 0) return L"";
    FILETIME ft = { static_cast<DWORD>(ft64 & 0xFFFFFFFF), static_cast<DWORD>(ft64 >> 32) };
    FILETIME localFt = {};
    SYSTEMTIME st = {};
    FileTimeToLocalFileTime(&ft, &localFt);
    FileTimeToSystemTime(&localFt, &st);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

int MainWindow::GetFirstSelectedRow() const {
    return ListView_GetNextItem(m_listView, -1, LVNI_SELECTED);
}

std::vector<int> MainWindow::GetSelectedRows() const {
    std::vector<int> rows;
    int idx = -1;
    while ((idx = ListView_GetNextItem(m_listView, idx, LVNI_SELECTED)) != -1) {
        rows.push_back(idx);
    }
    return rows;
}

void MainWindow::OpenSelected() {
    int row = GetFirstSelectedRow();
    if (!m_activeSnap || row < 0 || static_cast<size_t>(row) >= m_filteredIndices.size()) return;
    uint32_t idx = m_filteredIndices[row];
    std::wstring full = m_activeSnap->GetFullPath(idx);
    std::wstring dir  = m_activeSnap->GetParentPath(idx);
    ShellExecuteW(m_hwnd, L"open", full.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

void MainWindow::OpenSelectedPath() {
    int row = GetFirstSelectedRow();
    if (!m_activeSnap || row < 0 || static_cast<size_t>(row) >= m_filteredIndices.size()) return;
    uint32_t idx = m_filteredIndices[row];
    std::wstring args = L"/select,\"" + m_activeSnap->GetFullPath(idx) + L"\"";
    ShellExecuteW(m_hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void MainWindow::ShowSelectedProperties() {
    int row = GetFirstSelectedRow();
    if (!m_activeSnap || row < 0 || static_cast<size_t>(row) >= m_filteredIndices.size()) return;
    std::wstring full = m_activeSnap->GetFullPath(m_filteredIndices[row]);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = m_hwnd;
    sei.lpVerb = L"properties";
    sei.lpFile = full.c_str();
    ShellExecuteExW(&sei);
}

void MainWindow::DeleteSelectedToRecycleBin() {
    auto rows = GetSelectedRows();
    if (!m_activeSnap || rows.empty()) return;
    std::wstring doubleNull;
    for (int r : rows) {
        if (static_cast<size_t>(r) < m_filteredIndices.size()) {
            doubleNull += m_activeSnap->GetFullPath(m_filteredIndices[r]);
            doubleNull.push_back(L'\0');
        }
    }
    doubleNull.push_back(L'\0');

    SHFILEOPSTRUCTW op = {};
    op.hwnd = m_hwnd;
    op.wFunc = FO_DELETE;
    op.pFrom = doubleNull.c_str();
    op.fFlags = FOF_ALLOWUNDO;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) {
        m_index.StartIndexing(m_hwnd);
    }
}

void MainWindow::CopySelectedFilesToClipboard() {
    auto rows = GetSelectedRows();
    if (!m_activeSnap || rows.empty()) return;

    std::wstring doubleNull;
    size_t validCount = 0;
    for (int r : rows) {
        if (static_cast<size_t>(r) < m_filteredIndices.size()) {
            doubleNull += m_activeSnap->GetFullPath(m_filteredIndices[r]);
            doubleNull.push_back(L'\0');
            ++validCount;
        }
    }
    if (validCount == 0) return;
    doubleNull.push_back(L'\0');

    size_t dropBytes = sizeof(DROPFILES) + doubleNull.size() * sizeof(wchar_t);
    HGLOBAL hDrop = GlobalAlloc(GHND, dropBytes);
    if (!hDrop) return;

    auto* df = static_cast<DROPFILES*>(GlobalLock(hDrop));
    if (!df) {
        GlobalFree(hDrop);
        return;
    }
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    memcpy(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES), doubleNull.data(), doubleNull.size() * sizeof(wchar_t));
    GlobalUnlock(hDrop);

    HGLOBAL hEffect = GlobalAlloc(GHND, sizeof(DWORD));
    if (hEffect) {
        auto* eff = static_cast<DWORD*>(GlobalLock(hEffect));
        if (eff) {
            *eff = DROPEFFECT_COPY;
            GlobalUnlock(hEffect);
        } else {
            GlobalFree(hEffect);
            hEffect = nullptr;
        }
    }

    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_HDROP, hDrop)) {
            GlobalFree(hDrop);
        }
        if (hEffect) {
            UINT cfDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            if (!cfDropEffect || !SetClipboardData(cfDropEffect, hEffect)) {
                GlobalFree(hEffect);
            }
        }
        CloseClipboard();

        if (IsArabic()) {
            ShowStatusToast(L"تم نسخ " + std::to_wstring(validCount) + L" ملف إلى الحافظة (جاهز للصق Ctrl+V)");
        } else {
            ShowStatusToast(L"Copied " + std::to_wstring(validCount) + (validCount == 1 ? L" file to clipboard (ready to paste)" : L" files to clipboard (ready to paste)"));
        }
    } else {
        GlobalFree(hDrop);
        if (hEffect) GlobalFree(hEffect);
    }
}

void MainWindow::CopySelectedTextToClipboard(CopyTextMode mode) {
    auto rows = GetSelectedRows();
    if (!m_activeSnap || rows.empty()) return;

    std::wstring text;
    size_t validCount = 0;
    for (int r : rows) {
        if (static_cast<size_t>(r) >= m_filteredIndices.size()) continue;
        uint32_t idx = m_filteredIndices[r];
        if (validCount > 0) text += L"\r\n";
        switch (mode) {
            case CopyTextMode::FullPath:
                text += m_activeSnap->GetFullPath(idx);
                break;
            case CopyTextMode::FileName:
                text.append(m_activeSnap->NamePtr(idx), m_activeSnap->entries[idx].nameLen);
                break;
            case CopyTextMode::FolderPath:
                text += m_activeSnap->GetParentPath(idx);
                break;
        }
        ++validCount;
    }
    if (validCount == 0) return;

    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* ptr = GlobalLock(hMem);
            if (ptr) {
                memcpy(ptr, text.c_str(), bytes);
                GlobalUnlock(hMem);
                if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                    GlobalFree(hMem);
                }
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();

        const bool ar = IsArabic();
        if (mode == CopyTextMode::FullPath) {
            ShowStatusToast(ar ? L"تم نسخ المسار الكامل إلى الحافظة" : L"Copied full path to clipboard");
        } else if (mode == CopyTextMode::FileName) {
            ShowStatusToast(ar ? L"تم نسخ اسم الملف إلى الحافظة" : L"Copied file name to clipboard");
        } else {
            ShowStatusToast(ar ? L"تم نسخ مسار المجلد إلى الحافظة" : L"Copied folder path to clipboard");
        }
    }
}

void MainWindow::ExportResultsEfu() {
    wchar_t fileBuf[MAX_PATH] = L"EverythingResults.efu";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Everything File List (*.efu)\0*.efu\0CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"efu";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) {
        if (m_index.ExportEfu(fileBuf, m_activeSnap, m_filteredIndices)) {
            ShowStatusToast(IsArabic() ? L"تم تصدير النتائج بنجاح" : L"Exported search results");
        }
    }
}

void MainWindow::ToggleFullscreen() {
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    if (!m_fullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(m_hwnd, &m_prevPlacement) && GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongPtrW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(m_hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = true;
        }
    } else {
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_hwnd, &m_prevPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_fullscreen = false;
    }
}

void MainWindow::RelaunchAsAdmin() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(m_hwnd, L"runas", exe, nullptr, nullptr, SW_SHOWNORMAL)) > 32) {
        DestroyWindow(m_hwnd);
    }
}

void MainWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    const int id = LOWORD(wParam);
    const int code = HIWORD(wParam);

    if (id == IDC_SEARCH_EDIT && code == EN_CHANGE) {
        TriggerSearch();
        return;
    }
    if (id == IDC_FILTER_COMBO && code == CBN_SELCHANGE) {
        int sel = static_cast<int>(SendMessageW(m_filterCombo, CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel <= static_cast<int>(FilterCategory::Video)) {
            m_searchOpts.filter = static_cast<FilterCategory>(sel);
            UpdateMenuChecks();
            TriggerSearch();
        }
        return;
    }
    if (id == IDC_BTN_COPY_FILE && code == BN_CLICKED) {
        CopySelectedFilesToClipboard();
        return;
    }
    if (id == IDC_BTN_COPY_PATH && code == BN_CLICKED) {
        CopySelectedTextToClipboard(CopyTextMode::FullPath);
        return;
    }
    if (id == IDC_BTN_OPEN_PATH && code == BN_CLICKED) {
        OpenSelectedPath();
        return;
    }

    switch (id) {
        case IDM_FILE_NEW_WINDOW: {
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            ShellExecuteW(m_hwnd, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case IDM_FILE_EXPORT:      ExportResultsEfu(); break;
        case IDM_FILE_OPEN:        OpenSelected(); break;
        case IDM_FILE_OPEN_PATH:   OpenSelectedPath(); break;
        case IDM_FILE_PROPERTIES:  ShowSelectedProperties(); break;
        case IDM_FILE_DELETE:      DeleteSelectedToRecycleBin(); break;
        case IDM_FILE_EXIT:
        case IDM_TRAY_EXIT:        DestroyWindow(m_hwnd); break;

        case IDM_TRAY_SHOW:
            ShowWindow(m_hwnd, SW_SHOW);
            SetForegroundWindow(m_hwnd);
            SetFocus(m_searchEdit);
            break;

        case IDM_EDIT_COPY_FILES:       CopySelectedFilesToClipboard(); break;
        case IDM_EDIT_COPY_FULL_PATH:   CopySelectedTextToClipboard(CopyTextMode::FullPath); break;
        case IDM_EDIT_COPY_NAME:        CopySelectedTextToClipboard(CopyTextMode::FileName); break;
        case IDM_EDIT_COPY_FOLDER_PATH: CopySelectedTextToClipboard(CopyTextMode::FolderPath); break;
        case IDM_EDIT_SELECT_ALL:
            if (GetFocus() == m_searchEdit) {
                SendMessageW(m_searchEdit, EM_SETSEL, 0, -1);
            } else {
                ListView_SetItemState(m_listView, -1, LVIS_SELECTED, LVIS_SELECTED);
                UpdateActionButtons();
                UpdateStatusBar();
            }
            break;
        case IDM_EDIT_INVERT_SELECTION: {
            int count = ListView_GetItemCount(m_listView);
            for (int i = 0; i < count; ++i) {
                UINT st = ListView_GetItemState(m_listView, i, LVIS_SELECTED);
                ListView_SetItemState(m_listView, i, (st & LVIS_SELECTED) ? 0 : LVIS_SELECTED, LVIS_SELECTED);
            }
            UpdateActionButtons();
            UpdateStatusBar();
            break;
        }

        case IDM_VIEW_FILTER_ALL:
        case IDM_VIEW_FILTER_AUDIO:
        case IDM_VIEW_FILTER_COMPRESSED:
        case IDM_VIEW_FILTER_DOCUMENT:
        case IDM_VIEW_FILTER_EXECUTABLE:
        case IDM_VIEW_FILTER_FOLDER:
        case IDM_VIEW_FILTER_PICTURE:
        case IDM_VIEW_FILTER_VIDEO: {
            int idx = id - IDM_VIEW_FILTER_ALL;
            m_searchOpts.filter = static_cast<FilterCategory>(idx);
            SendMessageW(m_filterCombo, CB_SETCURSEL, idx, 0);
            UpdateMenuChecks();
            TriggerSearch();
            break;
        }
        case IDM_VIEW_DARK_MODE:
            m_config.dark_mode = !m_config.dark_mode;
            ApplyTheme();
            break;
        case IDM_VIEW_LANG_EN:
            m_config.language = L"en";
            ApplyLanguage();
            break;
        case IDM_VIEW_LANG_AR:
            m_config.language = L"ar";
            ApplyLanguage();
            break;
        case IDM_VIEW_ALWAYS_ON_TOP:
            m_config.always_on_top = !m_config.always_on_top;
            SetWindowPos(m_hwnd, m_config.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            UpdateMenuChecks();
            break;
        case IDM_VIEW_REFRESH:    TriggerSearch(); break;
        case IDM_VIEW_FULLSCREEN: ToggleFullscreen(); break;

        case IDM_SEARCH_MATCH_CASE:
            m_searchOpts.matchCase = !m_searchOpts.matchCase;
            m_config.match_case = m_searchOpts.matchCase;
            UpdateMenuChecks(); TriggerSearch();
            break;
        case IDM_SEARCH_MATCH_WHOLE_WORD:
            m_searchOpts.matchWholeWord = !m_searchOpts.matchWholeWord;
            m_config.match_whole_word = m_searchOpts.matchWholeWord;
            UpdateMenuChecks(); TriggerSearch();
            break;
        case IDM_SEARCH_MATCH_PATH:
            m_searchOpts.matchPath = !m_searchOpts.matchPath;
            m_config.match_path = m_searchOpts.matchPath;
            UpdateMenuChecks(); TriggerSearch();
            break;
        case IDM_SEARCH_REGEX:
            m_searchOpts.matchRegex = !m_searchOpts.matchRegex;
            m_config.match_regex = m_searchOpts.matchRegex;
            UpdateMenuChecks(); TriggerSearch();
            break;

        case IDM_TOOLS_HTTP_SERVER:
            if (m_httpServer.IsRunning()) {
                m_httpServer.Stop();
                m_config.allow_http_server = false;
            } else {
                if (m_httpServer.Start(m_config.http_server_port)) {
                    m_config.allow_http_server = true;
                }
            }
            UpdateMenuChecks();
            UpdateStatusBar();
            break;
        case IDM_TOOLS_OPEN_HTTP_URL: {
            if (!m_httpServer.IsRunning()) {
                m_httpServer.Start(m_config.http_server_port);
                m_config.allow_http_server = m_httpServer.IsRunning();
                UpdateMenuChecks();
                UpdateStatusBar();
            }
            if (m_httpServer.IsRunning()) {
                std::wstring url = L"http://127.0.0.1:" + std::to_wstring(m_httpServer.Port()) + L"/";
                ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        }
        case IDM_TOOLS_REBUILD_INDEX:
            m_index.StartIndexing(m_hwnd);
            SetTimer(m_hwnd, kProgressTimerId, 120, nullptr);
            UpdateStatusBar();
            break;
        case IDM_TOOLS_RUN_AS_ADMIN:
            RelaunchAsAdmin();
            break;
        case IDM_TOOLS_OPEN_INI:
            m_config.Save();
            ShellExecuteW(m_hwnd, L"open", m_config.ini_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;

        case IDM_HELP_SYNTAX:
            MessageBoxW(m_hwnd,
                L"Search Syntax:\n\n"
                L"  space        AND (e.g. report 2026)\n"
                L"  |            OR (e.g. .jpg|.png)\n"
                L"  !            NOT (e.g. !temp)\n"
                L"  \"...\"        Exact phrase\n"
                L"  * and ?      Wildcards\n"
                L"  ext:         Extensions (e.g. ext:mp4;mkv)\n"
                L"  folder:      Folders only\n"
                L"  file:        Files only\n"
                L"  size:>10mb   Size filter (b, kb, mb, gb)\n"
                L"  path:        Match folder path\n"
                L"  regex:       Regular expression\n\n"
                L"Quick Copy Shortcuts:\n"
                L"  Ctrl+C       Copy selected file(s) to paste in Explorer\n"
                L"  Ctrl+Shift+C Copy full path(s) to clipboard\n"
                L"  Ctrl+D       Toggle Dark Mode",
                L"EverythingVS - Search Syntax", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_HELP_ABOUT:
            MessageBoxW(m_hwnd,
                L"EverythingVS v3.1 (Flat-Arena Native Win32 C++20)\n\n"
                L"Zero-Allocation Flat Arena + Parallel NTFS MFT/USN + Multi-Core Search\n"
                L"Includes Direct UI Copy (CF_HDROP & Path), Dark Mode, AR/EN UI, and Local HTTP/JSON API.",
                L"About EverythingVS", MB_OK | MB_ICONINFORMATION);
            break;
    }
}

LRESULT MainWindow::OnNotify(LPNMHDR nm) {
    if (nm->idFrom != IDC_RESULT_LIST) return 0;

    switch (nm->code) {
        case LVN_GETDISPINFOW: {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
            size_t row = static_cast<size_t>(di->item.iItem);
            if (!m_activeSnap || row >= m_filteredIndices.size()) return 0;
            uint32_t idx = m_filteredIndices[row];
            const auto& e = m_activeSnap->entries[idx];

            if (di->item.mask & LVIF_IMAGE) {
                di->item.iImage = GetIconIndex(*m_activeSnap, idx);
            }
            if (di->item.mask & LVIF_TEXT) {
                if (di->item.iSubItem == 0) {
                    wcsncpy_s(di->item.pszText, di->item.cchTextMax, m_activeSnap->NamePtr(idx), _TRUNCATE);
                } else {
                    static thread_local std::wstring sBuf;
                    switch (di->item.iSubItem) {
                        case 1:
                            sBuf = m_activeSnap->GetParentPath(idx);
                            break;
                        case 2: {
                            uint64_t sz = e.size;
                            if (!e.isFolder && sz == 0 && e.dateModified == 0) {
                                const auto& meta = GetOrFetchMeta(*m_activeSnap, idx);
                                if (meta.valid) sz = meta.size;
                            }
                            sBuf = FormatSize(sz, e.isFolder != 0);
                            break;
                        }
                        case 3: {
                            uint64_t dt = e.dateModified;
                            if (dt == 0) {
                                const auto& meta = GetOrFetchMeta(*m_activeSnap, idx);
                                if (meta.valid) dt = meta.dateModified;
                            }
                            sBuf = FormatFileTime(dt);
                            break;
                        }
                        default:
                            sBuf.clear();
                            break;
                    }
                    wcsncpy_s(di->item.pszText, di->item.cchTextMax, sBuf.c_str(), _TRUNCATE);
                }
            }
            return 0;
        }
        case LVN_COLUMNCLICK: {
            auto* lv = reinterpret_cast<LPNMLISTVIEW>(nm);
            if (m_searchOpts.sortColumn == lv->iSubItem) {
                m_searchOpts.sortAscending = !m_searchOpts.sortAscending;
            } else {
                m_searchOpts.sortColumn = lv->iSubItem;
                m_searchOpts.sortAscending = true;
            }
            m_config.sort_column = m_searchOpts.sortColumn;
            m_config.sort_ascending = m_searchOpts.sortAscending;
            UpdateSortArrow();
            TriggerSearch();
            return 0;
        }
        case NM_DBLCLK:
            OpenSelected();
            return 0;
        case LVN_KEYDOWN: {
            auto* kd = reinterpret_cast<LPNMLVKEYDOWN>(nm);
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (kd->wVKey == 'C' && ctrl) {
                if (shift) CopySelectedTextToClipboard(CopyTextMode::FullPath);
                else       CopySelectedFilesToClipboard();
            } else if (kd->wVKey == VK_RETURN) {
                if (ctrl) OpenSelectedPath();
                else OpenSelected();
            } else if (kd->wVKey == VK_DELETE) {
                DeleteSelectedToRecycleBin();
            } else if (kd->wVKey == VK_ESCAPE) {
                SetFocus(m_searchEdit);
            }
            return 0;
        }
        case LVN_ITEMCHANGED:
            UpdateActionButtons();
            UpdateStatusBar();
            return 0;
    }
    return 0;
}

void MainWindow::OnContextMenu(HWND hwndCtrl, int x, int y) {
    if (hwndCtrl != m_listView || GetFirstSelectedRow() < 0) return;
    const bool ar = IsArabic();
    HMENU hPop = CreatePopupMenu();
    AppendMenuW(hPop, MF_STRING, IDM_FILE_OPEN,             ar ? L"فتح\tEnter"                      : L"&Open\tEnter");
    AppendMenuW(hPop, MF_STRING, IDM_FILE_OPEN_PATH,        ar ? L"فتح مجلد الملف\tCtrl+Enter"       : L"Open &Path\tCtrl+Enter");
    AppendMenuW(hPop, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hPop, MF_STRING, IDM_EDIT_COPY_FILES,       ar ? L"نسخ الملف (للصق مباشر)\tCtrl+C"   : L"&Copy File(s)\tCtrl+C");
    AppendMenuW(hPop, MF_STRING, IDM_EDIT_COPY_FULL_PATH,   ar ? L"نسخ المسار الكامل\tCtrl+Shift+C"  : L"Copy Full &Path\tCtrl+Shift+C");
    AppendMenuW(hPop, MF_STRING, IDM_EDIT_COPY_NAME,        ar ? L"نسخ اسم الملف"                    : L"Copy File &Name");
    AppendMenuW(hPop, MF_STRING, IDM_EDIT_COPY_FOLDER_PATH, ar ? L"نسخ مسار المجلد"                  : L"Copy F&older Path");
    AppendMenuW(hPop, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hPop, MF_STRING, IDM_FILE_DELETE,           ar ? L"حذف إلى سلة المحذوفات\tDel"       : L"&Delete\tDel");
    AppendMenuW(hPop, MF_STRING, IDM_FILE_PROPERTIES,       ar ? L"خصائص\tAlt+Enter"                 : L"P&roperties\tAlt+Enter");

    if (x == -1 && y == -1) {
        POINT pt = { 20, 20 };
        ClientToScreen(m_listView, &pt);
        x = pt.x; y = pt.y;
    }
    TrackPopupMenu(hPop, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(hPop);
}

void MainWindow::OnDestroy() {
    StopSearchWorker();

    RECT rc = {};
    if (!IsIconic(m_hwnd) && !m_fullscreen && GetWindowRect(m_hwnd, &rc)) {
        m_config.window_x = rc.left;
        m_config.window_y = rc.top;
        m_config.window_w = rc.right - rc.left;
        m_config.window_h = rc.bottom - rc.top;
    }
    m_config.col_name_w = ListView_GetColumnWidth(m_listView, 0);
    m_config.col_path_w = ListView_GetColumnWidth(m_listView, 1);
    m_config.col_size_w = ListView_GetColumnWidth(m_listView, 2);
    m_config.col_date_w = ListView_GetColumnWidth(m_listView, 3);
    m_config.Save();

    m_httpServer.Stop();
    m_index.Stop();
    m_index.SaveDatabase(AppConfig::GetAppDataDirectory() + L"\\Everything.db");
    RemoveTrayIcon();
    PostQuitMessage(0);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            OnCreate();
            return 0;
        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_ERASEBKGND:
            if (m_config.dark_mode && m_darkBgBrush) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                RECT rc = {};
                GetClientRect(m_hwnd, &rc);
                FillRect(hdc, &rc, m_darkBgBrush);
                return 1;
            }
            break;
        case WM_CTLCOLOREDIT:
            if (m_config.dark_mode && m_darkEditBrush) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, kDarkText);
                SetBkColor(hdc, kDarkEdit);
                return reinterpret_cast<LRESULT>(m_darkEditBrush);
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (m_config.dark_mode && m_darkBgBrush) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, kDarkText);
                SetBkColor(hdc, kDarkBg);
                return reinterpret_cast<LRESULT>(m_darkBgBrush);
            }
            break;
        case WM_COMMAND:
            OnCommand(wParam, lParam);
            return 0;
        case WM_NOTIFY:
            return OnNotify(reinterpret_cast<LPNMHDR>(lParam));
        case WM_CONTEXTMENU:
            OnContextMenu(reinterpret_cast<HWND>(wParam), static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
            return 0;
        case WM_TIMER:
            if (wParam == kProgressTimerId) {
                UpdateStatusBar();
            } else if (wParam == kToastTimerId) {
                KillTimer(m_hwnd, kToastTimerId);
                m_toastMessage.clear();
                UpdateStatusBar();
            }
            return 0;
        case WM_APP_INDEX_PROGRESS:
            UpdateStatusBar();
            return 0;
        case WM_APP_INDEX_COMPLETE:
            KillTimer(m_hwnd, kProgressTimerId);
            TriggerSearch();
            return 0;
        case WM_APP_INDEX_UPDATED:
            TriggerSearch();
            return 0;
        case WM_APP_SEARCH_DONE:
            ApplyCompletedSearch();
            return 0;
        case WM_APP_TRAY_ICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(m_hwnd, SW_SHOW);
                SetForegroundWindow(m_hwnd);
                SetFocus(m_searchEdit);
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt = {};
                GetCursorPos(&pt);
                HMENU hPop = CreatePopupMenu();
                AppendMenuW(hPop, MF_STRING, IDM_TRAY_SHOW, IsArabic() ? L"إظهار EverythingVS" : L"&Show EverythingVS");
                AppendMenuW(hPop, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hPop, MF_STRING, IDM_TRAY_EXIT, IsArabic() ? L"خروج" : L"E&xit");
                SetForegroundWindow(m_hwnd);
                TrackPopupMenu(hPop, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
                DestroyMenu(hPop);
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE && m_searchEdit && !m_initialFocusDone) {
                SetFocus(m_searchEdit);
                m_initialFocusDone = true;
            }
            return 0;
        case WM_DESTROY:
            OnDestroy();
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
