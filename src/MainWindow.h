#pragma once
#include "Config.h"
#include "IndexEngine.h"
#include "HttpServer.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class MainWindow {
public:
    MainWindow(HINSTANCE hInst);
    ~MainWindow();

    bool Create(int nCmdShow);
    HWND Handle() const { return m_hwnd; }

private:
    enum class CopyTextMode {
        FullPath,
        FileName,
        FolderPath
    };

    struct LazyMeta {
        uint64_t size = 0;
        uint64_t dateModified = 0;
        bool     valid = false;
    };

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SearchEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnSize(int width, int height);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnNotify(LPNMHDR nm);
    void OnContextMenu(HWND hwndCtrl, int x, int y);
    void OnDestroy();

    void StartSearchWorker();
    void StopSearchWorker();
    void SearchWorkerLoop();
    void TriggerSearch();
    void ApplyCompletedSearch();

    void UpdateStatusBar();
    void UpdateActionButtons();
    void ShowStatusToast(const std::wstring& message);
    void UpdateMenuChecks();
    void UpdateSortArrow();
    void ApplyTheme();
    void ApplyLanguage();
    bool IsArabic() const { return m_config.language == L"ar"; }
    void ShowTrayIcon();
    void RemoveTrayIcon();

    int GetFirstSelectedRow() const;
    std::vector<int> GetSelectedRows() const;
    void OpenSelected();
    void OpenSelectedPath();
    void ShowSelectedProperties();
    void DeleteSelectedToRecycleBin();
    void CopySelectedFilesToClipboard();
    void CopySelectedTextToClipboard(CopyTextMode mode);
    void ExportResultsEfu();
    void ToggleFullscreen();
    void RelaunchAsAdmin();

    const LazyMeta& GetOrFetchMeta(const IndexData& data, uint32_t idx);
    int GetIconIndex(const IndexData& data, uint32_t idx);
    static std::wstring FormatSize(uint64_t bytes, bool isFolder);
    static std::wstring FormatFileTime(uint64_t ft64);

    HINSTANCE m_hInst = nullptr;
    HWND      m_hwnd = nullptr;
    HWND      m_searchEdit = nullptr;
    HWND      m_filterCombo = nullptr;
    HWND      m_btnCopyFile = nullptr;
    HWND      m_btnCopyPath = nullptr;
    HWND      m_btnOpenPath = nullptr;
    HWND      m_listView = nullptr;
    HWND      m_statusBar = nullptr;
    HFONT     m_uiFont = nullptr;
    HICON     m_hIconBig = nullptr;
    HICON     m_hIconSmall = nullptr;
    HBRUSH    m_darkBgBrush = nullptr;
    HBRUSH    m_darkEditBrush = nullptr;

    NOTIFYICONDATAW m_nid = {};
    bool m_trayAdded = false;
    bool m_fullscreen = false;
    bool m_initialFocusDone = false;
    WINDOWPLACEMENT m_prevPlacement = { sizeof(WINDOWPLACEMENT) };

    std::wstring m_toastMessage;

    AppConfig   m_config;
    IndexEngine m_index;
    HttpServer  m_httpServer;

    SearchOptions         m_searchOpts;
    IndexDataPtr          m_activeSnap;
    std::vector<uint32_t> m_filteredIndices;
    double                m_lastSearchMs = 0.0;

    std::thread             m_searchThread;
    std::mutex              m_searchReqMutex;
    std::condition_variable m_searchCv;
    std::atomic<uint64_t>   m_searchGen{0};
    bool                    m_searchStop = false;
    bool                    m_searchPending = false;
    std::wstring            m_pendingQuery;
    SearchOptions           m_pendingOpts;

    std::mutex              m_searchResMutex;
    IndexDataPtr            m_completedSnap;
    std::vector<uint32_t>   m_completedIndices;
    double                  m_completedMs = 0.0;
    uint64_t                m_completedGen = 0;

    std::unordered_map<std::wstring, int>  m_extIconCache;
    std::unordered_map<uint32_t, LazyMeta> m_metaCache;
    int m_folderIconIdx = 0;
    int m_genericFileIconIdx = 0;
};
