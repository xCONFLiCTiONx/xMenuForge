#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
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

// Primary Slot 0: {7A4E9C10-5B2D-4F81-A310-9C6E2B410001}
// Additional Native Top-Level Slots 1..11: {7A4E9C10-5B2D-4F81-A310-9C6E2B410011} .. 001B
constexpr int kMaxRootSlots = 12;

constexpr CLSID CLSID_XMenuForgeRoot =
    {0x7a4e9c10, 0x5b2d, 0x4f81, {0xa3, 0x10, 0x9c, 0x6e, 0x2b, 0x41, 0x00, 0x01}};

// Classic Context Menu: {7A4E9C10-5B2D-4F81-A310-9C6E2B410002}
constexpr CLSID CLSID_XMenuForgeClassic =
    {0x7a4e9c10, 0x5b2d, 0x4f81, {0xa3, 0x10, 0x9c, 0x6e, 0x2b, 0x41, 0x00, 0x02}};

int MatchSlotClsid(REFCLSID clsid) {
    if (clsid == CLSID_XMenuForgeRoot) return 0;
    for (int slot = 1; slot < kMaxRootSlots; ++slot) {
        CLSID c = CLSID_XMenuForgeRoot;
        c.Data4[7] = static_cast<unsigned char>(0x10 + slot); // 0x11 .. 0x1B
        if (clsid == c) return slot;
    }
    return -1;
}

CLSID MakeSlotClsid(int slot) {
    if (slot == 0) return CLSID_XMenuForgeRoot;
    CLSID c = CLSID_XMenuForgeRoot;
    c.Data4[7] = static_cast<unsigned char>(0x10 + slot);
    return c;
}

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
    if (ctx.isBackground) cmdLine += L" --background";
    if (ctx.isFolder) cmdLine += L" --folder";
    if (ctx.ctrlHeld) cmdLine += L" --ctrl";
    if (ctx.altHeld) cmdLine += L" --alt";
    if (ctx.winHeld) cmdLine += L" --win";

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

struct ModifierSnapshot {
    bool ctrl = false;
    bool alt = false;
    bool win = false;

    static bool IsWinDown() {
        return ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);
    }

    static ModifierSnapshot Capture() {
        ModifierSnapshot m;
        m.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        m.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        m.win = IsWinDown();
        return m;
    }

    void ApplyTo(xmf::SelectionContext& ctx) const {
        ctx.ctrlHeld = ctrl || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
        ctx.altHeld = alt || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
        ctx.winHeld = win || IsWinDown();
    }
};

// ============================================================================
// Modern Windows 11 Context Menu (IExplorerCommand)
// Multi-slot native top-level architecture:
//   Each top-level command or category gets its own native Win11 root slot,
//   so every category renders as a 100% native Windows 11 Fluent cascading submenu!
// ============================================================================

