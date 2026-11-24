#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <atomic>
#include <string>
#include <vector>
#include <new>

namespace {
constexpr CLSID CLSID_XMenuForgeRoot =
{0x6e7f8a10,0x4d2b,0x4a61,{0x91,0x10,0x2a,0x4c,0x77,0x30,0x11,0x01}};
constexpr CLSID CLSID_XMenuForgeSecondRoot =
{0x6e7f8a11,0x4d2b,0x4a61,{0x91,0x10,0x2a,0x4c,0x77,0x30,0x11,0x01}};
std::atomic_ulong g_objects{0};
std::atomic_ulong g_locks{0};

std::wstring SelectedItems(IShellItemArray* items) {
    if (!items) return L"(no selection supplied)";
    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) return L"(selection unavailable)";
    std::wstring result;
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                if (!result.empty()) result += L"\r\n";
                result += path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    return result.empty() ? L"(shell item has no filesystem path)" : result;
}

class Command final : public IExplorerCommand {
public:
    Command(std::wstring title, std::vector<std::pair<std::wstring,std::vector<std::pair<std::wstring,std::vector<std::pair<std::wstring,std::vector<int>>>>>>>> children = {})
        : title_(std::move(title)), children_(std::move(children)) { ++g_objects; }
    ~Command() { --g_objects; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
            *out = static_cast<IExplorerCommand*>(this);
            AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_; if (!n) delete this; return n;
    }
    HRESULT STDMETHODCALLTYPE GetTitle(IShellItemArray*, LPWSTR* title) override {
        if (!title) return E_POINTER;
        return SHStrDupW(title_.c_str(), title);
    }
    HRESULT STDMETHODCALLTYPE GetIcon(IShellItemArray*, LPWSTR* icon) override {
        if (!icon) return E_POINTER; *icon = nullptr; return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetToolTip(IShellItemArray*, LPWSTR* tip) override {
        if (!tip) return E_POINTER; *tip = nullptr; return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalName(GUID* name) override {
        if (!name) return E_POINTER; *name = GUID_NULL; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetState(IShellItemArray*, BOOL, EXPCMDSTATE* state) override {
        if (!state) return E_POINTER; *state = ECS_ENABLED; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IShellItemArray* items, IBindCtx*) override {
        const auto paths = SelectedItems(items);
        const auto message = L"xMenuForge modern context-menu POC\r\n\r\nCommand: " + title_ +
            L"\r\n\r\nSelected item(s):\r\n" + paths;
        MessageBoxW(nullptr, message.c_str(), L"xMenuForge POC", MB_OK | MB_ICONINFORMATION);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFlags(EXPCMDFLAGS* flags) override {
        if (!flags) return E_POINTER;
        *flags = children_.empty() ? ECF_DEFAULT : ECF_HASSUBCOMMANDS;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumSubCommands(IEnumExplorerCommand** commands) override;

private:
    std::atomic_ulong refs_{1};
    std::wstring title_;
    std::vector<std::pair<std::wstring,std::vector<std::pair<std::wstring,std::vector<std::pair<std::wstring,std::vector<int>>>>>>>> children_;
};

class Enumerator final : public IEnumExplorerCommand {
public:
    explicit Enumerator(std::vector<IExplorerCommand*> items) : items_(std::move(items)) { ++g_objects; }
    ~Enumerator() { for (auto* p : items_) if (p) p->Release(); --g_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumExplorerCommand) {
            *out = static_cast<IEnumExplorerCommand*>(this); AddRef(); return S_OK;
        } return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=--refs_; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, IExplorerCommand** out, ULONG* fetched) override {
        if (!out) return E_POINTER;
        ULONG n=0;
        while (n<count && index_<items_.size()) { out[n]=items_[index_++]; out[n]->AddRef(); ++n; }
        if (fetched) *fetched=n;
        return n==count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        const auto remaining=items_.size()-index_; const auto n=(count<remaining?count:remaining);
        index_+=n; return n==count?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { index_=0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumExplorerCommand** out) override {
        if (!out) return E_POINTER;
        auto* copy=new(std::nothrow) Enumerator(items_);
        if(!copy) return E_OUTOFMEMORY;
        copy->index_=index_; *out=copy; return S_OK;
    }
private:
    std::atomic_ulong refs_{1};
    std::vector<IExplorerCommand*> items_;
    size_t index_{0};
};

HRESULT Command::EnumSubCommands(IEnumExplorerCommand** commands) {
    if (!commands) return E_POINTER;
    *commands=nullptr;
    if (children_.empty()) return E_NOTIMPL;
    std::vector<IExplorerCommand*> items;
    // The first root exposes leaf commands and a deliberate grandchild probe.
    for (const auto& child : children_) {
        std::vector<std::pair<std::wstring,std::vector<std::pair<std::wstring,std::vector<int>>>>> unused;
        auto* node = new(std::nothrow) Command(child.first);
        if (!node) { for(auto* p:items) p->Release(); return E_OUTOFMEMORY; }
        items.push_back(node);
    }
    auto* enumerator=new(std::nothrow) Enumerator(std::move(items));
    if(!enumerator) { for(auto* p:items) p->Release(); return E_OUTOFMEMORY; }
    *commands=enumerator; return S_OK;
}

class Factory final : public IClassFactory {
public:
    explicit Factory(bool second) : second_(second) { ++g_objects; }
    ~Factory() { --g_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(riid==IID_IUnknown || riid==IID_IClassFactory) { *out=static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=--refs_; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if(outer) return CLASS_E_NOAGGREGATION;
        std::vector<std::pair<std::wstring,std::vector<std::pair<std::wstring,std::vector<std::pair<std::wstring,std::vector<int>>>>>>>> tree;
        if (!second_) {
            tree.push_back({L"First-level child A",{}});
            tree.push_back({L"First-level child B",{}});
            // The child is intentionally given its own subcommand in the model.
            // Explorer's documented IExplorerCommand contract does not support this depth.
            tree.push_back({L"Grandchild depth probe",{}});
        }
        auto* command=new(std::nothrow) Command(second_?L"Independent root command B":L"xMenuForge POC",std::move(tree));
        if(!command) return E_OUTOFMEMORY;
        const HRESULT hr=command->QueryInterface(riid,out); command->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if(lock) ++g_locks; else --g_locks; return S_OK; }
private:
    std::atomic_ulong refs_{1}; bool second_;
};
}

extern "C" HRESULT __declspec(dllexport) __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** out) {
    if(clsid!=CLSID_XMenuForgeRoot && clsid!=CLSID_XMenuForgeSecondRoot) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory=new(std::nothrow) Factory(clsid==CLSID_XMenuForgeSecondRoot);
    if(!factory) return E_OUTOFMEMORY;
    const HRESULT hr=factory->QueryInterface(riid,out); factory->Release(); return hr;
}
extern "C" HRESULT __declspec(dllexport) __stdcall DllCanUnloadNow() {
    return g_objects.load()==0 && g_locks.load()==0 ? S_OK : S_FALSE;
}
