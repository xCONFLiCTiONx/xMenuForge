#include "BuiltInTools.h"
#include "../common/Theme.h"

#include <windows.h>
#include <bcrypt.h>
#include <restartmanager.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "rstrtmgr.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;

namespace xmf::tools {

static void SetClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        if (void* p = GlobalLock(hMem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

static std::wstring GetClipboardText() {
    std::wstring text;
    if (!OpenClipboard(nullptr)) return text;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (LPCWSTR p = static_cast<LPCWSTR>(GlobalLock(h))) {
            text = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return text;
}

static void ShowDarkTextDialog(const std::wstring& title, const std::wstring& body) {
    struct State {
        std::wstring text;
        HFONT font = nullptr;
        HWND edit = nullptr;
        HWND btnClose = nullptr;
    };
    State st;
    st.text = body;

    static const wchar_t* kCls = L"xMenuForgeTextInfoDlg";
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            s = static_cast<State*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            theme::ApplyDarkWindowFrame(hwnd);
            s->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  FIXED_PITCH | FF_MODERN, L"Consolas");
            s->edit = CreateWindowExW(0, L"EDIT", s->text.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                      14, 14, 540, 290, hwnd, nullptr, cs->hInstance, nullptr);
            SendMessageW(s->edit, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
            theme::ApplyDarkControlTheme(s->edit);

            s->btnClose = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                        464, 314, 90, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), cs->hInstance, nullptr);
            SendMessageW(s->btnClose, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
            theme::ApplyDarkControlTheme(s->btnClose);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, RGB(236, 238, 244));
            SetBkColor(hdc, RGB(20, 24, 32));
            return reinterpret_cast<LRESULT>(theme::DarkSurfaceBrush());
        }
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, theme::DarkBgBrush());
            return 1;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (s && s->font) DeleteObject(s->font);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = theme::DarkBgBrush();
    wc.lpszClassName = kCls;
    RegisterClassW(&wc);

    int w = 584, h = 396;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                x, y, w, h, nullptr, nullptr, wc.hInstance, &st);
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

static int LaunchPowerShell(const std::wstring& psCommand, bool runAsAdmin, bool showConsole) {
    std::wstring params = L"-NoProfile -ExecutionPolicy Bypass -Command \"" + psCommand + L"\"";
    AllowSetForegroundWindow(ASFW_ANY);
    HINSTANCE res = ShellExecuteW(
        nullptr,
        runAsAdmin ? L"runas" : L"open",
        L"powershell.exe",
        params.c_str(),
        nullptr,
        showConsole ? SW_SHOWNORMAL : SW_HIDE);
    return reinterpret_cast<INT_PTR>(res) > 32 ? 0 : 1;
}

static std::wstring EscapePsSingle(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t c : s) {
        if (c == L'\'') out += L"''";
        else out.push_back(c);
    }
    return out;
}

static int DoCopyFileContents(const SelectionContext& ctx) {
    if (ctx.paths.empty()) return 1;
    std::ifstream ifs(ctx.paths[0], std::ios::binary);
    if (!ifs.is_open()) return 1;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    SetClipboardText(Utf8ToWide(oss.str()));
    return 0;
}

