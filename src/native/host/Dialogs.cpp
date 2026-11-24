#include "Dialogs.h"
#include "CommandRunner.h"
#include "../common/Theme.h"

#include <accctrl.h>
#include <aclapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <atomic>
#include <filesystem>
#include <thread>
#include <utility>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace fs = std::filesystem;

namespace xmf::host {

static bool IsProcessElevated() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;
    TOKEN_ELEVATION elev{};
    DWORD sz = sizeof(elev);
    bool ok = GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &sz) && elev.TokenIsElevated != 0;
    CloseHandle(hToken);
    return ok;
}

static HFONT CreateUiFont(HWND hwnd, int ptSize = 9, int weight = FW_NORMAL) {
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    int height = -MulDiv(ptSize, dpi, 72);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static int ScaleDpi(HWND hwnd, int px) {
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    return MulDiv(px, dpi, 96);
}

static HWND CreateHiddenPopupHost() {
    static const wchar_t* kClass = L"xMenuForgePopupHost";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    theme::ApplyDarkWindowFrame(hwnd);
    return hwnd;
}

// ============================================================================
// Dark-Themed Category Flyout (supports arbitrary nesting from Modern Menu!)
// ============================================================================

void ShowCategoryFlyout(const AppConfig& config, const std::wstring& categoryId, const SelectionContext& ctx, POINT pt) {
    HWND host = CreateHiddenPopupHost();
    if (!host) return;

    std::vector<std::wstring> idTable;
    UINT nextId = 100;

    auto buildMenu = [&](const std::wstring& parentId, auto& self) -> HMENU {
        auto children = GetVisibleChildren(config, parentId, ctx);
        if (children.empty()) return nullptr;
        HMENU menu = CreatePopupMenu();
        UINT pos = 0;
        for (const auto* item : children) {
            if (item->kind == ItemKind::Separator) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ++pos;
            } else if (item->kind == ItemKind::Category) {
                HMENU sub = self(item->id, self);
                if (sub) {
                    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), item->title.c_str());
                    ++pos;
                }
            } else {
                UINT cmdId = nextId++;
                idTable.push_back(item->id);
                AppendMenuW(menu, MF_STRING, cmdId, item->title.c_str());
                ++pos;
            }
        }
        if (pos == 0) {
            DestroyMenu(menu);
            return nullptr;
        }
        return menu;
    };

    HMENU hMenu = buildMenu(categoryId, buildMenu);
    if (hMenu) {
        SetForegroundWindow(host);
        UINT chosen = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, host, nullptr);
        PostMessageW(host, WM_NULL, 0, 0);
        DestroyMenu(hMenu);
        if (chosen >= 100 && (chosen - 100) < idTable.size()) {
            ExecuteCommandById(config, idTable[chosen - 100], ctx, pt);
        }
    }
    DestroyWindow(host);
}

// ============================================================================
// Dark-Themed Edit With Popup
// ============================================================================

