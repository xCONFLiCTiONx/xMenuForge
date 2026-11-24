#include "FileTypes.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <unordered_map>

namespace xmf {

std::wstring NormalizeExtension(const std::wstring& extOrPath) {
    std::wstring ext = extOrPath;
    if (ext.find(L'\\') != std::wstring::npos || ext.find(L'/') != std::wstring::npos) {
        ext = std::filesystem::path(ext).extension().wstring();
    } else if (!ext.empty() && ext[0] != L'.') {
        ext = L"." + ext;
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext;
}

std::wstring GetFileCategoryForExtension(const std::wstring& rawExt) {
    static const std::unordered_map<std::wstring, std::wstring> kMap = {
        // Images
        {L".jpg", L"Images"}, {L".jpeg", L"Images"}, {L".png", L"Images"}, {L".gif", L"Images"},
        {L".bmp", L"Images"}, {L".webp", L"Images"}, {L".tif", L"Images"}, {L".tiff", L"Images"},
        {L".ico", L"Images"}, {L".svg", L"Images"}, {L".avif", L"Images"}, {L".heic", L"Images"},
        // Text
        {L".txt", L"Text"}, {L".log", L"Text"}, {L".ini", L"Text"}, {L".cfg", L"Text"},
        {L".conf", L"Text"}, {L".md", L"Text"}, {L".csv", L"Text"}, {L".tsv", L"Text"},
        // Code
        {L".c", L"Code"}, {L".h", L"Code"}, {L".cpp", L"Code"}, {L".hpp", L"Code"}, {L".cc", L"Code"},
        {L".cs", L"Code"}, {L".py", L"Code"}, {L".js", L"Code"}, {L".ts", L"Code"}, {L".jsx", L"Code"},
        {L".tsx", L"Code"}, {L".json", L"Code"}, {L".xml", L"Code"}, {L".yaml", L"Code"}, {L".yml", L"Code"},
        {L".toml", L"Code"}, {L".html", L"Code"}, {L".css", L"Code"}, {L".scss", L"Code"}, {L".rs", L"Code"},
        {L".go", L"Code"}, {L".java", L"Code"}, {L".kt", L"Code"}, {L".sql", L"Code"}, {L".sh", L"Code"},
        {L".ps1", L"Code"}, {L".psm1", L"Code"}, {L".psd1", L"Code"}, {L".bat", L"Code"}, {L".cmd", L"Code"},
        // Documents
        {L".pdf", L"Documents"}, {L".doc", L"Documents"}, {L".docx", L"Documents"}, {L".xls", L"Documents"},
        {L".xlsx", L"Documents"}, {L".ppt", L"Documents"}, {L".pptx", L"Documents"}, {L".odt", L"Documents"},
        {L".ods", L"Documents"}, {L".odp", L"Documents"}, {L".rtf", L"Documents"},
        // Audio
        {L".mp3", L"Audio"}, {L".wav", L"Audio"}, {L".flac", L"Audio"}, {L".ogg", L"Audio"},
        {L".opus", L"Audio"}, {L".m4a", L"Audio"}, {L".aac", L"Audio"}, {L".wma", L"Audio"},
        // Video
        {L".mp4", L"Video"}, {L".mkv", L"Video"}, {L".avi", L"Video"}, {L".mov", L"Video"},
        {L".webm", L"Video"}, {L".wmv", L"Video"}, {L".m4v", L"Video"}, {L".mpeg", L"Video"}, {L".mpg", L"Video"},
        // Archives
        {L".zip", L"Archives"}, {L".7z", L"Archives"}, {L".rar", L"Archives"}, {L".tar", L"Archives"},
        {L".gz", L"Archives"}, {L".bz2", L"Archives"}, {L".xz", L"Archives"}, {L".zst", L"Archives"},
        // Disk Images
        {L".iso", L"DiskImages"}, {L".img", L"DiskImages"}, {L".vhd", L"DiskImages"}, {L".vhdx", L"DiskImages"},
        {L".vmdk", L"DiskImages"}, {L".vdi", L"DiskImages"}, {L".qcow", L"DiskImages"}, {L".qcow2", L"DiskImages"},
        // Executables
        {L".exe", L"Executables"}, {L".msi", L"Executables"}, {L".msix", L"Executables"},
        {L".com", L"Executables"}, {L".scr", L"Executables"}, {L".dll", L"Executables"}
    };

    std::wstring ext = NormalizeExtension(rawExt);
    auto it = kMap.find(ext);
    return it != kMap.end() ? it->second : L"Other";
}

std::wstring GetFileCategoryForPath(const std::wstring& path) {
    return GetFileCategoryForExtension(std::filesystem::path(path).extension().wstring());
}

std::vector<std::wstring> GetAllFileCategories() {
    return {L"Images", L"Text", L"Code", L"Documents", L"Audio", L"Video", L"Archives", L"DiskImages", L"Executables", L"Other"};
}

} // namespace xmf