static int DoCopyFileSha256(const SelectionContext& ctx) {
    if (ctx.paths.empty()) return 1;
    HANDLE hFile = CreateFileW(ctx.paths[0].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return 1;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::wstring hexHash;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0) {
            std::vector<BYTE> buf(65536);
            DWORD read = 0;
            while (ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) && read > 0) {
                BCryptHashData(hHash, buf.data(), read, 0);
            }
            BYTE digest[32]{};
            if (BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
                std::wostringstream oss;
                oss << std::uppercase << std::hex << std::setfill(L'0');
                for (BYTE b : digest) oss << std::setw(2) << static_cast<int>(b);
                hexHash = oss.str();
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    CloseHandle(hFile);

    if (hexHash.empty()) return 1;
    SetClipboardText(hexHash);
    ShowDarkTextDialog(L"xMenuForge — SHA-256 Hash (Copied)",
                       L"File:\r\n" + ctx.paths[0] + L"\r\n\r\nSHA-256 (copied to clipboard):\r\n" + hexHash);
    return 0;
}

static int DoCopyTreeView(const std::wstring& targetDir) {
    if (targetDir.empty()) return 1;
    std::wostringstream oss;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(targetDir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        oss << it->path().wstring() << L"\r\n";
    }
    SetClipboardText(oss.str());
    return 0;
}

static int DoLockFile(const SelectionContext& ctx) {
    if (ctx.paths.empty()) return 1;
    HANDLE hFile = CreateFileW(ctx.paths[0].c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        ShowDarkTextDialog(L"xMenuForge — Lock File",
                           L"Could not acquire exclusive lock on:\r\n" + ctx.paths[0] +
                               L"\r\n\r\nWin32 Error: " + std::to_wstring(GetLastError()));
        return 1;
    }
    ShowDarkTextDialog(L"xMenuForge — File Exclusively Locked",
                       L"Exclusive lock held on:\r\n" + ctx.paths[0] +
                           L"\r\n\r\nNo other process can read, write, or delete this file while this window is open.\r\n\r\nClick Close to release the lock.");
    CloseHandle(hFile);
    return 0;
}

static int DoLockCheck(const SelectionContext& ctx) {
    if (ctx.paths.empty()) return 1;
    DWORD session = 0;
    WCHAR key[CCH_RM_SESSION_KEY + 1] = {};
    if (RmStartSession(&session, 0, key) != ERROR_SUCCESS) return 1;

    std::vector<LPCWSTR> files;
    for (const auto& p : ctx.paths) files.push_back(p.c_str());
    RmRegisterResources(session, static_cast<UINT>(files.size()), files.data(), 0, nullptr, 0, nullptr);

    UINT needed = 0, count = 0;
    DWORD rebootReason = RmRebootReasonNone;
    DWORD res = RmGetList(session, &needed, &count, nullptr, &rebootReason);

    std::wostringstream oss;
    oss << L"Target: " << ctx.paths[0] << L"\r\n\r\n";
    if (res == ERROR_MORE_DATA && needed > 0) {
        std::vector<RM_PROCESS_INFO> infos(needed);
        count = needed;
        if (RmGetList(session, &needed, &count, infos.data(), &rebootReason) == ERROR_SUCCESS) {
            oss << L"Locking processes found (" << count << L"):\r\n\r\n";
            for (UINT i = 0; i < count; ++i) {
                DWORD pid = infos[i].Process.dwProcessId;
                std::wstring procPath = L"(path unavailable)";
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (hProc) {
                    WCHAR buf[MAX_PATH] = {};
                    DWORD sz = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, buf, &sz)) procPath = buf;
                    CloseHandle(hProc);
                }
                oss << L"PID:  " << pid << L"\r\n"
                    << L"Name: " << infos[i].strAppName << L"\r\n"
                    << L"Path: " << procPath << L"\r\n\r\n";
            }
        }
    } else {
        oss << L"No locking processes found.";
    }
    RmEndSession(session);
    ShowDarkTextDialog(L"xMenuForge — File Lock Check", oss.str());
    return 0;
}

static int DoSearchClipboard(const std::wstring& targetDir) {
    if (targetDir.empty()) return 1;
    std::wstring query = GetClipboardText();
    while (!query.empty() && (query.back() == L'\r' || query.back() == L'\n' || query.back() == L' ')) query.pop_back();
    while (!query.empty() && (query.front() == L'\r' || query.front() == L'\n' || query.front() == L' ')) query.erase(query.begin());
    if (query.empty()) {
        ShowDarkTextDialog(L"xMenuForge — Search", L"Clipboard is empty. Copy a file or folder name first.");
        return 1;
    }

    std::wstring qLower = query;
    std::transform(qLower.begin(), qLower.end(), qLower.begin(), ::towlower);
    std::wstring exactMatch, partialMatch;

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(targetDir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::wstring name = it->path().filename().wstring();
        std::wstring nLower = name;
        std::transform(nLower.begin(), nLower.end(), nLower.begin(), ::towlower);
        if (nLower == qLower) {
            exactMatch = it->path().wstring();
            break;
        }
        if (partialMatch.empty() && nLower.find(qLower) != std::wstring::npos) {
            partialMatch = it->path().wstring();
        }
    }

    std::wstring found = !exactMatch.empty() ? exactMatch : partialMatch;
    if (!found.empty()) {
        std::wstring args = L"/select,\"" + found + L"\"";
        AllowSetForegroundWindow(ASFW_ANY);
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        return 0;
    }
    ShowDarkTextDialog(L"xMenuForge — Search", L"No matching file or folder found for:\r\n" + query + L"\r\n\r\nIn:\r\n" + targetDir);
    return 1;
}