void ShowEditWithPopup(const std::vector<std::wstring>& paths, POINT pt) {
    if (paths.empty()) return;
    HWND host = CreateHiddenPopupHost();
    if (!host) return;

    struct Editor {
        std::wstring name;
        std::wstring exe;
    };
    std::vector<Editor> editors;

    auto addEditor = [&](const std::wstring& name, const std::wstring& exe) {
        if (exe.empty() || !PathFileExistsW(exe.c_str())) return;
        for (const auto& e : editors) {
            if (_wcsicmp(e.exe.c_str(), exe.c_str()) == 0) return;
        }
        editors.push_back({name, exe});
    };

    auto scanExt = [&](const wchar_t* ext) {
        IEnumAssocHandlers* pEnum = nullptr;
        if (SUCCEEDED(SHAssocEnumHandlers(ext, ASSOC_FILTER_RECOMMENDED, &pEnum)) && pEnum) {
            IAssocHandler* h = nullptr;
            ULONG fetched = 0;
            while (pEnum->Next(1, &h, &fetched) == S_OK && fetched == 1) {
                LPWSTR uiName = nullptr;
                LPWSTR rawPath = nullptr;
                if (SUCCEEDED(h->GetUIName(&uiName)) && SUCCEEDED(h->GetName(&rawPath))) {
                    if (PathFileExistsW(rawPath)) {
                        addEditor(uiName, rawPath);
                    } else {
                        WCHAR resolved[MAX_PATH] = {};
                        DWORD sz = MAX_PATH;
                        if (SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_BYEXENAME, ASSOCSTR_EXECUTABLE, rawPath, nullptr, resolved, &sz))) {
                            addEditor(uiName, resolved);
                        }
                    }
                }
                if (uiName) CoTaskMemFree(uiName);
                if (rawPath) CoTaskMemFree(rawPath);
                h->Release();
            }
            pEnum->Release();
        }
    };

    std::wstring ext = fs::path(paths[0]).extension().wstring();
    if (!ext.empty()) scanExt(ext.c_str());
    if (_wcsicmp(ext.c_str(), L".txt") != 0) scanExt(L".txt");

    const std::pair<const wchar_t*, const wchar_t*> common[] = {
        {L"Visual Studio Code", L"code.exe"},
        {L"Notepad++", L"notepad++.exe"},
        {L"Sublime Text", L"sublime_text.exe"},
        {L"Cursor", L"cursor.exe"},
    };
    for (const auto& [name, exeName] : common) {
        WCHAR buf[MAX_PATH] = {};
        DWORD sz = MAX_PATH;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_BYEXENAME, ASSOCSTR_EXECUTABLE, exeName, nullptr, buf, &sz))) {
            addEditor(name, buf);
        } else if (SearchPathW(nullptr, exeName, nullptr, MAX_PATH, buf, nullptr) > 0) {
            addEditor(name, buf);
        }
    }

    WCHAR sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    addEditor(L"Notepad", std::wstring(sysDir) + L"\\notepad.exe");

    HMENU hMenu = CreatePopupMenu();
    for (size_t i = 0; i < editors.size(); ++i) {
        AppendMenuW(hMenu, MF_STRING, 100 + i, editors[i].name.c_str());
    }

    SetForegroundWindow(host);
    UINT chosen = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, host, nullptr);
    PostMessageW(host, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
    DestroyWindow(host);

    if (chosen >= 100 && (chosen - 100) < editors.size()) {
        std::wstring args;
        for (const auto& p : paths) {
            if (!args.empty()) args += L" ";
            args += L"\"" + p + L"\"";
        }
        AllowSetForegroundWindow(ASFW_ANY);
        ShellExecuteW(nullptr, L"open", editors[chosen - 100].exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

// ============================================================================
// Dark-Themed System Folders Popup
// ============================================================================

void ShowSystemFoldersPopup(POINT pt) {
    HWND host = CreateHiddenPopupHost();
    if (!host) return;

    auto knownPath = [](REFKNOWNFOLDERID id) -> std::wstring {
        PWSTR p = nullptr;
        std::wstring res;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) {
            res = p;
            CoTaskMemFree(p);
        }
        return res;
    };

    WCHAR tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    WCHAR winDir[MAX_PATH] = {};
    GetWindowsDirectoryW(winDir, MAX_PATH);

    struct FolderItem {
        std::wstring title;
        std::wstring path;
    };
    std::vector<FolderItem> items = {
        {L"AppData (Local)", knownPath(FOLDERID_LocalAppData)},
        {L"AppData (Roaming)", knownPath(FOLDERID_RoamingAppData)},
        {L"ProgramData", knownPath(FOLDERID_ProgramData)},
        {L"Temp Folder", tempDir},
        {L"Start Menu (User)", knownPath(FOLDERID_StartMenu)},
        {L"Start Menu (All Users)", knownPath(FOLDERID_CommonStartMenu)},
        {L"Startup (User)", knownPath(FOLDERID_Startup)},
        {L"Windows Hosts Folder", std::wstring(winDir) + L"\\System32\\drivers\\etc"},
    };

    HMENU hMenu = CreatePopupMenu();
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].path.empty()) {
            AppendMenuW(hMenu, MF_STRING, 100 + i, items[i].title.c_str());
        }
    }

    SetForegroundWindow(host);
    UINT chosen = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, host, nullptr);
    PostMessageW(host, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
    DestroyWindow(host);

    if (chosen >= 100 && (chosen - 100) < items.size() && !items[chosen - 100].path.empty()) {
        AllowSetForegroundWindow(ASFW_ANY);
        ShellExecuteW(nullptr, L"open", items[chosen - 100].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ============================================================================
// Dark-Themed Attributes Dialog
// ============================================================================

namespace {

constexpr int IDC_CHK_HIDDEN = 201;
constexpr int IDC_CHK_SYSTEM = 202;
constexpr int IDC_CHK_READONLY = 203;
constexpr int IDC_CHK_SHOW_HIDDEN = 204;
constexpr int IDC_CHK_SHOW_SYS = 205;
constexpr int IDC_BTN_APPLY = 206;
constexpr int IDC_BTN_CLOSE = 207;

struct AttrDialogState {
    std::vector<std::wstring> paths;
    HFONT font = nullptr;
    HFONT boldFont = nullptr;
    HWND chkHidden = nullptr;
    HWND chkSystem = nullptr;
    HWND chkReadOnly = nullptr;
    HWND chkShowHidden = nullptr;
    HWND chkShowSys = nullptr;
    HWND dtCreateDate = nullptr, dtCreateTime = nullptr;
    HWND dtWriteDate = nullptr, dtWriteTime = nullptr;
};

static bool ReadExplorerAdvancedBool(const wchar_t* name, DWORD trueVal) {
    DWORD val = 0, sz = sizeof(val);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                     name, RRF_RT_REG_DWORD, nullptr, &val, &sz) == ERROR_SUCCESS) {
        return val == trueVal;
    }
    return false;
}

static void WriteExplorerAdvancedBool(const wchar_t* name, DWORD value) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                      0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(hKey);
    }
}

