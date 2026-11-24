#include "CommandRunner.h"
#include "BuiltInTools.h"
#include "Dialogs.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;

namespace xmf::host {

std::wstring GetHostDirectory() {
    WCHAR buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return buf;
}

static void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::wstring QuoteArg(const std::wstring& s) {
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') out += L"\\\"";
        else out.push_back(c);
    }
    if (!s.empty() && s.back() == L'\\') out.push_back(L'\\');
    out.push_back(L'"');
    return out;
}

static std::wstring ResolveTargetDir(const SelectionContext& ctx) {
    if (ctx.paths.empty()) return {};
    std::wstring first = ctx.paths[0];
    if (ctx.isBackground || ctx.isFolder) return first;
    DWORD attrs = GetFileAttributesW(first.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return first;
    fs::path p(first);
    return p.parent_path().wstring();
}

static std::wstring ResolveWorkingDir(const RunnerConfig& runner, const SelectionContext& ctx) {
    if (runner.workingDirectoryMode == WorkingDirectoryMode::Custom && !runner.customWorkingDirectory.empty()) {
        return runner.customWorkingDirectory;
    }
    std::wstring targetDir = ResolveTargetDir(ctx);
    if (runner.workingDirectoryMode == WorkingDirectoryMode::Parent && !targetDir.empty()) {
        return fs::path(targetDir).parent_path().wstring();
    }
    return targetDir;
}

static std::wstring ExpandTemplate(const std::wstring& tpl, const std::wstring& singlePath, const SelectionContext& ctx) {
    std::wstring result = tpl;
    fs::path p(singlePath);
    std::wstring dir = ResolveTargetDir(ctx);
    std::wstring allQuoted;
    for (const auto& t : ctx.paths) {
        if (!allQuoted.empty()) allQuoted += L" ";
        allQuoted += QuoteArg(t);
    }

    ReplaceAll(result, L"{paths}", allQuoted);
    ReplaceAll(result, L"{path}", singlePath);
    ReplaceAll(result, L"{dir}", dir);
    ReplaceAll(result, L"{name}", p.filename().wstring());
    ReplaceAll(result, L"{stem}", p.stem().wstring());
    ReplaceAll(result, L"{ext}", p.extension().wstring());
    ReplaceAll(result, L"%1", singlePath);
    ReplaceAll(result, L"%V", singlePath);
    ReplaceAll(result, L"%v", singlePath);
    return result;
}

static std::wstring FindPowerShellExe(const std::wstring& pref) {
    auto findOnPath = [](const wchar_t* exe) -> std::wstring {
        WCHAR buf[MAX_PATH] = {};
        if (SearchPathW(nullptr, exe, nullptr, MAX_PATH, buf, nullptr) > 0) {
            return buf;
        }
        return {};
    };
    if (pref == L"powershell") {
        std::wstring p = findOnPath(L"powershell.exe");
        return p.empty() ? L"powershell.exe" : p;
    }
    if (pref == L"pwsh" || pref == L"auto") {
        std::wstring p = findOnPath(L"pwsh.exe");
        if (!p.empty()) return p;
    }
    std::wstring p = findOnPath(L"powershell.exe");
    return p.empty() ? L"powershell.exe" : p;
}

static std::vector<wchar_t> BuildEnvironmentBlock(const std::map<std::wstring, std::wstring>& overrides) {
    if (overrides.empty()) return {};
    std::map<std::wstring, std::wstring> merged;
    LPWCH envStrings = GetEnvironmentStringsW();
    if (envStrings) {
        for (LPWCH p = envStrings; *p; p += wcslen(p) + 1) {
            std::wstring entry(p);
            size_t eq = entry.find(L'=');
            if (eq != std::wstring::npos && eq > 0) {
                merged[entry.substr(0, eq)] = entry.substr(eq + 1);
            }
        }
        FreeEnvironmentStringsW(envStrings);
    }
    for (const auto& [k, v] : overrides) {
        merged[k] = v;
    }
    std::vector<wchar_t> block;
    for (const auto& [k, v] : merged) {
        std::wstring line = k + L"=" + v;
        block.insert(block.end(), line.begin(), line.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

static void CopyToClipboard(const std::wstring& text) {
    if (text.empty()) return;
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* p = GlobalLock(hMem);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

static int RunBuiltInAction(const AppConfig& config, const MenuItem& item, const SelectionContext& ctx, POINT pt) {
    const std::wstring& act = item.builtInAction;
    std::wstring dir = ResolveTargetDir(ctx);

    if (act == L"attributes") {
        ShowAttributesDialog(ctx.paths);
        return 0;
    }
    if (act == L"take_ownership") {
        ShowTakeOwnershipDialog(ctx.paths, false);
        return 0;
    }
    if (act == L"edit_with") {
        ShowEditWithPopup(ctx.paths, pt);
        return 0;
    }
    if (act == L"system_folders") {
        ShowSystemFoldersPopup(pt);
        return 0;
    }
    if (act == L"settings") {
        std::wstring settingsExe = GetHostDirectory() + L"\\xMenuForge.Settings.exe";
        AllowSetForegroundWindow(ASFW_ANY);
        ShellExecuteW(nullptr, L"open", settingsExe.c_str(), nullptr, GetHostDirectory().c_str(), SW_SHOWNORMAL);
        return 0;
    }
    if (act == L"copy_name" || act == L"copy_path") {
        std::wstring text;
        for (const auto& p : ctx.paths) {
            if (!text.empty()) text += L"\r\n";
            text += (act == L"copy_name") ? fs::path(p).filename().wstring() : p;
        }
        CopyToClipboard(text);
        return 0;
    }
    if (act == L"paste_to_file") {
        if (dir.empty()) return 1;
        if (!OpenClipboard(nullptr)) return 1;
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        std::wstring text;
        if (hData) {
            LPCWSTR ptr = static_cast<LPCWSTR>(GlobalLock(hData));
            if (ptr) {
                text = ptr;
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
        if (text.empty()) return 1;

        fs::path outPath = fs::path(dir) / L"Clipboard.txt";
        int suffix = 1;
        while (fs::exists(outPath) && suffix < 1000) {
            outPath = fs::path(dir) / (L"Clipboard_" + std::to_wstring(suffix++) + L".txt");
        }
        std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            std::string utf8 = WideToUtf8(text);
            ofs.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            ofs.close();
            SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, outPath.c_str(), nullptr);
            return 0;
        }
        return 1;
    }
    if (act == L"terminal" || act == L"terminal_admin") {
        std::wstring d = dir;
        if (!d.empty() && d.back() == L'\\') d += L'\\';
        std::wstring params = L"-d \"" + d + L"\"";
        if (!config.execution.defaultTerminalProfile.empty()) {
            params = L"-p \"" + config.execution.defaultTerminalProfile + L"\" " + params;
        }
        AllowSetForegroundWindow(ASFW_ANY);
        HINSTANCE res = ShellExecuteW(
            nullptr,
            act == L"terminal_admin" ? L"runas" : L"open",
            L"wt.exe",
            params.c_str(),
            dir.empty() ? nullptr : dir.c_str(),
            SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(res) > 32 ? 0 : 1;
    }
    if (act == L"powershell" || act == L"powershell_admin") {
        std::wstring ps = FindPowerShellExe(config.execution.defaultPowerShell);
        std::wstring params = L"-NoExit -Command \"Set-Location -LiteralPath '" + dir + L"'\"";
        AllowSetForegroundWindow(ASFW_ANY);
        HINSTANCE res = ShellExecuteW(
            nullptr,
            act == L"powershell_admin" ? L"runas" : L"open",
            ps.c_str(),
            dir.empty() ? L"-NoExit" : params.c_str(),
            dir.empty() ? nullptr : dir.c_str(),
            SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(res) > 32 ? 0 : 1;
    }
    if (act == L"cmd" || act == L"cmd_admin") {
        std::wstring params = dir.empty() ? L"/k" : (L"/k cd /d \"" + dir + L"\"");
        AllowSetForegroundWindow(ASFW_ANY);
        HINSTANCE res = ShellExecuteW(
            nullptr,
            act == L"cmd_admin" ? L"runas" : L"open",
            L"cmd.exe",
            params.c_str(),
            dir.empty() ? nullptr : dir.c_str(),
            SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(res) > 32 ? 0 : 1;
    }
    int extendedRc = 0;
    if (xmf::tools::TryRunExtendedBuiltIn(config, act, ctx, dir, extendedRc)) {
        return extendedRc;
    }
    return 1;
}

static int RunCustomCommand(const AppConfig& config, const MenuItem& item, const SelectionContext& ctx) {
    auto startTime = std::chrono::steady_clock::now();

    ConsoleVisibility vis = item.runner.consoleVisibility;
    if (vis == ConsoleVisibility::Default) {
        vis = config.execution.defaultConsoleVisibility;
    }
    bool hideWindow = (vis == ConsoleVisibility::Hide);

    std::wstring workDir = ResolveWorkingDir(item.runner, ctx);
    std::wstring primaryTarget = ctx.paths.empty() ? L"" : ctx.paths[0];
    std::wstring expandedArgs = ExpandTemplate(item.runner.arguments, primaryTarget, ctx);

    std::wstring exe;
    std::wstring finalArgs;

    switch (item.runner.type) {
    case RunnerType::PowerShell: {
        exe = FindPowerShellExe(config.execution.defaultPowerShell);
        std::wstring scriptOrCmd = ExpandTemplate(item.runner.executable, primaryTarget, ctx);
        if (!scriptOrCmd.empty() && PathFileExistsW(scriptOrCmd.c_str())) {
            finalArgs = L"-NoProfile -ExecutionPolicy Bypass -File " + QuoteArg(scriptOrCmd);
            if (!expandedArgs.empty()) finalArgs += L" " + expandedArgs;
        } else {
            std::wstring combined = scriptOrCmd;
            if (!expandedArgs.empty()) {
                if (!combined.empty()) combined += L" ";
                combined += expandedArgs;
            }
            finalArgs = L"-NoProfile -ExecutionPolicy Bypass -Command " + QuoteArg(combined);
        }
        break;
    }
    case RunnerType::Cmd: {
        exe = L"cmd.exe";
        std::wstring cmdText = ExpandTemplate(item.runner.executable, primaryTarget, ctx);
        if (!expandedArgs.empty()) {
            if (!cmdText.empty()) cmdText += L" ";
            cmdText += expandedArgs;
        }
        finalArgs = (hideWindow ? L"/c " : L"/k ") + cmdText;
        break;
    }
    case RunnerType::WindowsTerminal: {
        exe = L"wt.exe";
        std::wstring d = workDir;
        if (!d.empty() && d.back() == L'\\') d += L'\\';
        if (!d.empty()) finalArgs = L"-d \"" + d + L"\" ";
        std::wstring sub = ExpandTemplate(item.runner.executable, primaryTarget, ctx);
        if (!sub.empty()) finalArgs += sub + L" ";
        finalArgs += expandedArgs;
        hideWindow = false; // Windows Terminal is a GUI host
        break;
    }
    case RunnerType::Executable:
    default: {
        exe = ExpandTemplate(item.runner.executable, primaryTarget, ctx);
        finalArgs = expandedArgs;
        break;
    }
    }

    ExecutionLogEntry logEntry;
    logEntry.commandId = item.id;
    logEntry.title = item.title;
    logEntry.executable = exe;
    logEntry.arguments = finalArgs;
    logEntry.workingDirectory = workDir;
    logEntry.consoleVisibility = hideWindow ? L"hide" : L"show";
    logEntry.targets = ctx.paths;

    int exitCode = 0;

    if (item.runner.runAsAdmin) {
        AllowSetForegroundWindow(ASFW_ANY);
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = exe.c_str();
        sei.lpParameters = finalArgs.empty() ? nullptr : finalArgs.c_str();
        sei.lpDirectory = workDir.empty() ? nullptr : workDir.c_str();
        sei.nShow = hideWindow ? SW_HIDE : SW_SHOWNORMAL;

        if (!ShellExecuteExW(&sei)) {
            DWORD err = GetLastError();
            logEntry.success = false;
            logEntry.exitCode = static_cast<int>(err);
            logEntry.errorMessage = L"ShellExecuteExW failed with error " + std::to_wstring(err);
            exitCode = static_cast<int>(err);
        } else if (sei.hProcess) {
            if (hideWindow) {
                DWORD timeoutMs = static_cast<DWORD>((item.runner.timeoutSeconds > 0 ? item.runner.timeoutSeconds : config.execution.defaultTimeoutSeconds) * 1000);
                if (WaitForSingleObject(sei.hProcess, timeoutMs) == WAIT_OBJECT_0) {
                    DWORD code = 0;
                    GetExitCodeProcess(sei.hProcess, &code);
                    exitCode = static_cast<int>(code);
                    logEntry.exitCode = exitCode;
                    logEntry.success = (exitCode == 0);
                }
            }
            CloseHandle(sei.hProcess);
        }
    } else if (hideWindow) {
        // Hidden execution with stdout/stderr capture & timeout!
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE hRead = nullptr, hWrite = nullptr;
        bool capture = config.execution.logHiddenCommandOutput && item.runner.logOutput;
        if (capture) {
            CreatePipe(&hRead, &hWrite, &sa, 0);
            SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW | (capture ? STARTF_USESTDHANDLES : 0);
        si.wShowWindow = SW_HIDE;
        if (capture) {
            si.hStdOutput = hWrite;
            si.hStdError = hWrite;
        }

        std::wstring cmdLine = QuoteArg(exe);
        if (!finalArgs.empty()) cmdLine += L" " + finalArgs;
        std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back(L'\0');

        std::vector<wchar_t> envBlock = BuildEnvironmentBlock(item.runner.environment);
        PROCESS_INFORMATION pi{};
        DWORD flags = CREATE_NO_WINDOW | (envBlock.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT);

        if (!CreateProcessW(
                nullptr,
                mutableCmd.data(),
                nullptr,
                nullptr,
                capture ? TRUE : FALSE,
                flags,
                envBlock.empty() ? nullptr : envBlock.data(),
                workDir.empty() ? nullptr : workDir.c_str(),
                &si,
                &pi)) {
            DWORD err = GetLastError();
            if (hRead) CloseHandle(hRead);
            if (hWrite) CloseHandle(hWrite);
            logEntry.success = false;
            logEntry.exitCode = static_cast<int>(err);
            logEntry.errorMessage = L"CreateProcessW failed with error " + std::to_wstring(err);
            exitCode = static_cast<int>(err);
        } else {
            if (hWrite) CloseHandle(hWrite);

            DWORD timeoutMs = static_cast<DWORD>((item.runner.timeoutSeconds > 0 ? item.runner.timeoutSeconds : config.execution.defaultTimeoutSeconds) * 1000);
            ULONGLONG startTick = GetTickCount64();
            std::string captured;
            char buf[4096];

            while (true) {
                if (hRead) {
                    DWORD avail = 0;
                    if (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                        DWORD read = 0;
                        if (ReadFile(hRead, buf, sizeof(buf), &read, nullptr) && read > 0) {
                            if (captured.size() < 16384) {
                                captured.append(buf, read);
                            }
                        }
                        continue;
                    }
                }
                DWORD waitRes = WaitForSingleObject(pi.hProcess, 50);
                if (waitRes == WAIT_OBJECT_0) {
                    if (hRead) {
                        DWORD avail = 0;
                        while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                            DWORD read = 0;
                            if (ReadFile(hRead, buf, sizeof(buf), &read, nullptr) && read > 0 && captured.size() < 16384) {
                                captured.append(buf, read);
                            } else {
                                break;
                            }
                        }
                    }
                    DWORD code = 0;
                    GetExitCodeProcess(pi.hProcess, &code);
                    exitCode = static_cast<int>(code);
                    logEntry.exitCode = exitCode;
                    logEntry.success = (exitCode == 0);
                    break;
                }
                if (timeoutMs > 0 && (GetTickCount64() - startTick) >= timeoutMs) {
                    TerminateProcess(pi.hProcess, 124);
                    exitCode = 124;
                    logEntry.exitCode = 124;
                    logEntry.success = false;
                    logEntry.errorMessage = L"Command timed out after " + std::to_wstring(timeoutMs / 1000) + L"s";
                    break;
                }
            }
            if (hRead) CloseHandle(hRead);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            logEntry.outputSnippet = Utf8ToWide(captured);
        }
    } else {
        // Visible execution
        AllowSetForegroundWindow(ASFW_ANY);
        std::wstring cmdLine = QuoteArg(exe);
        if (!finalArgs.empty()) cmdLine += L" " + finalArgs;
        std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back(L'\0');

        std::vector<wchar_t> envBlock = BuildEnvironmentBlock(item.runner.environment);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOWNORMAL;
        PROCESS_INFORMATION pi{};
        DWORD flags = (envBlock.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT);

        if (!CreateProcessW(
                nullptr,
                mutableCmd.data(),
                nullptr,
                nullptr,
                FALSE,
                flags,
                envBlock.empty() ? nullptr : envBlock.data(),
                workDir.empty() ? nullptr : workDir.c_str(),
                &si,
                &pi)) {
            // Fallback to ShellExecuteW for documents/verbs/AppExecutionAliases
            HINSTANCE res = ShellExecuteW(
                nullptr,
                L"open",
                exe.c_str(),
                finalArgs.empty() ? nullptr : finalArgs.c_str(),
                workDir.empty() ? nullptr : workDir.c_str(),
                SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(res) <= 32) {
                logEntry.success = false;
                logEntry.exitCode = static_cast<int>(reinterpret_cast<INT_PTR>(res));
                logEntry.errorMessage = L"Failed to launch process: " + exe;
                exitCode = 1;
            }
        } else {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    logEntry.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    if (config.execution.enableExecutionLogging) {
        AppendExecutionLog(logEntry);
    }

    return exitCode;
}

int ExecuteMenuItem(const AppConfig& config, const MenuItem& item, const SelectionContext& ctx, POINT cursorPos) {
    if (item.builtIn && !item.builtInAction.empty()) {
        auto startTime = std::chrono::steady_clock::now();
        int rc = RunBuiltInAction(config, item, ctx, cursorPos);
        if (config.execution.enableExecutionLogging && item.builtInAction != L"settings") {
            ExecutionLogEntry e;
            e.commandId = item.id;
            e.title = item.title;
            e.executable = L"builtin:" + item.builtInAction;
            e.workingDirectory = ResolveTargetDir(ctx);
            e.consoleVisibility = L"builtin";
            e.targets = ctx.paths;
            e.exitCode = rc;
            e.success = (rc == 0);
            e.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startTime)
                               .count();
            AppendExecutionLog(e);
        }
        return rc;
    }
    return RunCustomCommand(config, item, ctx);
}

int ExecuteCommandById(const AppConfig& config, const std::wstring& commandId, const SelectionContext& ctx, POINT cursorPos) {
    const MenuItem* item = FindItemById(config, commandId);
    if (!item) {
        AppendDiagnosticLog(L"WARN", L"Command not found: " + commandId);
        return 1;
    }
    return ExecuteMenuItem(config, *item, ctx, cursorPos);
}

} // namespace xmf::host
