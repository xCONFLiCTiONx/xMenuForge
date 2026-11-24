#pragma once

#include <string>
#include <vector>

namespace xmf {

std::wstring NormalizeExtension(const std::wstring& extOrPath);
std::wstring GetFileCategoryForPath(const std::wstring& path);
std::wstring GetFileCategoryForExtension(const std::wstring& ext);
std::vector<std::wstring> GetAllFileCategories();

} // namespace xmf
