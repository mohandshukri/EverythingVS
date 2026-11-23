# EverythingVS

[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D6?logo=windows)](https://github.com)
[![Language](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
[![Dependencies](https://img.shields.io/badge/dependencies-zero-brightgreen)](#architecture)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**EverythingVS** is a lightweight, zero-dependency **native Win32 C++20** instant file search engine for Windows. Built on a zero-allocation flat-arena index, parallel NTFS MFT/USN scanning, and multi-core cancellable search, it searches millions of files in milliseconds while keeping memory usage minimal.

## Key Features

- **Ultra-Fast Dual-Mode Indexing (`IndexEngine`)**
  - **Parallel NTFS MFT / USN Journal** (`FSCTL_ENUM_USN_DATA`) when elevated via *Tools → Run as Administrator*, scanning all fixed NTFS drives simultaneously.
  - **Automatic Non-Admin Fallback** (`FindFirstFileExW` with `FIND_FIRST_EX_LARGE_FETCH`) so the app runs cleanly as a standard user (`AsInvoker`).
  - **Instant Startup Cache (`Everything.db`)** using 4 bulk binary reads/writes with strict bounds validation.
  - **On-Demand Metadata Caching** lazily populates *Size* and *Date Modified* for visible NTFS MFT rows without slowing down indexing.
- **Effortless UI Copying & File Actions (`MainWindow`)**
  - **One-Click Top Action Bar**: **Copy File**, **Copy Path**, and **Open Folder** buttons right beside the search bar.
  - **Real Windows File Copy (`CF_HDROP`)**: Press `Ctrl+C` or click **Copy File** to copy actual files/folders to the clipboard and paste (`Ctrl+V`) directly into File Explorer, Desktop, or any app.
  - **Text Copy Modes**: Copy full path (`Ctrl+Shift+C`), file name, or parent folder path with instant status-bar toast feedback.
- **Powerful Search Engine (`SearchEngine`)**
  - Boolean operators: space-separated `AND`, `|` (`OR`), `!` (`NOT`), `"exact phrase"`, and `*` / `?` wildcards.
  - Functions: `ext:`, `folder:`, `file:`, `size:>10mb`, `path:`, `parent:`, `regex:`.
  - Live toggles: Match Case (`Ctrl+I`), Match Whole Word (`Ctrl+B`), Match Path (`Ctrl+U`), Regex (`Ctrl+R`).
  - Built-in category filters: Everything, Audio, Compressed, Document, Executable, Folder, Picture, Video.
- **Modern Native Win32 UI**
  - Virtual `SysListView32` (`LVS_OWNERDATA`) displaying 1,000,000+ rows smoothly with real Windows Shell icons and Per-Monitor High-DPI support.
  - **Native Dark Mode (`Ctrl+D`)** with immersive Windows 10/11 dark titlebar (`DwmSetWindowAttribute`).
  - **Bilingual Interface**: Instant switch between **English** and **Arabic (العربية)** via *View → Language*.
  - Export results to **EFU / CSV** (`Ctrl+S`), Recycle Bin delete, Shell Properties, Fullscreen (`F11`), Always on Top, and System Tray integration.
- **Built-in Localhost HTTP & JSON API (`HttpServer`)**
  - Disabled by default (`allow_http_server=0`), toggleable anytime from *Tools → HTTP Server*.
  - Binds strictly to `127.0.0.1` with responsive Dark/Light web UI, 1-click Copy Path buttons, and a JSON endpoint:
    ```
    http://127.0.0.1:8080/?search=ext:pdf&json=1&count=100
    ```

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Ctrl+C` | Copy selected file(s) to clipboard (`CF_HDROP`) |
| `Ctrl+Shift+C` | Copy full path(s) to clipboard |
| `Enter` / `Double-Click` | Open selected file or folder |
| `Ctrl+Enter` | Open containing folder in File Explorer |
| `Alt+Enter` | Show Windows Properties dialog |
| `Del` | Move selected item(s) to Recycle Bin |
| `Ctrl+D` | Toggle Dark Mode |
| `Ctrl+S` | Export results to EFU / CSV |
| `Ctrl+I` / `Ctrl+B` / `Ctrl+U` / `Ctrl+R` | Toggle Case / Whole Word / Path / Regex |
| `F5` / `F11` | Refresh / Toggle Fullscreen |
| `Esc` | Clear search box or hide to System Tray |

## Search Syntax Examples

```text
report 2026                 # Contains both "report" AND "2026"
.jpg|.png|.webp             # Matches any of the three extensions
ext:mp4;mkv size:>100mb     # Videos larger than 100 MB
folder: projects !backup    # Folders containing "projects" excluding "backup"
path:Documents *.docx       # Word documents inside any Documents path
regex:^IMG_\d{8}\.jpg$      # Regular expression match
```

## Architecture

EverythingVS avoids per-entry heap allocations by storing all indexed objects in three contiguous flat buffers (`IndexData`):
- `entries`: packed 48-byte `CompactEntry` structs referencing parent folder indices.
- `nameArena` / `lowerArena`: contiguous UTF-16 string arenas for cache-friendly scanning.
- Immutable `std::shared_ptr<const IndexData>` snapshots allow lock-free multi-core search with instant generation-based cancellation on every keystroke.

```text
EverythingVS/
├── src/
│   ├── main.cpp            # Win32 entry point & accelerator message loop
│   ├── MainWindow.h/.cpp   # Virtual ListView, UI Copy (CF_HDROP), Dark Mode, AR/EN UI
│   ├── IndexEngine.h/.cpp  # Parallel NTFS MFT/USN scanner, fallback & Everything.db cache
│   ├── SearchEngine.h/.cpp # Query parser, wildcards, regex & multi-core search engine
│   ├── HttpServer.h/.cpp   # Localhost HTTP web UI & JSON API server
│   ├── Config.h/.cpp       # Everything.ini persistence
│   ├── EverythingVS.rc     # Menus, accelerators, icon & version metadata
│   └── resource.h          # Control & menu command IDs
├── .github/workflows/      # Automated Windows CI & Release packaging (x64 + Win32)
├── EverythingVS.sln        # Visual Studio 2022 Solution
├── EverythingVS.vcxproj    # MSBuild C++20 project (zero external dependencies)
├── Everything.ini          # Default configuration
└── build.bat               # One-click CLI build script
```

## Building from Source

### Requirements
- Windows 10 or Windows 11
- **Visual Studio 2022** with the **Desktop development with C++** workload

### Build via Visual Studio
1. Open `EverythingVS.sln` in Visual Studio 2022.
2. Select **`Release | x64`** (or **`Release | Win32`**) and press **`Ctrl+Shift+B`**.
3. Run `bin\x64\Release\EverythingVS.exe`.

### Build via Command Line
```bat
build.bat Release x64
```

## Disclaimer
EverythingVS is an independent, clean-room open-source C++20 Win32 implementation inspired by instant NTFS desktop file search utilities. It is not affiliated with or endorsed by voidtools.

## License
Released under the [MIT License](LICENSE).
