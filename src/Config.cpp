#include "Config.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

std::wstring AppConfig::GetExeDirectory() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(0, pos) : L".";
}

std::wstring AppConfig::GetAppDataDirectory() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
        std::wstring dir = std::wstring(buf) + L"\\EverythingVS";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return GetExeDirectory();
}

static bool ReadIniBool(const wchar_t* key, bool defVal, const std::wstring& path) {
    return GetPrivateProfileIntW(L"Everything", key, defVal ? 1 : 0, path.c_str()) != 0;
}

static int ReadIniInt(const wchar_t* key, int defVal, const std::wstring& path) {
    return static_cast<int>(GetPrivateProfileIntW(L"Everything", key, defVal, path.c_str()));
}

static std::wstring ReadIniString(const wchar_t* key, const wchar_t* defVal, const std::wstring& path) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"Everything", key, defVal, buf, 64, path.c_str());
    return std::wstring(buf);
}

static void WriteIniInt(const wchar_t* key, int val, const std::wstring& path) {
    std::wstring s = std::to_wstring(val);
    WritePrivateProfileStringW(L"Everything", key, s.c_str(), path.c_str());
}

static void WriteIniString(const wchar_t* key, const std::wstring& val, const std::wstring& path) {
    WritePrivateProfileStringW(L"Everything", key, val.c_str(), path.c_str());
}

void AppConfig::Load() {
    std::wstring localIni = GetExeDirectory() + L"\\Everything.ini";
    app_data = ReadIniBool(L"app_data", true, localIni);
    run_as_admin = ReadIniBool(L"run_as_admin", false, localIni);
    allow_http_server = ReadIniBool(L"allow_http_server", false, localIni);
    allow_etp_server = ReadIniBool(L"allow_etp_server", false, localIni);

    ini_path = app_data ? (GetAppDataDirectory() + L"\\Everything.ini") : localIni;
    const std::wstring& src = (GetFileAttributesW(ini_path.c_str()) != INVALID_FILE_ATTRIBUTES) ? ini_path : localIni;

    http_server_port = ReadIniInt(L"http_server_port", 8080, src);
    if (http_server_port < 1024 || http_server_port > 65535) http_server_port = 8080;

    match_case       = ReadIniBool(L"match_case", false, src);
    match_whole_word = ReadIniBool(L"match_whole_word", false, src);
    match_path       = ReadIniBool(L"match_path", false, src);
    match_regex      = ReadIniBool(L"match_regex", false, src);
    always_on_top    = ReadIniBool(L"always_on_top", false, src);
    dark_mode        = ReadIniBool(L"dark_mode", false, src);
    language         = ReadIniString(L"language", L"en", src);
    if (language != L"ar" && language != L"en") language = L"en";

    sort_column    = ReadIniInt(L"sort_column", 0, src);
    if (sort_column < 0 || sort_column > 3) sort_column = 0;
    sort_ascending = ReadIniBool(L"sort_ascending", true, src);

    window_x = ReadIniInt(L"window_x", 120, src);
    window_y = ReadIniInt(L"window_y", 100, src);
    window_w = ReadIniInt(L"window_w", 1080, src);
    window_h = ReadIniInt(L"window_h", 680, src);
    if (window_w < 640) window_w = 1080;
    if (window_h < 400) window_h = 680;

    col_name_w = ReadIniInt(L"col_name_w", 280, src);
    col_path_w = ReadIniInt(L"col_path_w", 440, src);
    col_size_w = ReadIniInt(L"col_size_w", 110, src);
    col_date_w = ReadIniInt(L"col_date_w", 155, src);
}

void AppConfig::Save() const {
    if (ini_path.empty()) return;
    WriteIniInt(L"app_data", app_data ? 1 : 0, ini_path);
    WriteIniInt(L"run_as_admin", run_as_admin ? 1 : 0, ini_path);
    WriteIniInt(L"allow_http_server", allow_http_server ? 1 : 0, ini_path);
    WriteIniInt(L"allow_etp_server", allow_etp_server ? 1 : 0, ini_path);
    WriteIniInt(L"http_server_port", http_server_port, ini_path);

    WriteIniInt(L"match_case", match_case ? 1 : 0, ini_path);
    WriteIniInt(L"match_whole_word", match_whole_word ? 1 : 0, ini_path);
    WriteIniInt(L"match_path", match_path ? 1 : 0, ini_path);
    WriteIniInt(L"match_regex", match_regex ? 1 : 0, ini_path);
    WriteIniInt(L"always_on_top", always_on_top ? 1 : 0, ini_path);
    WriteIniInt(L"dark_mode", dark_mode ? 1 : 0, ini_path);
    WriteIniString(L"language", language, ini_path);

    WriteIniInt(L"sort_column", sort_column, ini_path);
    WriteIniInt(L"sort_ascending", sort_ascending ? 1 : 0, ini_path);

    WriteIniInt(L"window_x", window_x, ini_path);
    WriteIniInt(L"window_y", window_y, ini_path);
    WriteIniInt(L"window_w", window_w, ini_path);
    WriteIniInt(L"window_h", window_h, ini_path);

    WriteIniInt(L"col_name_w", col_name_w, ini_path);
    WriteIniInt(L"col_path_w", col_path_w, ini_path);
    WriteIniInt(L"col_size_w", col_size_w, ini_path);
    WriteIniInt(L"col_date_w", col_date_w, ini_path);
}