bool TryRunExtendedBuiltIn(
    const AppConfig&,
    const std::wstring& act,
    const SelectionContext& ctx,
    const std::wstring& targetDir,
    int& outExitCode) {
    if (act == L"copy_file_contents") {
        outExitCode = DoCopyFileContents(ctx);
        return true;
    }
    if (act == L"copy_sha256") {
        outExitCode = DoCopyFileSha256(ctx);
        return true;
    }
    if (act == L"copy_tree_view") {
        outExitCode = DoCopyTreeView(targetDir);
        return true;
    }
    if (act == L"lock_file") {
        outExitCode = DoLockFile(ctx);
        return true;
    }
    if (act == L"lock_check") {
        outExitCode = DoLockCheck(ctx);
        return true;
    }
    if (act == L"search_clipboard") {
        outExitCode = DoSearchClipboard(targetDir);
        return true;
    }
    if (act == L"restart_explorer") {
        outExitCode = LaunchPowerShell(L"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }", false, false);
        return true;
    }
    if (act == L"clear_icon_cache") {
        outExitCode = LaunchPowerShell(L"ie4uinit.exe -show; Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; Start-Process explorer.exe", true, false);
        return true;
    }
    if (act == L"clear_print_queue") {
        outExitCode = LaunchPowerShell(L"$host.UI.RawUI.WindowTitle='Clear Print Queue'; Stop-Service spooler -Force; Remove-Item \"$env:SystemRoot\\System32\\spool\\PRINTERS\\*\" -Recurse -Force -ErrorAction SilentlyContinue; Start-Service spooler; Write-Host 'Print queue cleared!' -ForegroundColor Green; Start-Sleep 2", true, true);
        return true;
    }
    if (act == L"flush_dns") {
        outExitCode = LaunchPowerShell(L"ipconfig /flushdns; Start-Sleep 1", true, true);
        return true;
    }
    if (act == L"ip_renew") {
        outExitCode = LaunchPowerShell(L"$host.UI.RawUI.WindowTitle='IP Renew'; Clear-DnsClientCache; ipconfig /release; ipconfig /renew; ipconfig /flushdns; Write-Host 'DNS flushed and IP renewed.' -ForegroundColor Green; Start-Sleep 2", true, true);
        return true;
    }
    if (act == L"add_to_path") {
        if (targetDir.empty()) { outExitCode = 1; return true; }
        std::wstring d = EscapePsSingle(targetDir);
        outExitCode = LaunchPowerShell(L"$p='" + d + L"'; $m=[Environment]::GetEnvironmentVariable('Path','Machine'); if($m -notlike ('*'+$p+'*')){[Environment]::SetEnvironmentVariable('Path',($m.TrimEnd(';')+';'+$p),'Machine')}", true, false);
        return true;
    }
    if (act == L"check_internet") {
        outExitCode = LaunchPowerShell(L"$host.UI.RawUI.WindowTitle='Check for Internet'; Write-Host 'Checking internet connection...' -ForegroundColor Cyan; while ($true) { if (ping.exe -n 1 -w 1000 8.8.8.8 | Select-String 'TTL=') { break }; Write-Host ('[' + (Get-Date -Format 'HH:mm:ss') + '] Offline...') -ForegroundColor Yellow; Start-Sleep 10 }; Write-Host ('[' + (Get-Date -Format 'HH:mm:ss') + '] Connected!') -ForegroundColor Green; [System.Media.SystemSounds]::Exclamation.Play(); $null=(New-Object -ComObject WScript.Shell).Popup('You are connected to the internet!',7,'Network Status',64); Read-Host 'Press Enter to close'", false, true);
        return true;
    }
    if (act == L"ip_watcher") {
        outExitCode = LaunchPowerShell(L"$host.UI.RawUI.WindowTitle='IP Watcher'; $t=Read-Host 'Enter IP or host to watch'; if(-not $t){exit}; $st='online'; $ws=New-Object -ComObject WScript.Shell; while($true){ if(ping.exe -n 1 -w 1000 $t | Select-String 'TTL='){ if($st -eq 'offline'){ Write-Host ('['+(Get-Date -Format 'HH:mm:ss')+'] '+$t+' is BACK ONLINE') -ForegroundColor Green; [System.Media.SystemSounds]::Asterisk.Play(); $null=$ws.Popup($t+' is back online.',5,'IP Watcher',64); $st='online' } } else { if($st -eq 'online'){ Write-Host ('['+(Get-Date -Format 'HH:mm:ss')+'] '+$t+' went OFFLINE') -ForegroundColor Red; [System.Media.SystemSounds]::Hand.Play(); $null=$ws.Popup($t+' went offline.',5,'IP Watcher',48); $st='offline' } }; Start-Sleep 10 }", false, true);
        return true;
    }
    if (act == L"wifi_passwords") {
        outExitCode = LaunchPowerShell(L"$host.UI.RawUI.WindowTitle='WiFi Passwords'; $w=netsh wlan show profiles; $ssids=($w | Select-String 'All User Profile\\s*:\\s*(.+)' -AllMatches).Matches | ForEach-Object{$_.Groups[1].Value.Trim()}; foreach($s in $ssids){ $k=((netsh wlan show profiles name=\"$s\" key=clear) | Select-String 'Key Content\\s*:\\s*(.+)').Matches.Groups[1].Value; Write-Host (\"{0,-30} : {1}\" -f $s, $(if($k){$k}else{'(none)'})) }; Write-Host ''; Read-Host 'Press Enter to close'", true, true);
        return true;
    }
    if (act == L"flip_image") {
        if (ctx.paths.empty()) { outExitCode = 1; return true; }
        std::wstring f = EscapePsSingle(ctx.paths[0]);
        outExitCode = LaunchPowerShell(L"Add-Type -AssemblyName System.Drawing; $p='" + f + L"'; $i=[System.Drawing.Image]::FromFile($p); $i.RotateFlip('RotateNoneFlipX'); $tmp=$p+'.xmf.tmp'; $i.Save($tmp, $i.RawFormat); $i.Dispose(); Move-Item -LiteralPath $tmp -Destination $p -Force", false, false);
        return true;
    }
    if (act == L"create_junction") {
        if (ctx.paths.empty()) { outExitCode = 1; return true; }
        std::wstring f = EscapePsSingle(ctx.paths[0]);
        outExitCode = LaunchPowerShell(L"Add-Type -AssemblyName System.Windows.Forms; $t='" + f + L"'; $d=New-Object System.Windows.Forms.FolderBrowserDialog; $d.Description='Select destination folder for Junction'; if($d.ShowDialog() -eq 'OK'){ $j=Join-Path $d.SelectedPath (Split-Path $t -Leaf); New-Item -ItemType Junction -Path $j -Target $t | Out-Null; Start-Process explorer.exe -ArgumentList ('/select,\"'+$j+'\"') }", true, false);
        return true;
    }
    if (act == L"mount_image") {
        if (ctx.paths.empty()) { outExitCode = 1; return true; }
        std::wstring f = EscapePsSingle(ctx.paths[0]);
        outExitCode = LaunchPowerShell(L"Mount-DiskImage -ImagePath '" + f + L"' -ErrorAction Stop", true, false);
        return true;
    }
    if (act == L"unmount_image") {
        if (ctx.paths.empty()) { outExitCode = 1; return true; }
        std::wstring f = EscapePsSingle(ctx.paths[0]);
        outExitCode = LaunchPowerShell(L"Dismount-DiskImage -ImagePath '" + f + L"' -ErrorAction Stop", true, false);
        return true;
    }
    if (act == L"unzip_here") {
        if (ctx.paths.empty()) { outExitCode = 1; return true; }
        std::wstring f = EscapePsSingle(ctx.paths[0]);
        outExitCode = LaunchPowerShell(L"$p='" + f + L"'; $dest=Join-Path (Split-Path $p -Parent) ([System.IO.Path]::GetFileNameWithoutExtension($p)); Expand-Archive -LiteralPath $p -DestinationPath $dest -Force", false, false);
        return true;
    }
    if (act == L"view_sqlite_db") {
        if (ctx.paths.empty()) { outExitCode = 1; return true; }
        std::wstring f = EscapePsSingle(ctx.paths[0]);
        outExitCode = LaunchPowerShell(L"$host.UI.RawUI.WindowTitle='SQLite DB Viewer'; sqlite3 '" + f + L"' \".headers on\" \".mode box\" \".tables\" \"SELECT * FROM Files LIMIT 200;\"; Read-Host 'Press Enter to close'", false, true);
        return true;
    }
    return false;
}

} // namespace xmf::tools
