#pragma once

#include "../common/Config.h"
#include <windows.h>
#include <string>
#include <vector>

namespace xmf::host {

void ShowAttributesDialog(const std::vector<std::wstring>& paths);
void ShowTakeOwnershipDialog(const std::vector<std::wstring>& paths, bool alreadyElevatedFlag);
void ShowCategoryFlyout(const AppConfig& config, const std::wstring& categoryId, const SelectionContext& ctx, POINT pt);
void ShowEditWithPopup(const std::vector<std::wstring>& paths, POINT pt);
void ShowSystemFoldersPopup(POINT pt);

} // namespace xmf::host
