#include "Registration.h"
#include "../common/Config.h"

#include <windows.h>
#include <shlwapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace xmf::reg {

static constexpr int kSlotCount = 12;
static constexpr const wchar_t* kPackageName = L"xCONFLiCTiONx.xMenuForge";
static constexpr const wchar_t* kModernClsid = L"{7A4E9C10-5B2D-4F81-A310-9C6E2B410001}";
static constexpr const wchar_t* kClassicClsid = L"{7A4E9C10-5B2D-4F81-A310-9C6E2B410002}";

static std::wstring GetSlotClsidNoBraces(int slot) {
    if (slot == 0) return L"7A4E9C10-5B2D-4F81-A310-9C6E2B410001";
    wchar_t buf[64]{};
    swprintf_s(buf, L"7A4E9C10-5B2D-4F81-A310-9C6E2B4100%02X", 0x10 + slot);
    return buf;
}

static std::wstring GetSlotClsid(int slot) {
    return L"{" + GetSlotClsidNoBraces(slot) + L"}";
}

static std::wstring GetSlotPackageName(int slot) {
    if (slot == 0) return kPackageName;
    return std::wstring(kPackageName) + L".Slot" + std::to_wstring(slot);
}

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

static bool SetHkcuDword(const std::wstring& subKey, const std::wstring& valueName, DWORD data) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG rc = RegSetValueExW(hKey, valueName.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data));
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

