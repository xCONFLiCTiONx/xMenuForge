#pragma once

#include "../common/Config.h"
#include <string>

namespace xmf::host {

int ExecuteCommandById(
    const AppConfig& config,
    const std::wstring& commandId,
    const SelectionContext& ctx,
    POINT cursorPos);

int ExecuteMenuItem(
    const AppConfig& config,
    const MenuItem& item,
    const SelectionContext& ctx,
    POINT cursorPos);

std::wstring GetHostDirectory();

} // namespace xmf::host
