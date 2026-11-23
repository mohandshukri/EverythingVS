#pragma once
#include <string>

struct AppConfig {
    bool app_data = true;
    bool run_as_admin = false;
    bool allow_http_server = false;
    bool allow_etp_server = false;
    int  http_server_port = 8080;

    bool match_case = false;
    bool match_whole_word = false;
    bool match_path = false;
    bool match_regex = false;
    bool always_on_top = false;
    bool dark_mode = false;
    std::wstring language = L"en"; // "en" or "ar"

    int  sort_column = 0;       // 0=Name, 1=Path, 2=Size, 3=DateModified
    bool sort_ascending = true;

    int  window_x = 120;
    int  window_y = 100;
    int  window_w = 1080;
    int  window_h = 680;

    int  col_name_w = 280;
    int  col_path_w = 440;
    int  col_size_w = 110;
    int  col_date_w = 155;

    std::wstring ini_path;

    void Load();
    void Save() const;
    static std::wstring GetExeDirectory();
    static std::wstring GetAppDataDirectory();
};