static bool RunPowerShell(const std::wstring& script, std::wstring& outText, DWORD timeoutMs = 90000) {
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

static bool WriteSlotManifest(const fs::path& manifestPath, int slot) {
    std::string pkgName = WideToUtf8(GetSlotPackageName(slot));
    std::string clsid = WideToUtf8(GetSlotClsidNoBraces(slot));
    std::string appId = "xMenuForgeSlot" + std::to_string(slot);
    std::string displayName = "xMenuForge Slot " + std::to_string(slot);

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<Package\n"
        << "  xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"\n"
        << "  xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"\n"
        << "  xmlns:uap10=\"http://schemas.microsoft.com/appx/manifest/uap/windows10/10\"\n"
        << "  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"\n"
        << "  xmlns:desktop4=\"http://schemas.microsoft.com/appx/manifest/desktop/windows10/4\"\n"
        << "  xmlns:desktop5=\"http://schemas.microsoft.com/appx/manifest/desktop/windows10/5\"\n"
        << "  xmlns:desktop6=\"http://schemas.microsoft.com/appx/manifest/desktop/windows10/6\"\n"
        << "  xmlns:com=\"http://schemas.microsoft.com/appx/manifest/com/windows10\"\n"
        << "  IgnorableNamespaces=\"uap uap10 rescap desktop4 desktop5 desktop6 com\">\n"
        << "  <Identity Name=\"" << pkgName << "\" Publisher=\"CN=xCONFLiCTiONx\" Version=\"1.0.0.0\" />\n"
        << "  <Properties>\n"
        << "    <DisplayName>" << displayName << "</DisplayName>\n"
        << "    <PublisherDisplayName>xCONFLiCTiONx</PublisherDisplayName>\n"
        << "    <Logo>Assets\\StoreLogo.png</Logo>\n"
        << "    <uap10:AllowExternalContent>true</uap10:AllowExternalContent>\n"
        << "    <desktop6:RegistryWriteVirtualization>disabled</desktop6:RegistryWriteVirtualization>\n"
        << "    <desktop6:FileSystemWriteVirtualization>disabled</desktop6:FileSystemWriteVirtualization>\n"
        << "  </Properties>\n"
        << "  <Dependencies>\n"
        << "    <TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.19041.0\" MaxVersionTested=\"10.0.26100.0\" />\n"
        << "  </Dependencies>\n"
        << "  <Capabilities>\n"
        << "    <rescap:Capability Name=\"runFullTrust\" />\n"
        << "    <rescap:Capability Name=\"unvirtualizedResources\" />\n"
        << "  </Capabilities>\n"
        << "  <Applications>\n"
        << "    <Application Id=\"" << appId << "\" Executable=\"xMenuForge.Host.exe\" EntryPoint=\"Windows.FullTrustApplication\">\n"
        << "      <uap:VisualElements DisplayName=\"" << displayName << "\" Description=\"xMenuForge context menu slot\" BackgroundColor=\"#0E1015\" Square150x150Logo=\"Assets\\StoreLogo.png\" Square44x44Logo=\"Assets\\Square44x44Logo.png\" AppListEntry=\"none\" />\n"
        << "      <Extensions>\n"
        << "        <com:Extension Category=\"windows.comServer\">\n"
        << "          <com:ComServer>\n"
        << "            <com:SurrogateServer>\n"
        << "              <com:Class Id=\"" << clsid << "\" Path=\"xMenuForge.Shell.dll\" ThreadingModel=\"STA\" />\n"
        << "            </com:SurrogateServer>\n"
        << "          </com:ComServer>\n"
        << "        </com:Extension>\n"
        << "        <desktop4:Extension Category=\"windows.fileExplorerContextMenus\">\n"
        << "          <desktop4:FileExplorerContextMenus>\n"
        << "            <desktop5:ItemType Type=\"*\"><desktop5:Verb Id=\"xmfFiles" << slot << "\" Clsid=\"" << clsid << "\" /></desktop5:ItemType>\n"
        << "            <desktop5:ItemType Type=\"Directory\"><desktop5:Verb Id=\"xmfDir" << slot << "\" Clsid=\"" << clsid << "\" /></desktop5:ItemType>\n"
        << "            <desktop5:ItemType Type=\"Directory\\Background\"><desktop5:Verb Id=\"xmfBg" << slot << "\" Clsid=\"" << clsid << "\" /></desktop5:ItemType>\n"
        << "          </desktop4:FileExplorerContextMenus>\n"
        << "        </desktop4:Extension>\n"
        << "      </Extensions>\n"
        << "    </Application>\n"
        << "  </Applications>\n"
        << "</Package>\n";

    std::ofstream ofs(manifestPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::string content = xml.str();
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return ofs.good();
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

    // 0. Ensure AppContainer / Windows 11 PackagedCom can read & execute the shell DLL
    std::wstring aclOut;
    RunPowerShell(
        L"icacls '" + dir.wstring() + L"' /grant '*S-1-15-2-1:(OI)(CI)RX' /grant '*S-1-15-2-2:(OI)(CI)RX' /T /Q | Out-Null",
        aclOut,
        15000);

    // 1. Register HKCU COM InprocServer32 for all 12 Modern IExplorerCommand slots
    for (int slot = 0; slot < kSlotCount; ++slot) {
        std::wstring key = L"Software\\Classes\\CLSID\\" + GetSlotClsid(slot) + L"\\InprocServer32";
        SetHkcuString(key, L"", dllPath.wstring());
        SetHkcuString(key, L"ThreadingModel", L"Apartment");
    }

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

    // 3. Generate per-slot Sparse Package manifests in <installDir>\Slots\Slot<N>\AppxManifest.xml
    //    and register all 12 native Win11 top-level slots in a single PowerShell call!
    fs::path slotsRoot = dir / L"Slots";
    std::error_code ec;
    fs::create_directories(slotsRoot, ec);

    std::wostringstream ps;
    ps << L"$ProgressPreference='SilentlyContinue'; "
       << L"Restart-Service AppXSvc -Force -ErrorAction SilentlyContinue; "
       << L"Get-AppxPackage -Name 'xCONFLiCTiONx.xMenuForge*' -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue; "
       << L"Add-AppxPackage -Register -ForceUpdateFromAnyVersion -Path '" << manifestPath.wstring() << L"' -ExternalLocation '" << dir.wstring() << L"'; ";

    for (int slot = 1; slot < kSlotCount; ++slot) {
        fs::path slotDir = slotsRoot / (L"Slot" + std::to_wstring(slot));
        fs::create_directories(slotDir, ec);
        fs::path slotManifest = slotDir / L"AppxManifest.xml";
        if (WriteSlotManifest(slotManifest, slot)) {
            ps << L"Add-AppxPackage -Register -ForceUpdateFromAnyVersion -Path '" << slotManifest.wstring()
               << L"' -ExternalLocation '" << dir.wstring() << L"'; ";
        }
    }

    std::wstring psOut;
    bool ok = RunPowerShell(ps.str(), psOut, 90000);
    if (!ok) {
        outMessage = L"Sparse package registration failed: " + psOut;
        AppendDiagnosticLog(L"ERROR", outMessage);
        return false;
    }

    // 4. Enable all xMenuForge slot packages in Windows 11 26H2+ Explorer ContextMenu Preferences
    std::wstring pfnOut;
    if (RunPowerShell(L"Get-AppxPackage -Name 'xCONFLiCTiONx.xMenuForge*' -ErrorAction SilentlyContinue | ForEach-Object { Write-Output $_.PackageFamilyName }", pfnOut, 15000)) {
        std::wistringstream iss(pfnOut);
        std::wstring line;
        while (std::getline(iss, line)) {
            while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ')) line.pop_back();
            if (!line.empty()) {
                SetHkcuDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ContextMenu\\Preferences\\extensions\\" + line, L"Enabled", 1);
            }
        }
    }

    if (restartExplorer) {
        std::wstring expOut;
        RunPowerShell(L"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 600; if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }", expOut, 15000);
    }

    outMessage = L"xMenuForge Explorer integration (12 native Windows 11 top-level slots) registered successfully.";
    AppendDiagnosticLog(L"INFO", outMessage);
    return true;
}

bool UnregisterAll(bool restartExplorer, std::wstring& outMessage) {
    // Clean up all Windows 11 26H2 ContextMenu preferences for xMenuForge slots
    std::wstring pfnOut;
    if (RunPowerShell(L"Get-AppxPackage -Name 'xCONFLiCTiONx.xMenuForge*' -ErrorAction SilentlyContinue | ForEach-Object { Write-Output $_.PackageFamilyName }", pfnOut, 15000)) {
        std::wistringstream iss(pfnOut);
        std::wstring line;
        while (std::getline(iss, line)) {
            while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ')) line.pop_back();
            if (!line.empty()) {
                DeleteHkcuTree(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ContextMenu\\Preferences\\extensions\\" + line);
            }
        }
    }

    std::wstring psOut;
    RunPowerShell(
        L"$ProgressPreference='SilentlyContinue'; "
        L"Get-AppxPackage -Name 'xCONFLiCTiONx.xMenuForge*' -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue",
        psOut, 60000);

    for (int slot = 0; slot < kSlotCount; ++slot) {
        DeleteHkcuTree(L"Software\\Classes\\CLSID\\" + GetSlotClsid(slot));
    }
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
