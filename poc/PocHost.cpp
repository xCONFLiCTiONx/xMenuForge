#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    MessageBoxW(nullptr,
        L"This executable exists to provide the sparse-package Win32 application identity.\r\n"
        L"The Explorer extension is implemented by xMenuForgeContextMenu.dll.",
        L"xMenuForge POC host", MB_OK | MB_ICONINFORMATION);
    return 0;
}
