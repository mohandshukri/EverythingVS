#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include "MainWindow.h"
#include "resource.h"

// Enable modern Windows Common Controls v6 visual styles
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    MainWindow win(hInstance);
    if (!win.Create(nCmdShow)) {
        CoUninitialize();
        return 1;
    }

    HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL));

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!hAccel || !TranslateAcceleratorW(win.Handle(), hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
