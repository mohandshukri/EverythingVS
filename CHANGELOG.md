# Changelog

All notable changes to **EverythingVS** are documented in this file.

## [3.1.0] - 2026-11-23
### Added
- **Direct UI Quick-Action Toolbar**: One-click **Copy File**, **Copy Path**, and **Open Folder** buttons right next to the search bar, automatically enabled when items are selected.
- **Real Windows File Copy (`CF_HDROP`)**: Press `Ctrl+C` (or click **Copy File**) to copy selected files/folders directly to the Windows Clipboard for immediate `Ctrl+V` pasting in File Explorer, Desktop, or other apps.
- **Copy Folder Path** action in Edit & Context menus, plus instant status bar confirmation toasts.
- **Native Dark Mode (`Ctrl+D`)**: Immersive Windows 10/11 dark titlebar (`DwmSetWindowAttribute`), dark ListView, and dark search controls persisted in `Everything.ini`.
- **Bilingual UI (English / Arabic)**: Live switch between English and Arabic (`View -> Language`) persisted in `Everything.ini`.
- **On-Demand NTFS Metadata Caching**: Lazily populates File Size and Date Modified for visible rows indexed via ultra-fast NTFS MFT mode.
- **HTTP Server JSON API & Modern UI**: Added `/?search=...&json=1` JSON endpoint, dark/light responsive web UI, 1-click Copy Path buttons, socket timeouts, and security headers.
- **GitHub Ready Packaging**: Added GitHub Actions CI/CD workflow (`x64` + `Win32` automated builds & releases), `.gitignore`, `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md`, and Issue/PR templates.

### Changed
- Changed UAC execution level from `RequireAdministrator` to `AsInvoker` so standard-user fallback and on-demand elevation work cleanly.
- Enabled **Per-Monitor High DPI Awareness** and `/utf-8` compilation in `EverythingVS.vcxproj`.
- Disabled HTTP/ETP servers by default (`allow_http_server=0`) for safer out-of-the-box defaults.
- Hardened `Everything.db` binary loader with full arena and entry bounds validation.
- Fixed `Ctrl+A` inside the search input so it selects query text instead of the results list, and prevented `WM_ACTIVATE` from stealing focus from the results list.

## [3.0.0] - 2026-11-21
- Flat-arena zero-allocation `CompactEntry` architecture with parallel NTFS MFT/USN scanning, multi-core cancellable search, and bulk binary `Everything.db` cache.