struct LeafSpec {
    std::wstring id;
    std::wstring title;
    std::wstring iconPath;
    std::wstring tooltip;
    bool separatorBefore = false;
    xmf::MenuItem itemCopy;
    ModifierSnapshot modifiers;
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
        spec_.modifiers.ApplyTo(ctx);
        *state = xmf::MatchesContext(spec_.itemCopy, ctx) ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        xmf::SelectionContext ctx = ExtractContext(items, site_);
        spec_.modifiers.ApplyTo(ctx);
        LaunchHost(L"--invoke", spec_.id, ctx);
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

class SlotCommand final : public IExplorerCommand, public IObjectWithSite {
public:
    explicit SlotCommand(int slotIndex)
        : slotIndex_(slotIndex), modifiers_(ModifierSnapshot::Capture()) {
        ++g_objects;
    }
    ~SlotCommand() {
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
        EnsureLoaded();
        if (IsSingleRootMode()) {
            std::wstring title = config_.appearance.rootMenuTitle.empty() ? L"xMenuForge" : config_.appearance.rootMenuTitle;
            return SHStrDupW(title.c_str(), name);
        }
        if (isOverflowSlot_) {
            std::wstring title = config_.appearance.rootMenuTitle.empty() ? L"xMenuForge" : (config_.appearance.rootMenuTitle + L" More");
            return SHStrDupW(title.c_str(), name);
        }
        if (boundItem_) {
            return SHStrDupW(boundItem_->title.c_str(), name);
        }
        return SHStrDupW(L"xMenuForge", name);
    }

    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override {
        if (!icon) return E_POINTER;
        EnsureLoaded();
        if (!config_.appearance.showIcons) {
            *icon = nullptr;
            return E_NOTIMPL;
        }
        std::wstring spec = config_.appearance.rootMenuIcon;
        if (!IsSingleRootMode() && !isOverflowSlot_ && boundItem_ && !boundItem_->icon.empty()) {
            spec = boundItem_->icon;
        }
        std::wstring resolved = xmf::ResolveIconPath(spec, config_.appearance, g_hModule);
        if (resolved.empty()) {
            *icon = nullptr;
            return E_NOTIMPL;
        }
        return SHStrDupW(resolved.c_str(), icon);
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override {
        if (!tip) return E_POINTER;
        EnsureLoaded();
        if (!IsSingleRootMode() && !isOverflowSlot_ && boundItem_ && !boundItem_->description.empty()) {
            return SHStrDupW(boundItem_->description.c_str(), tip);
        }
        return SHStrDupW(L"xMenuForge", tip);
    }

    IFACEMETHODIMP GetCanonicalName(GUID* guid) override {
        if (!guid) return E_POINTER;
        *guid = MakeSlotClsid(slotIndex_);
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        if (!state) return E_POINTER;
        EnsureLoaded();
        if (!config_.integrations.modernContextMenuEnabled) {
            *state = ECS_HIDDEN;
            return S_OK;
        }

        xmf::SelectionContext ctx = ExtractContext(items, site_);
        modifiers_.ApplyTo(ctx);

        if (IsSingleRootMode()) {
            if (slotIndex_ != 0) {
                *state = ECS_HIDDEN;
                return S_OK;
            }
            *state = xmf::HasAnyVisibleCommand(config_, L"", ctx) ? ECS_ENABLED : ECS_HIDDEN;
            return S_OK;
        }

        // Direct Root mode
        if (isOverflowSlot_) {
            bool any = false;
            for (size_t i = kMaxRootSlots - 1; i < topItems_.size(); ++i) {
                const auto* it = topItems_[i];
                if (it->kind == xmf::ItemKind::Category) {
                    if (xmf::MatchesContext(*it, ctx) && xmf::HasAnyVisibleCommand(config_, it->id, ctx)) {
                        any = true;
                        break;
                    }
                } else if (xmf::MatchesContext(*it, ctx)) {
                    any = true;
                    break;
                }
            }
            *state = any ? ECS_ENABLED : ECS_HIDDEN;
            return S_OK;
        }

        if (!boundItem_) {
            *state = ECS_HIDDEN;
            return S_OK;
        }

        if (boundItem_->kind == xmf::ItemKind::Category) {
            bool vis = xmf::MatchesContext(*boundItem_, ctx) && xmf::HasAnyVisibleCommand(config_, boundItem_->id, ctx);
            *state = vis ? ECS_ENABLED : ECS_HIDDEN;
        } else {
            *state = xmf::MatchesContext(*boundItem_, ctx) ? ECS_ENABLED : ECS_HIDDEN;
        }
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        EnsureLoaded();
        if (!IsSingleRootMode() && !isOverflowSlot_ && boundItem_ && boundItem_->kind == xmf::ItemKind::Command) {
            xmf::SelectionContext ctx = ExtractContext(items, site_);
            modifiers_.ApplyTo(ctx);
            LaunchHost(L"--invoke", boundItem_->id, ctx);
        }
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override {
        if (!flags) return E_POINTER;
        EnsureLoaded();
        if (IsSingleRootMode()) {
            *flags = (slotIndex_ == 0) ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
            return S_OK;
        }
        if (isOverflowSlot_) {
            *flags = ECF_HASSUBCOMMANDS;
            return S_OK;
        }
        if (boundItem_ && boundItem_->kind == xmf::ItemKind::Category) {
            *flags = ECF_HASSUBCOMMANDS;
            return S_OK;
        }
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override {
        if (!commands) return E_POINTER;
        *commands = nullptr;
        EnsureLoaded();

        std::vector<IExplorerCommand*> built;
        bool pendingSeparator = false;

        auto pushLeaf = [&](const xmf::MenuItem& item, const std::wstring& prefix) {
            LeafSpec spec;
            spec.id = item.id;
            spec.title = prefix + item.title;
            spec.iconPath = xmf::ResolveIconPath(item.icon, config_.appearance, g_hModule);
            spec.tooltip = item.description;
            spec.separatorBefore = pendingSeparator && !built.empty();
            spec.itemCopy = item;
            spec.modifiers = modifiers_;
            pendingSeparator = false;

            auto* cmd = new (std::nothrow) LeafCommand(std::move(spec), config_);
            if (cmd) {
                if (site_) cmd->SetSite(site_);
                built.push_back(cmd);
            }
        };

        auto flattenChildren = [&](const std::wstring& parentId, const std::wstring& prefix, auto& self) -> void {
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
                    pushLeaf(*k, prefix);
                }
            }
        };

        if (IsSingleRootMode()) {
            if (slotIndex_ != 0) return E_NOTIMPL;
            flattenChildren(L"", L"", flattenChildren);
        } else if (isOverflowSlot_) {
            for (size_t i = kMaxRootSlots - 1; i < topItems_.size(); ++i) {
                const auto* it = topItems_[i];
                if (it->kind == xmf::ItemKind::Category) {
                    pendingSeparator = true;
                    flattenChildren(it->id, it->title + L" / ", flattenChildren);
                    pendingSeparator = true;
                } else {
                    pushLeaf(*it, L"");
                }
            }
        } else if (boundItem_ && boundItem_->kind == xmf::ItemKind::Category) {
            flattenChildren(boundItem_->id, L"", flattenChildren);
        } else {
            return E_NOTIMPL;
        }

        auto* enumerator = new (std::nothrow) CommandEnumerator(std::move(built));
        if (!enumerator) {
            for (auto* p : built) p->Release();
            return E_OUTOFMEMORY;
        }
        *commands = enumerator;
        return S_OK;
    }

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
    bool IsSingleRootMode() const {
        return config_.appearance.modernSubmenuMode == L"single" ||
               config_.appearance.modernSubmenuMode == L"flatten";
    }

    void EnsureLoaded() {
        if (loaded_) return;
        config_ = xmf::LoadConfig();
        loaded_ = true;

        topItems_.clear();
        for (const auto& it : config_.items) {
            if (it.enabled && it.parentId.empty() && it.kind != xmf::ItemKind::Separator) {
                topItems_.push_back(&it);
            }
        }
        std::stable_sort(topItems_.begin(), topItems_.end(), [](const xmf::MenuItem* a, const xmf::MenuItem* b) {
            return a->order < b->order;
        });

        if (topItems_.size() > static_cast<size_t>(kMaxRootSlots) && slotIndex_ == kMaxRootSlots - 1) {
            isOverflowSlot_ = true;
            boundItem_ = nullptr;
        } else if (static_cast<size_t>(slotIndex_) < topItems_.size()) {
            boundItem_ = topItems_[slotIndex_];
        } else {
            boundItem_ = nullptr;
        }
    }

    std::atomic_ulong refs_{1};
    int slotIndex_ = 0;
    IUnknown* site_ = nullptr;
    xmf::AppConfig config_;
    bool loaded_ = false;
    std::vector<const xmf::MenuItem*> topItems_;
    const xmf::MenuItem* boundItem_ = nullptr;
    bool isOverflowSlot_ = false;
    ModifierSnapshot modifiers_;
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

    IFACEMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override {
        if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

        config_ = xmf::LoadConfig();
        if (!config_.integrations.classicContextMenuEnabled) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);
        }

        ModifierSnapshot::Capture().ApplyTo(ctx_);

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
                commandIds_.push_back(L"");
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
    explicit ClassFactory(int slotIndex, bool classic) : slotIndex_(slotIndex), classic_(classic) { ++g_objects; }
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
        }
        auto* obj = new (std::nothrow) SlotCommand(slotIndex_);
        if (!obj) return E_OUTOFMEMORY;
        HRESULT hr = obj->QueryInterface(riid, out);
        obj->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_locks;
        else --g_locks;
        return S_OK;
    }

private:
    std::atomic_ulong refs_{1};
    int slotIndex_;
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
    if (rclsid == CLSID_XMenuForgeClassic) {
        auto* factory = new (std::nothrow) ClassFactory(-1, true);
        if (!factory) return E_OUTOFMEMORY;
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    int slot = MatchSlotClsid(rclsid);
    if (slot >= 0) {
        auto* factory = new (std::nothrow) ClassFactory(slot, false);
        if (!factory) return E_OUTOFMEMORY;
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return (g_objects.load() == 0 && g_locks.load() == 0) ? S_OK : S_FALSE;
}
