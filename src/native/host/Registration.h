#pragma once

#include <string>

namespace xmf::reg {

struct RegistrationStatus {
    bool modernPackageRegistered = false;
    std::wstring packageFullName;
    bool modernComRegistered = false;
    bool classicComRegistered = false;
    bool classicHandlersRegistered = false;
    bool shellDllExists = false;
    bool hostExeExists = false;
    bool settingsExeExists = false;
    bool manifestExists = false;
};

RegistrationStatus QueryStatus(const std::wstring& installDir);
bool RegisterAll(const std::wstring& installDir, bool enableClassic, bool restartExplorer, std::wstring& outMessage);
bool UnregisterAll(bool restartExplorer, std::wstring& outMessage);

} // namespace xmf::reg