static void ApplyExplorerVisibility(bool showHidden, bool showSuperHidden) {
    WriteExplorerAdvancedBool(L"Hidden", showHidden ? 1 : 2);
    WriteExplorerAdvancedBool(L"ShowSuperHidden", showSuperHidden ? 1 : 0);

    SHELLSTATEW ss{};
    ss.fShowAllObjects = showHidden ? TRUE : FALSE;
    ss.fShowSuperHidden = showSuperHidden ? TRUE : FALSE;
    SHGetSetSettings(&ss, SSF_SHOWALLOBJECTS | SSF_SHOWSUPERHIDDEN, TRUE);

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ShellState"), SMTO_ABORTIFHUNG, 1000, nullptr);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    // Refresh Desktop and open Explorer windows immediately
    EnumWindows([](HWND h, LPARAM) -> BOOL {
        WCHAR cls[64]{};
        GetClassNameW(h, cls, 64);
        if (wcscmp(cls, L"CabinetWClass") == 0 || wcscmp(cls, L"ExploreWClass") == 0 ||
            wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) {
            PostMessageW(h, WM_COMMAND, 41504, 0); // FCIDM_SHVIEW_REFRESH
        }
        return TRUE;
    }, 0);
}

static LRESULT CALLBACK AttrWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<AttrDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<AttrDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        theme::ApplyDarkWindowFrame(hwnd);

        st->font = CreateUiFont(hwnd, 9, FW_NORMAL);
        st->boldFont = CreateUiFont(hwnd, 10, FW_SEMIBOLD);

        auto s = [&](int px) { return ScaleDpi(hwnd, px); };
        auto mkLabel = [&](const wchar_t* text, int x, int y, int w, int h, bool bold = false) {
            HWND c = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, s(x), s(y), s(w), s(h), hwnd, nullptr, cs->hInstance, nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(bold ? st->boldFont : st->font), TRUE);
            return c;
        };
        auto mkCheck = [&](const wchar_t* text, int id, int x, int y, int w, int h, bool tri = false) {
            DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (tri ? BS_AUTO3STATE : BS_AUTOCHECKBOX);
            HWND c = CreateWindowW(L"BUTTON", text, style, s(x), s(y), s(w), s(h), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cs->hInstance, nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
            theme::ApplyDarkControlTheme(c);
            return c;
        };

        std::wstring header = st->paths.empty()
                                  ? L"Folder Visibility Settings"
                                  : (st->paths.size() == 1 ? fs::path(st->paths[0]).filename().wstring()
                                                           : std::to_wstring(st->paths.size()) + L" items selected");
        mkLabel(header.c_str(), 18, 14, 380, 22, true);

        mkLabel(L"File && Folder Attributes", 18, 44, 300, 18, true);
        st->chkHidden = mkCheck(L"Hidden", IDC_CHK_HIDDEN, 24, 68, 130, 22, st->paths.size() > 1);
        st->chkSystem = mkCheck(L"System (Super-Hidden)", IDC_CHK_SYSTEM, 164, 68, 220, 22, st->paths.size() > 1);
        st->chkReadOnly = mkCheck(L"Read-Only", IDC_CHK_READONLY, 24, 96, 130, 22, st->paths.size() > 1);

        if (!st->paths.empty()) {
            bool allH = true, someH = false, allS = true, someS = false, allR = true, someR = false;
            for (const auto& p : st->paths) {
                DWORD a = GetFileAttributesW(p.c_str());
                if (a != INVALID_FILE_ATTRIBUTES) {
                    if (a & FILE_ATTRIBUTE_HIDDEN) someH = true; else allH = false;
                    if (a & FILE_ATTRIBUTE_SYSTEM) someS = true; else allS = false;
                    if (a & FILE_ATTRIBUTE_READONLY) someR = true; else allR = false;
                }
            }
            SendMessageW(st->chkHidden, BM_SETCHECK, allH ? BST_CHECKED : (someH ? BST_INDETERMINATE : BST_UNCHECKED), 0);
            SendMessageW(st->chkSystem, BM_SETCHECK, allS ? BST_CHECKED : (someS ? BST_INDETERMINATE : BST_UNCHECKED), 0);
            SendMessageW(st->chkReadOnly, BM_SETCHECK, allR ? BST_CHECKED : (someR ? BST_INDETERMINATE : BST_UNCHECKED), 0);
        } else {
            EnableWindow(st->chkHidden, FALSE);
            EnableWindow(st->chkSystem, FALSE);
            EnableWindow(st->chkReadOnly, FALSE);
        }

        mkLabel(L"Explorer Visibility", 18, 130, 240, 18, true);
        st->chkShowHidden = mkCheck(L"Show hidden files and folders", IDC_CHK_SHOW_HIDDEN, 24, 154, 260, 22);
        st->chkShowSys = mkCheck(L"Show protected operating system files", IDC_CHK_SHOW_SYS, 24, 180, 280, 22);
        SendMessageW(st->chkShowHidden, BM_SETCHECK, ReadExplorerAdvancedBool(L"Hidden", 1) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(st->chkShowSys, BM_SETCHECK, ReadExplorerAdvancedBool(L"ShowSuperHidden", 1) ? BST_CHECKED : BST_UNCHECKED, 0);

        mkLabel(L"Timestamps (First Item)", 18, 216, 240, 18, true);
        mkLabel(L"Created:", 24, 244, 70, 20);
        st->dtCreateDate = CreateWindowW(DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT, s(100), s(240), s(130), s(24), hwnd, nullptr, cs->hInstance, nullptr);
        st->dtCreateTime = CreateWindowW(DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_TIMEFORMAT, s(238), s(240), s(120), s(24), hwnd, nullptr, cs->hInstance, nullptr);
        mkLabel(L"Modified:", 24, 276, 70, 20);
        st->dtWriteDate = CreateWindowW(DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT, s(100), s(272), s(130), s(24), hwnd, nullptr, cs->hInstance, nullptr);
        st->dtWriteTime = CreateWindowW(DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_TIMEFORMAT, s(238), s(272), s(120), s(24), hwnd, nullptr, cs->hInstance, nullptr);

        for (HWND h : {st->dtCreateDate, st->dtCreateTime, st->dtWriteDate, st->dtWriteTime}) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
        }

        if (!st->paths.empty()) {
            HANDLE hFile = CreateFileW(st->paths[0].c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                FILETIME ftC{}, ftA{}, ftW{};
                if (GetFileTime(hFile, &ftC, &ftA, &ftW)) {
                    auto setDt = [](HWND hDate, HWND hTime, const FILETIME& ft) {
                        FILETIME local{};
                        SYSTEMTIME stLocal{};
                        FileTimeToLocalFileTime(&ft, &local);
                        FileTimeToSystemTime(&local, &stLocal);
                        DateTime_SetSystemtime(hDate, GDT_VALID, &stLocal);
                        DateTime_SetSystemtime(hTime, GDT_VALID, &stLocal);
                    };
                    setDt(st->dtCreateDate, st->dtCreateTime, ftC);
                    setDt(st->dtWriteDate, st->dtWriteTime, ftW);
                }
                CloseHandle(hFile);
            }
        } else {
            for (HWND h : {st->dtCreateDate, st->dtCreateTime, st->dtWriteDate, st->dtWriteTime}) EnableWindow(h, FALSE);
        }

        HWND btnApply = CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      s(200), s(316), s(90), s(30), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_APPLY)), cs->hInstance, nullptr);
        HWND btnClose = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      s(298), s(316), s(90), s(30), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CLOSE)), cs->hInstance, nullptr);
        SendMessageW(btnApply, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
        SendMessageW(btnClose, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
        theme::ApplyDarkControlTheme(btnApply);
        theme::ApplyDarkControlTheme(btnClose);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(236, 236, 242));
        SetBkColor(hdc, RGB(24, 24, 28));
        return reinterpret_cast<LRESULT>(theme::DarkBgBrush());
    }
    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, theme::DarkBgBrush());
        return 1;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_CLOSE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if ((id == IDC_CHK_SHOW_HIDDEN || id == IDC_CHK_SHOW_SYS) && HIWORD(wParam) == BN_CLICKED && st) {
            bool showHidden = SendMessageW(st->chkShowHidden, BM_GETCHECK, 0, 0) == BST_CHECKED;
            bool showSys = SendMessageW(st->chkShowSys, BM_GETCHECK, 0, 0) == BST_CHECKED;
            ApplyExplorerVisibility(showHidden, showSys);
            return 0;
        }
        if (id == IDC_BTN_APPLY && st) {
            LRESULT hState = SendMessageW(st->chkHidden, BM_GETCHECK, 0, 0);
            LRESULT sState = SendMessageW(st->chkSystem, BM_GETCHECK, 0, 0);
            LRESULT rState = SendMessageW(st->chkReadOnly, BM_GETCHECK, 0, 0);

            auto getFt = [](HWND hDate, HWND hTime, FILETIME& out) -> bool {
                SYSTEMTIME d{}, t{};
                if (DateTime_GetSystemtime(hDate, &d) != GDT_VALID) return false;
                if (DateTime_GetSystemtime(hTime, &t) != GDT_VALID) return false;
                d.wHour = t.wHour; d.wMinute = t.wMinute; d.wSecond = t.wSecond; d.wMilliseconds = 0;
                FILETIME local{};
                return SystemTimeToFileTime(&d, &local) && LocalFileTimeToFileTime(&local, &out);
            };
            FILETIME ftC{}, ftW{};
            bool hasC = !st->paths.empty() && getFt(st->dtCreateDate, st->dtCreateTime, ftC);
            bool hasW = !st->paths.empty() && getFt(st->dtWriteDate, st->dtWriteTime, ftW);

            for (const auto& p : st->paths) {
                DWORD attrs = GetFileAttributesW(p.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    if (hState == BST_CHECKED) attrs |= FILE_ATTRIBUTE_HIDDEN;
                    else if (hState == BST_UNCHECKED) attrs &= ~FILE_ATTRIBUTE_HIDDEN;
                    if (sState == BST_CHECKED) attrs |= FILE_ATTRIBUTE_SYSTEM;
                    else if (sState == BST_UNCHECKED) attrs &= ~FILE_ATTRIBUTE_SYSTEM;
                    if (rState == BST_CHECKED) attrs |= FILE_ATTRIBUTE_READONLY;
                    else if (rState == BST_UNCHECKED) attrs &= ~FILE_ATTRIBUTE_READONLY;
                    SetFileAttributesW(p.c_str(), attrs);
                    SHChangeNotify(SHCNE_ATTRIBUTES, SHCNF_PATHW, p.c_str(), nullptr);
                }
                if (hasC || hasW) {
                    HANDLE hFile = CreateFileW(p.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        SetFileTime(hFile, hasC ? &ftC : nullptr, nullptr, hasW ? &ftW : nullptr);
                        CloseHandle(hFile);
                    }
                }
            }

            bool showHidden = SendMessageW(st->chkShowHidden, BM_GETCHECK, 0, 0) == BST_CHECKED;
            bool showSys = SendMessageW(st->chkShowSys, BM_GETCHECK, 0, 0) == BST_CHECKED;
            ApplyExplorerVisibility(showHidden, showSys);
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        if (st) {
            if (st->font) DeleteObject(st->font);
            if (st->boldFont) DeleteObject(st->boldFont);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void ShowAttributesDialog(const std::vector<std::wstring>& paths) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_DATE_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    const wchar_t* kClass = L"xMenuForgeAttributesDlg";
    WNDCLASSW wc{};
    wc.lpfnWndProc = AttrWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = theme::DarkBgBrush();
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    AttrDialogState state;
    state.paths = paths;

    UINT dpi = GetDpiForSystem();
    if (dpi == 0) dpi = 96;
    RECT rc{0, 0, MulDiv(416, dpi, 96), MulDiv(366, dpi, 96)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(
        exStyle, kClass, L"xMenuForge - Attributes",
        style,
        sx, sy, w, h, nullptr, nullptr, wc.hInstance, &state);
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// ============================================================================
// Dark-Themed Take Ownership Dialog (Auto-Elevating)
// ============================================================================

namespace {

constexpr int IDC_TO_CLOSE = 301;
constexpr UINT WM_TO_PROGRESS = WM_APP + 1;
constexpr UINT WM_TO_DONE = WM_APP + 2;

struct TakeOwnState {
    std::vector<std::wstring> targets;
    HWND progress = nullptr;
    HWND statusLabel = nullptr;
    HWND btnClose = nullptr;
    HFONT font = nullptr;
    std::atomic_bool done{false};
};

static PSID GetCurrentUserSidCopy() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return nullptr;
    DWORD sz = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &sz);
    PSID result = nullptr;
    if (sz > 0) {
        std::vector<BYTE> buf(sz);
        if (GetTokenInformation(hToken, TokenUser, buf.data(), sz, &sz)) {
            auto* tu = reinterpret_cast<PTOKEN_USER>(buf.data());
            DWORD sidLen = GetLengthSid(tu->User.Sid);
            result = LocalAlloc(LPTR, sidLen);
            if (result) CopySid(sidLen, result, tu->User.Sid);
        }
    }
    CloseHandle(hToken);
    return result;
}

static void EnablePrivilege(const wchar_t* name) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return;
    TOKEN_PRIVILEGES tp{};
    if (LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(hToken);
}

static void TakeOwnSingle(const std::wstring& path, PSID pSid) {
    if (!pSid) return;
    SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, pSid, nullptr, nullptr, nullptr);

    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = static_cast<LPWSTR>(pSid);

    PACL pNewDacl = nullptr;
    if (SetEntriesInAclW(1, &ea, nullptr, &pNewDacl) == ERROR_SUCCESS && pNewDacl) {
        SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, pNewDacl, nullptr);
        LocalFree(pNewDacl);
    }
}

