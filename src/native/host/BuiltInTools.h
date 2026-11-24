#pragma once

#include "../common/Config.h"
#include <string>

namespace xmf::tools {

// Returns true if the built-in action name was handled by BuiltInTools, setting outExitCode.
bool TryRunExtendedBuiltIn(
    const AppConfig& config,
    const std::wstring& action,
    const SelectionContext& ctx,
    const std::wstring& targetDir,
    int& outExitCode);

} // namespace xmf::tools
