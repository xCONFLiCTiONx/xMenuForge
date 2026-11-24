#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <map>
#include <string>
#include <vector>

namespace xmf {

enum class ItemKind {
    Command,
    Category,
    Separator
};

enum class RunnerType {
    Executable,
    PowerShell,
    Cmd,
    WindowsTerminal
};

enum class ConsoleVisibility {
    Default,
    Show,
    Hide
};

enum class WorkingDirectoryMode {
    Target,
    Parent,
    Custom
};

struct RunnerConfig {
    RunnerType type = RunnerType::Executable;
    std::wstring executable;
    std::wstring arguments;
    WorkingDirectoryMode workingDirectoryMode = WorkingDirectoryMode::Target;
    std::wstring customWorkingDirectory;
    ConsoleVisibility consoleVisibility = ConsoleVisibility::Default;
    bool runAsAdmin = false;
    int timeoutSeconds = 0;
    bool logOutput = true;
    std::map<std::wstring, std::wstring> environment;
};

struct ContextRules {
    bool showOnFiles = true;
    bool showOnFolders = true;
    bool showOnBackground = false;
    bool allowMultiSelect = true;
    std::wstring modifierKey = L"none"; // none | ctrl | shift | alt | ctrl_shift
    std::vector<std::wstring> fileCategories; // empty = all
    std::vector<std::wstring> extensions;     // empty = all
};

struct MenuItem {
    std::wstring id;
    ItemKind kind = ItemKind::Command;
    std::wstring parentId;
    int order = 0;
    bool enabled = true;
    bool builtIn = false;
    std::wstring title;
    std::wstring description;
    std::wstring icon;
    std::wstring builtInAction;
    RunnerConfig runner;
    ContextRules rules;
};

struct AppearanceConfig {
    std::wstring rootMenuTitle = L"xMenuForge";
    std::wstring rootMenuIcon = L"Icons\\ICON.ico";
    bool showIcons = true;
    std::wstring iconTheme = L"auto";         // auto | dark | light
    std::wstring modernSubmenuMode = L"flyout"; // flyout | flatten
    bool windowMaximized = false;
};

struct ExecutionConfig {
    ConsoleVisibility defaultConsoleVisibility = ConsoleVisibility::Show;
    std::wstring defaultPowerShell = L"auto"; // auto | pwsh | powershell
    std::wstring defaultTerminalProfile;
    int defaultTimeoutSeconds = 120;
    bool enableExecutionLogging = true;
    bool logHiddenCommandOutput = true;
    bool notifyOnHiddenError = true;
};

struct IntegrationsConfig {
    bool modernContextMenuEnabled = true;
    bool classicContextMenuEnabled = false;
};

struct AppConfig {
    int schemaVersion = 1;
    AppearanceConfig appearance;
    ExecutionConfig execution;
    IntegrationsConfig integrations;
    std::vector<MenuItem> items;
};

struct SelectionContext {
    bool isBackground = false;
    bool isFolder = false;
    bool ctrlHeld = false;
    bool shiftHeld = false;
    bool altHeld = false;
    std::vector<std::wstring> paths;
};

struct ExecutionLogEntry {
    std::wstring timestampUtc;
    std::wstring commandId;
    std::wstring title;
    std::wstring executable;
    std::wstring arguments;
    std::wstring workingDirectory;
    std::wstring consoleVisibility;
    std::vector<std::wstring> targets;
    int exitCode = 0;
    int64_t durationMs = 0;
    bool success = true;
    std::wstring outputSnippet;
    std::wstring errorMessage;
};

// UTF-8 <-> UTF-16 helpers
std::string WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& s);

// AppData paths
std::wstring GetAppDataDir();
std::wstring GetConfigPath();
std::wstring GetLogsDir();
std::wstring GetExecutionLogPath();
std::wstring GetDiagnosticLogPath();

// Config lifecycle
AppConfig CreateDefaultConfig();
AppConfig LoadConfig();
bool SaveConfig(const AppConfig& config);

// Context evaluation & hierarchy helpers
bool MatchesContext(const MenuItem& item, const SelectionContext& ctx);
std::vector<const MenuItem*> GetVisibleChildren(const AppConfig& config, const std::wstring& parentId, const SelectionContext& ctx);
bool HasAnyVisibleCommand(const AppConfig& config, const std::wstring& parentId, const SelectionContext& ctx);
const MenuItem* FindItemById(const AppConfig& config, const std::wstring& id);

// Icon path resolution relative to module directory
std::wstring ResolveIconPath(const std::wstring& iconSpec, const AppearanceConfig& appearance, HMODULE hModule);

// Logging
void AppendExecutionLog(const ExecutionLogEntry& entry);
void AppendDiagnosticLog(const std::wstring& level, const std::wstring& message);

} // namespace xmf