static LRESULT CALLBACK TakeOwnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<TakeOwnState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<TakeOwnState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        theme::ApplyDarkWindowFrame(hwnd);
        st->font = CreateUiFont(hwnd, 9);

        auto s = [&](int px) { return ScaleDpi(hwnd, px); };
        st->statusLabel = CreateWindowW(L"STATIC", L"Scanning items...", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                                        s(18), s(18), s(400), s(22), hwnd, nullptr, cs->hInstance, nullptr);
        SendMessageW(st->statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);

        st->progress = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                     s(18), s(48), s(400), s(20), hwnd, nullptr, cs->hInstance, nullptr);

        st->btnClose = CreateWindowW(L"BUTTON", L"Working...", WS_CHILD | WS_VISIBLE | WS_DISABLED,
                                     s(328), s(84), s(90), s(28), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TO_CLOSE)), cs->hInstance, nullptr);
        SendMessageW(st->btnClose, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
        theme::ApplyDarkControlTheme(st->btnClose);

        std::thread([hwnd, st]() {
            EnablePrivilege(SE_TAKE_OWNERSHIP_NAME);
            EnablePrivilege(SE_RESTORE_NAME);
            PSID pSid = GetCurrentUserSidCopy();
            std::vector<std::wstring> all;
            for (const auto& t : st->targets) {
                all.push_back(t);
                std::error_code ec;
                if (fs::is_directory(t, ec)) {
                    for (auto it = fs::recursive_directory_iterator(t, fs::directory_options::skip_permission_denied, ec);
                         it != fs::recursive_directory_iterator(); it.increment(ec)) {
                        all.push_back(it->path().wstring());
                    }
                }
            }
            SendMessageW(st->progress, PBM_SETRANGE32, 0, static_cast<LPARAM>(all.size()));
            for (size_t i = 0; i < all.size(); ++i) {
                TakeOwnSingle(all[i], pSid);
                if (i % 4 == 0 || i + 1 == all.size()) {
                    SetWindowTextW(st->statusLabel, all[i].c_str());
                    PostMessageW(hwnd, WM_TO_PROGRESS, static_cast<WPARAM>(i + 1), 0);
                }
            }
            if (pSid) LocalFree(pSid);
            PostMessageW(hwnd, WM_TO_DONE, static_cast<WPARAM>(all.size()), 0);
        }).detach();
        return 0;
    }
    case WM_TO_PROGRESS:
        if (st && st->progress) SendMessageW(st->progress, PBM_SETPOS, wParam, 0);
        return 0;
    case WM_TO_DONE:
        if (st) {
            st->done = true;
            std::wstring msgText = L"Completed ownership update for " + std::to_wstring(wParam) + L" item(s).";
            SetWindowTextW(st->statusLabel, msgText.c_str());
            SetWindowTextW(st->btnClose, L"Close");
            EnableWindow(st->btnClose, TRUE);
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(236, 236, 242));
        SetBkColor(hdc, RGB(24, 24, 28));
        return reinterpret_cast<LRESULT>(theme::DarkBgBrush());
    }
    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, theme::DarkBgBrush());
        return 1;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_TO_CLOSE && st && st->done) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (st && st->font) DeleteObject(st->font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void ShowTakeOwnershipDialog(const std::vector<std::wstring>& paths, bool alreadyElevatedFlag) {
    if (paths.empty()) return;
    if (!IsProcessElevated() && !alreadyElevatedFlag) {
        WCHAR exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring params = L"--take-ownership-elevated";
        for (const auto& p : paths) {
            params += L" \"" + p + L"\"";
        }
        AllowSetForegroundWindow(ASFW_ANY);
        ShellExecuteW(nullptr, L"runas", exe, params.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    const wchar_t* kClass = L"xMenuForgeTakeOwnDlg";
    WNDCLASSW wc{};
    wc.lpfnWndProc = TakeOwnWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = theme::DarkBgBrush();
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    TakeOwnState st;
    st.targets = paths;
    UINT dpi = GetDpiForSystem();
    if (dpi == 0) dpi = 96;
    RECT rc{0, 0, MulDiv(436, dpi, 96), MulDiv(130, dpi, 96)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(
        exStyle, kClass, L"xMenuForge - Take Ownership",
        style,
        sx, sy, w, h, nullptr, nullptr, wc.hInstance, &st);
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

} // namespace xmf::host
