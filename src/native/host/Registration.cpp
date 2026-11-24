#include "Registration.h"
#include "../common/Config.h"

#include <windows.h>
#include <shlwapi.h>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace xmf::reg {

static constexpr const wchar_t* kPackageName = L"xCONFLiCTiONx.xMenuForge";
static constexpr const wchar_t* kModernClsid = L"{7A4E9C10-5B2D-4F81-A310-9C6E2B410001}";
static constexpr const wchar_t* kClassicClsid = L"{7A4E9C10-5B2D-4F81-A310-9C6E2B410002}";

static bool SetHkcuString(const std::wstring& subKey, const std::wstring& valueName, const std::wstring& data) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG rc = RegSetValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(), 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(data.c_str()),
                             static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

static bool HkcuKeyExists(const std::wstring& subKey) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

static void DeleteHkcuTree(const std::wstring& subKey) {
    RegDeleteTreeW(HKEY_CURRENT_USER, subKey.c_str());
}

static bool RunPowerShell(const std::wstring& script, std::wstring& outText, DWORD timeoutMs = 45000) {
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + script + L"\"";
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hRead = nullptr, hWrite = nullptr;
    CreatePipe(&hRead, &hWrite, &sa, 0);
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        outText = L"Failed to start PowerShell.";
        return false;
    }
    CloseHandle(hWrite);

    std::string raw;
    char buf[4096];
    ULONGLONG start = GetTickCount64();
    while (true) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD rd = 0;
            if (ReadFile(hRead, buf, sizeof(buf), &rd, nullptr) && rd > 0) raw.append(buf, rd);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD rd = 0;
                if (ReadFile(hRead, buf, sizeof(buf), &rd, nullptr) && rd > 0) raw.append(buf, rd);
                else break;
            }
            break;
        }
        if (GetTickCount64() - start >= timeoutMs) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(hRead);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            outText = L"PowerShell timed out.";
            return false;
        }
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(hRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    outText = Utf8ToWide(raw);
    return exitCode == 0;
}

RegistrationStatus QueryStatus(const std::wstring& installDir) {
    RegistrationStatus st;
    fs::path dir(installDir);
    st.shellDllExists = fs::exists(dir / L"xMenuForge.Shell.dll");
    st.hostExeExists = fs::exists(dir / L"xMenuForge.Host.exe");
    st.settingsExeExists = fs::exists(dir / L"xMenuForge.Settings.exe");
    st.manifestExists = fs::exists(dir / L"AppxManifest.xml");

    st.modernComRegistered = HkcuKeyExists(std::wstring(L"Software\\Classes\\CLSID\\") + kModernClsid + L"\\InprocServer32");
    st.classicComRegistered = HkcuKeyExists(std::wstring(L"Software\\Classes\\CLSID\\") + kClassicClsid + L"\\InprocServer32");
    st.classicHandlersRegistered =
        HkcuKeyExists(L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\xMenuForge") &&
        HkcuKeyExists(L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\xMenuForge") &&
        HkcuKeyExists(L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\xMenuForge");

    std::wstring psOut;
    if (RunPowerShell(L"$p = Get-AppxPackage -Name 'xCONFLiCTiONx.xMenuForge' -ErrorAction SilentlyContinue; if ($p) { Write-Output $p.PackageFullName }", psOut, 15000)) {
        while (!psOut.empty() && (psOut.back() == L'\r' || psOut.back() == L'\n' || psOut.back() == L' ')) psOut.pop_back();
        if (!psOut.empty()) {
            st.modernPackageRegistered = true;
            st.packageFullName = psOut;
        }
    }
    return st;
}

bool RegisterAll(const std::wstring& installDir, bool enableClassic, bool restartExplorer, std::wstring& outMessage) {
    fs::path dir(installDir);
    fs::path dllPath = dir / L"xMenuForge.Shell.dll";
    fs::path manifestPath = dir / L"AppxManifest.xml";

    if (!fs::exists(dllPath) || !fs::exists(manifestPath)) {
        outMessage = L"Missing xMenuForge.Shell.dll or AppxManifest.xml in " + installDir;
        AppendDiagnosticLog(L"ERROR", outMessage);
        return false;
    }

    // 1. Register HKCU COM InprocServer32 for Modern IExplorerCommand
    std::wstring modernKey = std::wstring(L"Software\\Classes\\CLSID\\") + kModernClsid + L"\\InprocServer32";
    SetHkcuString(modernKey, L"", dllPath.wstring());
    SetHkcuString(modernKey, L"ThreadingModel", L"Apartment");

    // 2. Classic COM + ContextMenuHandlers (only if enabled; otherwise clean up)
    std::wstring classicKey = std::wstring(L"Software\\Classes\\CLSID\\") + kClassicClsid;
    if (enableClassic) {
        SetHkcuString(classicKey + L"\\InprocServer32", L"", dllPath.wstring());
        SetHkcuString(classicKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
        SetHkcuString(L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\xMenuForge", L"", kClassicClsid);
        SetHkcuString(L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\xMenuForge", L"", kClassicClsid);
        SetHkcuString(L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\xMenuForge", L"", kClassicClsid);
    } else {
        DeleteHkcuTree(classicKey);
        DeleteHkcuTree(L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\xMenuForge");
        DeleteHkcuTree(L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\xMenuForge");
        DeleteHkcuTree(L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\xMenuForge");
    }

    // 3. Register Sparse Package for Windows 11 Modern Context Menu
    std::wstring psOut;
    std::wstring script =
        L"$ProgressPreference='SilentlyContinue'; "
        L"Get-AppxPackage -Name '" + std::wstring(kPackageName) + L"' -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue; "
        L"Add-AppxPackage -Register -ForceUpdateFromAnyVersion -Path '" + manifestPath.wstring() + L"' -ExternalLocation '" + dir.wstring() + L"'";
    bool ok = RunPowerShell(script, psOut);
    if (!ok) {
        outMessage = L"Sparse package registration failed: " + psOut;
        AppendDiagnosticLog(L"ERROR", outMessage);
        return false;
    }

    if (restartExplorer) {
        std::wstring expOut;
        RunPowerShell(L"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 600; if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }", expOut, 15000);
    }

    outMessage = L"xMenuForge Explorer integration registered successfully.";
    AppendDiagnosticLog(L"INFO", outMessage);
    return true;
}

bool UnregisterAll(bool restartExplorer, std::wstring& outMessage) {
    std::wstring psOut;
    RunPowerShell(
        L"$ProgressPreference='SilentlyContinue'; "
        L"Get-AppxPackage -Name '" + std::wstring(kPackageName) + L"' -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue",
        psOut);

    DeleteHkcuTree(std::wstring(L"Software\\Classes\\CLSID\\") + kModernClsid);
    DeleteHkcuTree(std::wstring(L"Software\\Classes\\CLSID\\") + kClassicClsid);
    DeleteHkcuTree(L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\xMenuForge");
    DeleteHkcuTree(L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\xMenuForge");
    DeleteHkcuTree(L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\xMenuForge");

    if (restartExplorer) {
        std::wstring expOut;
        RunPowerShell(L"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 600; if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }", expOut, 15000);
    }

    outMessage = L"xMenuForge Explorer integration unregistered. User configuration preserved.";
    AppendDiagnosticLog(L"INFO", outMessage);
    return true;
}

} // namespace xmf::reg
