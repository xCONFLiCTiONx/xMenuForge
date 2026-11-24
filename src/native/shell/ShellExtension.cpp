#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "../common/Config.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {

// {7A4E9C10-5B2D-4F81-A310-9C6E2B410001}
constexpr CLSID CLSID_XMenuForgeRoot =
    {0x7a4e9c10, 0x5b2d, 0x4f81, {0xa3, 0x10, 0x9c, 0x6e, 0x2b, 0x41, 0x00, 0x01}};

// {7A4E9C10-5B2D-4F81-A310-9C6E2B410002}
constexpr CLSID CLSID_XMenuForgeClassic =
    {0x7a4e9c10, 0x5b2d, 0x4f81, {0xa3, 0x10, 0x9c, 0x6e, 0x2b, 0x41, 0x00, 0x02}};

HMODULE g_hModule = nullptr;
std::atomic_ulong g_objects{0};
std::atomic_ulong g_locks{0};

std::wstring GetModuleDir() {
    WCHAR buf[MAX_PATH] = {};
    if (GetModuleFileNameW(g_hModule, buf, MAX_PATH) == 0) return {};
    PathRemoveFileSpecW(buf);
    return buf;
}

xmf::SelectionContext ExtractContext(IShellItemArray* items, IUnknown* site) {
    xmf::SelectionContext ctx;
    if (items) {
        DWORD count = 0;
        if (SUCCEEDED(items->GetCount(&count)) && count > 0) {
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                        ctx.paths.push_back(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
        }
    }

    if (ctx.paths.empty()) {
        ctx.isBackground = true;
        if (site) {
            IServiceProvider* sp = nullptr;
            if (SUCCEEDED(site->QueryInterface(IID_PPV_ARGS(&sp))) && sp) {
                IShellBrowser* sb = nullptr;
                if (SUCCEEDED(sp->QueryService(SID_SShellBrowser, IID_PPV_ARGS(&sb))) && sb) {
                    IShellView* sv = nullptr;
                    if (SUCCEEDED(sb->QueryActiveShellView(&sv)) && sv) {
                        IFolderView* fv = nullptr;
                        if (SUCCEEDED(sv->QueryInterface(IID_PPV_ARGS(&fv))) && fv) {
                            IShellItem* folder = nullptr;
                            if (SUCCEEDED(fv->GetFolder(IID_PPV_ARGS(&folder))) && folder) {
                                PWSTR path = nullptr;
                                if (SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                                    ctx.paths.push_back(path);
                                    CoTaskMemFree(path);
                                }
                                folder->Release();
                            }
                            fv->Release();
                        }
                        sv->Release();
                    }
                    sb->Release();
                }
                sp->Release();
            }
        }
    } else {
        DWORD attrs = GetFileAttributesW(ctx.paths[0].c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            ctx.isFolder = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
    }
    return ctx;
}

std::wstring WriteTargetsTempFile(const std::vector<std::wstring>& paths) {
    WCHAR tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return {};
    static std::atomic_ulong seq{0};
    std::wstring filePath = std::wstring(tempDir) + L"xmf_targets_" +
                            std::to_wstring(GetCurrentProcessId()) + L"_" +
                            std::to_wstring(GetTickCount64()) + L"_" +
                            std::to_wstring(++seq) + L".txt";
    std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};
    for (const auto& p : paths) {
        ofs << xmf::WideToUtf8(p) << "\n";
    }
    ofs.close();
    return filePath;
}

void LaunchHost(const std::wstring& modeFlag, const std::wstring& itemId, const xmf::SelectionContext& ctx) {
    std::wstring hostExe = GetModuleDir() + L"\\xMenuForge.Host.exe";
    if (!PathFileExistsW(hostExe.c_str())) {
        xmf::AppendDiagnosticLog(L"ERROR", L"xMenuForge.Host.exe not found at: " + hostExe);
        return;
    }

    std::wstring targetsFile = WriteTargetsTempFile(ctx.paths);
    std::wstring cmdLine = L"\"" + hostExe + L"\" " + modeFlag + L" \"" + itemId + L"\"";
    if (!targetsFile.empty()) {
        cmdLine += L" --targets-file \"" + targetsFile + L"\"";
    }
    if (ctx.isBackground) {
        cmdLine += L" --background";
    }
    if (ctx.isFolder) {
        cmdLine += L" --folder";
    }

    POINT pt{};
    if (GetCursorPos(&pt)) {
        cmdLine += L" --x " + std::to_wstring(pt.x) + L" --y " + std::to_wstring(pt.y);
    }

    AllowSetForegroundWindow(ASFW_ANY);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    if (CreateProcessW(
            hostExe.c_str(),
            mutableCmd.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            GetModuleDir().c_str(),
            &si,
            &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        xmf::AppendDiagnosticLog(L"ERROR", L"Failed to launch xMenuForge.Host.exe (error " + std::to_wstring(GetLastError()) + L")");
    }
}

// ============================================================================
// Modern Windows 11 Context Menu (IExplorerCommand)
// Strictly adheres to Microsoft Learn docs: 1 submenu level (no recursive EnumSubCommands).
// ============================================================================

struct LeafSpec {
    std::wstring id;
    std::wstring title;
    std::wstring iconPath;
    std::wstring tooltip;
    bool isCategoryFlyout = false;
    bool separatorBefore = false;
    xmf::MenuItem itemCopy;
};

class LeafCommand final : public IExplorerCommand, public IObjectWithSite {
public:
    LeafCommand(LeafSpec spec, xmf::AppConfig configSnapshot)
        : spec_(std::move(spec)), config_(std::move(configSnapshot)) {
        ++g_objects;
    }
    ~LeafCommand() {
        if (site_) site_->Release();
        --g_objects;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
            *out = static_cast<IExplorerCommand*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IObjectWithSite) {
            *out = static_cast<IObjectWithSite*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override {
        if (!name) return E_POINTER;
        return SHStrDupW(spec_.title.c_str(), name);
    }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override {
        if (!icon) return E_POINTER;
        if (!config_.appearance.showIcons || spec_.iconPath.empty()) {
            *icon = nullptr;
            return E_NOTIMPL;
        }
        return SHStrDupW(spec_.iconPath.c_str(), icon);
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override {
        if (!tip) return E_POINTER;
        if (spec_.tooltip.empty()) {
            *tip = nullptr;
            return E_NOTIMPL;
        }
        return SHStrDupW(spec_.tooltip.c_str(), tip);
    }
    IFACEMETHODIMP GetCanonicalName(GUID* guid) override {
        if (!guid) return E_POINTER;
        *guid = GUID_NULL;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        if (!state) return E_POINTER;
        xmf::SelectionContext ctx = ExtractContext(items, site_);
        if (spec_.isCategoryFlyout) {
            *state = xmf::HasAnyVisibleCommand(config_, spec_.id, ctx) ? ECS_ENABLED : ECS_HIDDEN;
        } else {
            *state = xmf::MatchesContext(spec_.itemCopy, ctx) ? ECS_ENABLED : ECS_HIDDEN;
        }
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        xmf::SelectionContext ctx = ExtractContext(items, site_);
        if (spec_.isCategoryFlyout) {
            LaunchHost(L"--flyout", spec_.id, ctx);
        } else {
            LaunchHost(L"--invoke", spec_.id, ctx);
        }
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override {
        if (!flags) return E_POINTER;
        *flags = spec_.separatorBefore ? ECF_SEPARATORBEFORE : ECF_DEFAULT;
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override {
        if (!commands) return E_POINTER;
        *commands = nullptr;
        return E_NOTIMPL;
    }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* pUnkSite) override {
        if (site_) site_->Release();
        site_ = pUnkSite;
        if (site_) site_->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP GetSite(REFIID riid, void** ppvSite) override {
        if (!ppvSite) return E_POINTER;
        *ppvSite = nullptr;
        return site_ ? site_->QueryInterface(riid, ppvSite) : E_FAIL;
    }

private:
    std::atomic_ulong refs_{1};
    LeafSpec spec_;
    xmf::AppConfig config_;
    IUnknown* site_ = nullptr;
};

class CommandEnumerator final : public IEnumExplorerCommand {
public:
    explicit CommandEnumerator(std::vector<IExplorerCommand*> items) : items_(std::move(items)) { ++g_objects; }
    ~CommandEnumerator() {
        for (auto* p : items_) {
            if (p) p->Release();
        }
        --g_objects;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumExplorerCommand) {
            *out = static_cast<IEnumExplorerCommand*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    IFACEMETHODIMP Next(ULONG celt, IExplorerCommand** rgelt, ULONG* pceltFetched) override {
        if (!rgelt) return E_POINTER;
        ULONG n = 0;
        while (n < celt && index_ < items_.size()) {
            rgelt[n] = items_[index_++];
            rgelt[n]->AddRef();
            ++n;
        }
        if (pceltFetched) *pceltFetched = n;
        return n == celt ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG celt) override {
        size_t rem = items_.size() - index_;
        size_t n = celt < rem ? celt : rem;
        index_ += n;
        return n == celt ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }
    IFACEMETHODIMP Clone(IEnumExplorerCommand** ppenum) override {
        if (!ppenum) return E_POINTER;
        std::vector<IExplorerCommand*> copy = items_;
        for (auto* p : copy) p->AddRef();
        auto* e = new (std::nothrow) CommandEnumerator(std::move(copy));
        if (!e) {
            for (auto* p : copy) p->Release();
            return E_OUTOFMEMORY;
        }
        e->index_ = index_;
        *ppenum = e;
        return S_OK;
    }

private:
    std::atomic_ulong refs_{1};
    std::vector<IExplorerCommand*> items_;
    size_t index_ = 0;
};

class RootCommand final : public IExplorerCommand, public IObjectWithSite {
public:
    RootCommand() { ++g_objects; }
    ~RootCommand() {
        if (site_) site_->Release();
        --g_objects;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
            *out = static_cast<IExplorerCommand*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IObjectWithSite) {
            *out = static_cast<IObjectWithSite*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override {
        if (!name) return E_POINTER;
        EnsureConfigLoaded();
        std::wstring title = config_.appearance.rootMenuTitle.empty() ? L"xMenuForge" : config_.appearance.rootMenuTitle;
        return SHStrDupW(title.c_str(), name);
    }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override {
        if (!icon) return E_POINTER;
        EnsureConfigLoaded();
        if (!config_.appearance.showIcons) {
            *icon = nullptr;
            return E_NOTIMPL;
        }
        std::wstring resolved = xmf::ResolveIconPath(config_.appearance.rootMenuIcon, config_.appearance, g_hModule);
        if (resolved.empty()) {
            *icon = nullptr;
            return E_NOTIMPL;
        }
        return SHStrDupW(resolved.c_str(), icon);
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override {
        if (!tip) return E_POINTER;
        return SHStrDupW(L"xMenuForge - Customizable Context Menu", tip);
    }
    IFACEMETHODIMP GetCanonicalName(GUID* guid) override {
        if (!guid) return E_POINTER;
        *guid = CLSID_XMenuForgeRoot;
        return S_OK;
    }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        if (!state) return E_POINTER;
        EnsureConfigLoaded();
        if (!config_.integrations.modernContextMenuEnabled) {
            *state = ECS_HIDDEN;
            return S_OK;
        }
        xmf::SelectionContext ctx = ExtractContext(items, site_);
        *state = xmf::HasAnyVisibleCommand(config_, L"", ctx) ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override { return S_OK; }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override {
        if (!flags) return E_POINTER;
        *flags = ECF_HASSUBCOMMANDS;
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override {
        if (!commands) return E_POINTER;
        *commands = nullptr;
        EnsureConfigLoaded();

        // Collect top-level items in configured order
        std::vector<const xmf::MenuItem*> topItems;
        for (const auto& item : config_.items) {
            if (item.enabled && item.parentId.empty()) {
                topItems.push_back(&item);
            }
        }
        std::stable_sort(topItems.begin(), topItems.end(), [](const xmf::MenuItem* a, const xmf::MenuItem* b) {
            return a->order < b->order;
        });

        std::vector<IExplorerCommand*> built;
        bool pendingSeparator = false;
        bool flattenCategories = (config_.appearance.modernSubmenuMode == L"flatten");

        auto pushLeaf = [&](const xmf::MenuItem& item, bool isCatFlyout, const std::wstring& prefix) {
            LeafSpec spec;
            spec.id = item.id;
            spec.title = prefix + item.title + (isCatFlyout ? L"  \u25B8" : L"");
            spec.iconPath = xmf::ResolveIconPath(item.icon, config_.appearance, g_hModule);
            spec.tooltip = item.description;
            spec.isCategoryFlyout = isCatFlyout;
            spec.separatorBefore = pendingSeparator && !built.empty();
            spec.itemCopy = item;
            pendingSeparator = false;

            auto* cmd = new (std::nothrow) LeafCommand(std::move(spec), config_);
            if (cmd) {
                if (site_) cmd->SetSite(site_);
                built.push_back(cmd);
            }
        };

        auto flattenRec = [&](const std::wstring& parentId, const std::wstring& prefix, auto& self) -> void {
            std::vector<const xmf::MenuItem*> kids;
            for (const auto& it : config_.items) {
                if (it.enabled && it.parentId == parentId) kids.push_back(&it);
            }
            std::stable_sort(kids.begin(), kids.end(), [](const xmf::MenuItem* a, const xmf::MenuItem* b) {
                return a->order < b->order;
            });
            for (const auto* k : kids) {
                if (k->kind == xmf::ItemKind::Separator) {
                    pendingSeparator = true;
                } else if (k->kind == xmf::ItemKind::Category) {
                    pendingSeparator = true;
                    self(k->id, prefix + k->title + L" / ", self);
                    pendingSeparator = true;
                } else {
                    pushLeaf(*k, false, prefix);
                }
            }
        };

        for (const auto* item : topItems) {
            if (item->kind == xmf::ItemKind::Separator) {
                pendingSeparator = true;
            } else if (item->kind == xmf::ItemKind::Category) {
                if (flattenCategories) {
                    pendingSeparator = true;
                    flattenRec(item->id, L"", flattenRec);
                    pendingSeparator = true;
                } else {
                    pushLeaf(*item, true, L"");
                }
            } else {
                pushLeaf(*item, false, L"");
            }
        }

        auto* enumerator = new (std::nothrow) CommandEnumerator(std::move(built));
        if (!enumerator) {
            for (auto* p : built) p->Release();
            return E_OUTOFMEMORY;
        }
        *commands = enumerator;
        return S_OK;
    }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* pUnkSite) override {
        if (site_) site_->Release();
        site_ = pUnkSite;
        if (site_) site_->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP GetSite(REFIID riid, void** ppvSite) override {
        if (!ppvSite) return E_POINTER;
        *ppvSite = nullptr;
        return site_ ? site_->QueryInterface(riid, ppvSite) : E_FAIL;
    }

private:
    void EnsureConfigLoaded() {
        if (!configLoaded_) {
            config_ = xmf::LoadConfig();
            configLoaded_ = true;
        }
    }

    std::atomic_ulong refs_{1};
    IUnknown* site_ = nullptr;
    xmf::AppConfig config_;
    bool configLoaded_ = false;
};

// ============================================================================
// Classic Context Menu (IContextMenu + IShellExtInit) — Full recursive HMENU
// ============================================================================

HBITMAP LoadMenuIconBitmap(const std::wstring& iconPath) {
    if (iconPath.empty()) return nullptr;
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    HICON hIcon = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE));
    if (!hIcon) {
        std::wstring clean = iconPath;
        int idx = 0;
        size_t comma = clean.find_last_of(L',');
        if (comma != std::wstring::npos) {
            idx = _wtoi(clean.substr(comma + 1).c_str());
            clean = clean.substr(0, comma);
        }
        HICON hSmall = nullptr;
        if (ExtractIconExW(clean.c_str(), idx, nullptr, &hSmall, 1) > 0 && hSmall) {
            hIcon = hSmall;
        }
    }
    if (!hIcon) return nullptr;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = nullptr;
    if (hdcMem) {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = cx;
        bmi.bmiHeader.biHeight = cy;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (hBmp && bits) {
            HGDIOBJ old = SelectObject(hdcMem, hBmp);
            DrawIconEx(hdcMem, 0, 0, hIcon, cx, cy, 0, nullptr, DI_NORMAL);
            SelectObject(hdcMem, old);
        }
        DeleteDC(hdcMem);
    }
    ReleaseDC(nullptr, hdcScreen);
    DestroyIcon(hIcon);
    return hBmp;
}

class ClassicMenu final : public IContextMenu, public IShellExtInit {
public:
    ClassicMenu() { ++g_objects; }
    ~ClassicMenu() {
        for (HBITMAP b : bitmaps_) {
            if (b) DeleteObject(b);
        }
        --g_objects;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IContextMenu) {
            *out = static_cast<IContextMenu*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IShellExtInit) {
            *out = static_cast<IShellExtInit*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    // IShellExtInit
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdtobj, HKEY) override {
        ctx_ = {};
        if (pdtobj) {
            FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM stm{};
            if (SUCCEEDED(pdtobj->GetData(&fe, &stm))) {
                HDROP hDrop = static_cast<HDROP>(GlobalLock(stm.hGlobal));
                if (hDrop) {
                    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                    for (UINT i = 0; i < count; ++i) {
                        UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                        std::wstring path(len, L'\0');
                        if (DragQueryFileW(hDrop, i, path.data(), len + 1) > 0) {
                            ctx_.paths.push_back(std::move(path));
                        }
                    }
                    GlobalUnlock(stm.hGlobal);
                }
                ReleaseStgMedium(&stm);
            }
        }
        if (ctx_.paths.empty() && pidlFolder) {
            WCHAR path[MAX_PATH] = {};
            if (SHGetPathFromIDListW(pidlFolder, path)) {
                ctx_.paths.push_back(path);
                ctx_.isBackground = true;
            }
        }
        if (!ctx_.isBackground && !ctx_.paths.empty()) {
            DWORD attrs = GetFileAttributesW(ctx_.paths[0].c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) {
                ctx_.isFolder = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
            }
        }
        return S_OK;
    }

    // IContextMenu
    IFACEMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override {
        if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

        config_ = xmf::LoadConfig();
        if (!config_.integrations.classicContextMenuEnabled) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);
        }

        for (HBITMAP b : bitmaps_) {
            if (b) DeleteObject(b);
        }
        bitmaps_.clear();
        commandIds_.clear();

        UINT nextOffset = 0;
        HMENU rootSub = BuildSubmenu(L"", idCmdFirst, idCmdLast, nextOffset);
        if (!rootSub) return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

        std::wstring rootTitle = config_.appearance.rootMenuTitle.empty() ? L"xMenuForge" : config_.appearance.rootMenuTitle;
        MENUITEMINFOW mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
        mii.wID = idCmdFirst + nextOffset++;
        mii.hSubMenu = rootSub;
        mii.dwTypeData = rootTitle.data();

        if (config_.appearance.showIcons) {
            std::wstring iconPath = xmf::ResolveIconPath(config_.appearance.rootMenuIcon, config_.appearance, g_hModule);
            if (HBITMAP bmp = LoadMenuIconBitmap(iconPath)) {
                mii.fMask |= MIIM_BITMAP;
                mii.hbmpItem = bmp;
                bitmaps_.push_back(bmp);
            }
        }
        InsertMenuItemW(hmenu, indexMenu, TRUE, &mii);
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, nextOffset);
    }

    IFACEMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO lpici) override {
        if (HIWORD(lpici->lpVerb) != 0) return E_INVALIDARG;
        UINT offset = LOWORD(lpici->lpVerb);
        if (offset < commandIds_.size() && !commandIds_[offset].empty()) {
            LaunchHost(L"--invoke", commandIds_[offset], ctx_);
            return S_OK;
        }
        return E_INVALIDARG;
    }

    IFACEMETHODIMP GetCommandString(UINT_PTR, UINT, UINT*, LPSTR, UINT) override {
        return E_NOTIMPL;
    }

private:
    HMENU BuildSubmenu(const std::wstring& parentId, UINT idCmdFirst, UINT idCmdLast, UINT& nextOffset) {
        auto children = xmf::GetVisibleChildren(config_, parentId, ctx_);
        if (children.empty()) return nullptr;

        HMENU sub = CreatePopupMenu();
        UINT pos = 0;
        for (const auto* item : children) {
            if (idCmdFirst + nextOffset >= idCmdLast) break;
            if (item->kind == xmf::ItemKind::Separator) {
                MENUITEMINFOW mii{};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_FTYPE;
                mii.fType = MFT_SEPARATOR;
                InsertMenuItemW(sub, pos++, TRUE, &mii);
            } else if (item->kind == xmf::ItemKind::Category) {
                HMENU nested = BuildSubmenu(item->id, idCmdFirst, idCmdLast, nextOffset);
                if (!nested) continue;
                std::wstring title = item->title;
                MENUITEMINFOW mii{};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
                mii.wID = idCmdFirst + nextOffset++;
                mii.hSubMenu = nested;
                mii.dwTypeData = title.data();
                commandIds_.push_back(L""); // placeholder slot
                if (config_.appearance.showIcons) {
                    std::wstring iconPath = xmf::ResolveIconPath(item->icon, config_.appearance, g_hModule);
                    if (HBITMAP bmp = LoadMenuIconBitmap(iconPath)) {
                        mii.fMask |= MIIM_BITMAP;
                        mii.hbmpItem = bmp;
                        bitmaps_.push_back(bmp);
                    }
                }
                InsertMenuItemW(sub, pos++, TRUE, &mii);
            } else {
                std::wstring title = item->title;
                MENUITEMINFOW mii{};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_STRING | MIIM_ID;
                mii.wID = idCmdFirst + nextOffset++;
                mii.dwTypeData = title.data();
                commandIds_.push_back(item->id);
                if (config_.appearance.showIcons) {
                    std::wstring iconPath = xmf::ResolveIconPath(item->icon, config_.appearance, g_hModule);
                    if (HBITMAP bmp = LoadMenuIconBitmap(iconPath)) {
                        mii.fMask |= MIIM_BITMAP;
                        mii.hbmpItem = bmp;
                        bitmaps_.push_back(bmp);
                    }
                }
                InsertMenuItemW(sub, pos++, TRUE, &mii);
            }
        }
        if (pos == 0) {
            DestroyMenu(sub);
            return nullptr;
        }
        return sub;
    }

    std::atomic_ulong refs_{1};
    xmf::SelectionContext ctx_;
    xmf::AppConfig config_;
    std::vector<std::wstring> commandIds_;
    std::vector<HBITMAP> bitmaps_;
};

class ClassFactory final : public IClassFactory {
public:
    explicit ClassFactory(bool classic) : classic_(classic) { ++g_objects; }
    ~ClassFactory() { --g_objects; }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *out = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        if (classic_) {
            auto* obj = new (std::nothrow) ClassicMenu();
            if (!obj) return E_OUTOFMEMORY;
            HRESULT hr = obj->QueryInterface(riid, out);
            obj->Release();
            return hr;
        } else {
            auto* obj = new (std::nothrow) RootCommand();
            if (!obj) return E_OUTOFMEMORY;
            HRESULT hr = obj->QueryInterface(riid, out);
            obj->Release();
            return hr;
        }
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_locks;
        else --g_locks;
        return S_OK;
    }

private:
    std::atomic_ulong refs_{1};
    bool classic_;
};

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_XMenuForgeRoot && rclsid != CLSID_XMenuForgeClassic) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new (std::nothrow) ClassFactory(rclsid == CLSID_XMenuForgeClassic);
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_objects.load() == 0 && g_locks.load() == 0) ? S_OK : S_FALSE;
}
