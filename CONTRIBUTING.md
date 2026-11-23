# Contributing to EverythingVS

Thank you for your interest in contributing to **EverythingVS**!

## Development Prerequisites
- **Windows 10 / 11**
- **Visual Studio 2022** with the **Desktop development with C++** workload (MSVC `v143`, Windows 10/11 SDK)
- Zero external libraries or package managers required

## Building Locally
```bat
git clone https://github.com/<your-username>/EverythingVS.git
cd EverythingVS
build.bat Release x64
```

Or open `EverythingVS.sln` in Visual Studio 2022 and press `Ctrl+Shift+B`.

## Coding Guidelines
- **C++20 standard**, compiled with `/W3 /utf-8` and zero warnings.
- **Zero external dependencies**: stick to the C++20 STL and native Win32 APIs.
- **Performance first**: avoid per-entry heap allocations in `IndexEngine` and `SearchEngine`; keep `CompactEntry` trivially copyable and flat-arena friendly.
- **UI responsiveness**: never block the Win32 UI thread with disk I/O or full-index scans.

## Submitting a Pull Request
1. Fork the repository and create a feature branch (`git checkout -b feat/my-feature`).
2. Test both `Release | x64` and `Release | Win32` builds.
3. Open a Pull Request describing the change and how you tested it.
