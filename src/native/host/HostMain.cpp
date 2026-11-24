#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../common/Config.h"
#include "CommandRunner.h"
#include "Dialogs.h"
#include "Registration.h"

namespace fs = std::filesystem;

static std::vector<std::wstring> ReadTargetsFile(const std::wstring& filePath) {
    std::vector<std::wstring> lines;
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) return lines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(xmf::Utf8ToWide(line));
    }
    ifs.close();
    DeleteFileW(filePath.c_str());
    return lines;
}

static bool IsProcessAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

static int ElevateSelfAndWait(const std::vector<std::wstring>& args) {
    WCHAR exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return 1;
    std::wstring params;
    for (const auto& a : args) {
        if (!params.empty()) params += L" ";
        params += L"\"" + a + L"\"";
    }
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return 1;
    DWORD exitCode = 0;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 60000);
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
    }
    return static_cast<int>(exitCode);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        CoUninitialize();
        return 1;
    }

    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);

    if (args.empty()) {
        // Default launch (e.g., Start Menu or AppX activation): open Settings UI
        std::wstring settingsExe = xmf::host::GetHostDirectory() + L"\\xMenuForge.Settings.exe";
        if (fs::exists(settingsExe)) {
            AllowSetForegroundWindow(ASFW_ANY);
            ShellExecuteW(nullptr, L"open", settingsExe.c_str(), nullptr, xmf::host::GetHostDirectory().c_str(), SW_SHOWNORMAL);
        }
        CoUninitialize();
        return 0;
    }

    std::wstring mode = args[0];

    if (mode == L"--register") {
        if (!IsProcessAdmin()) {
            int rc = ElevateSelfAndWait(args);
            CoUninitialize();
            return rc;
        }
        bool enableClassic = false;
        bool restartExplorer = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == L"--classic") enableClassic = true;
            if (args[i] == L"--restart-explorer") restartExplorer = true;
        }
        std::wstring msg;
        bool ok = xmf::reg::RegisterAll(xmf::host::GetHostDirectory(), enableClassic, restartExplorer, msg);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    if (mode == L"--unregister") {
        if (!IsProcessAdmin()) {
            int rc = ElevateSelfAndWait(args);
            CoUninitialize();
            return rc;
        }
        bool restartExplorer = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == L"--restart-explorer") restartExplorer = true;
        }
        std::wstring msg;
        bool ok = xmf::reg::UnregisterAll(restartExplorer, msg);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    if (mode == L"--take-ownership-elevated") {
        std::vector<std::wstring> paths(args.begin() + 1, args.end());
        xmf::host::ShowTakeOwnershipDialog(paths, true);
        CoUninitialize();
        return 0;
    }

    if ((mode == L"--invoke" || mode == L"--flyout" || mode == L"--open-editor") && args.size() >= 2) {
        std::wstring targetId = args[1];
        xmf::SelectionContext ctx;
        POINT pt{};
        GetCursorPos(&pt);

        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == L"--targets-file" && i + 1 < args.size()) {
                ctx.paths = ReadTargetsFile(args[++i]);
            } else if (args[i] == L"--target" && i + 1 < args.size()) {
                ctx.paths.push_back(args[++i]);
            } else if (args[i] == L"--background") {
                ctx.isBackground = true;
            } else if (args[i] == L"--folder") {
                ctx.isFolder = true;
            } else if (args[i] == L"--ctrl") {
                ctx.ctrlHeld = true;
            } else if (args[i] == L"--alt") {
                ctx.altHeld = true;
            } else if (args[i] == L"--x" && i + 1 < args.size()) {
                pt.x = _wtoi(args[++i].c_str());
            } else if (args[i] == L"--y" && i + 1 < args.size()) {
                pt.y = _wtoi(args[++i].c_str());
            }
        }

        if (mode == L"--open-editor") {
            std::wstring editorArgs;
            for (const auto& p : ctx.paths) {
                if (!editorArgs.empty()) editorArgs += L" ";
                editorArgs += L"\"" + p + L"\"";
            }
            AllowSetForegroundWindow(ASFW_ANY);
            ShellExecuteW(nullptr, L"open", targetId.c_str(), editorArgs.empty() ? nullptr : editorArgs.c_str(), nullptr, SW_SHOWNORMAL);
            CoUninitialize();
            return 0;
        }

        xmf::AppConfig cfg = xmf::LoadConfig();
        int rc = 0;
        if (mode == L"--flyout") {
            xmf::host::ShowCategoryFlyout(cfg, targetId, ctx, pt);
        } else {
            rc = xmf::host::ExecuteCommandById(cfg, targetId, ctx, pt);
        }
        CoUninitialize();
        return rc;
    }

    CoUninitialize();
    return 0;
}
