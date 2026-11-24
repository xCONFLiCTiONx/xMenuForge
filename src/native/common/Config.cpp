#include "Config.h"
#include "FileTypes.h"
#include "Json.h"
#include "Theme.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;

namespace xmf {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::wstring GetAppDataDir() {
    PWSTR path = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) && path) {
        dir = std::wstring(path) + L"\\xMenuForge";
        CoTaskMemFree(path);
    } else {
        WCHAR envBuf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", envBuf, MAX_PATH) > 0) {
            dir = std::wstring(envBuf) + L"\\xMenuForge";
        } else {
            dir = L"C:\\ProgramData\\xMenuForge";
        }
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::wstring GetConfigPath() {
    return GetAppDataDir() + L"\\config.json";
}

std::wstring GetLogsDir() {
    std::wstring dir = GetAppDataDir() + L"\\logs";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::wstring GetExecutionLogPath() {
    return GetLogsDir() + L"\\executions.jsonl";
}

std::wstring GetDiagnosticLogPath() {
    return GetLogsDir() + L"\\xmenuforge.log";
}

static std::string KindToString(ItemKind k) {
    switch (k) {
    case ItemKind::Category: return "category";
    case ItemKind::Separator: return "separator";
    default: return "command";
    }
}

static ItemKind StringToKind(const std::string& s) {
    if (s == "category") return ItemKind::Category;
    if (s == "separator") return ItemKind::Separator;
    return ItemKind::Command;
}

static std::string RunnerTypeToString(RunnerType t) {
    switch (t) {
    case RunnerType::PowerShell: return "powershell";
    case RunnerType::Cmd: return "cmd";
    case RunnerType::WindowsTerminal: return "windows_terminal";
    default: return "executable";
    }
}

static RunnerType StringToRunnerType(const std::string& s) {
    if (s == "powershell") return RunnerType::PowerShell;
    if (s == "cmd") return RunnerType::Cmd;
    if (s == "windows_terminal") return RunnerType::WindowsTerminal;
    return RunnerType::Executable;
}

static std::string VisibilityToString(ConsoleVisibility v) {
    switch (v) {
    case ConsoleVisibility::Show: return "show";
    case ConsoleVisibility::Hide: return "hide";
    default: return "default";
    }
}

static ConsoleVisibility StringToVisibility(const std::string& s) {
    if (s == "show") return ConsoleVisibility::Show;
    if (s == "hide") return ConsoleVisibility::Hide;
    return ConsoleVisibility::Default;
}

static std::string WorkDirToString(WorkingDirectoryMode m) {
    switch (m) {
    case WorkingDirectoryMode::Parent: return "parent";
    case WorkingDirectoryMode::Custom: return "custom";
    default: return "target";
    }
}

static WorkingDirectoryMode StringToWorkDir(const std::string& s) {
    if (s == "parent") return WorkingDirectoryMode::Parent;
    if (s == "custom") return WorkingDirectoryMode::Custom;
    return WorkingDirectoryMode::Target;
}

AppConfig CreateDefaultConfig() {
    AppConfig cfg;
    cfg.schemaVersion = 1;

    int order = 0;
    auto addBuiltIn = [&](const wchar_t* id, const wchar_t* title, const wchar_t* desc,
                          const wchar_t* icon, const wchar_t* action,
                          bool files, bool folders, bool bg, const wchar_t* parentId = L"") {
        MenuItem item;
        item.id = id;
        item.kind = ItemKind::Command;
        item.parentId = parentId;
        item.order = order++;
        item.enabled = true;
        item.builtIn = true;
        item.title = title;
        item.description = desc;
        item.icon = icon;
        item.builtInAction = action;
        item.rules.showOnFiles = files;
        item.rules.showOnFolders = folders;
        item.rules.showOnBackground = bg;
        cfg.items.push_back(std::move(item));
    };

    auto addCategory = [&](const wchar_t* id, const wchar_t* title, const wchar_t* icon) {
        MenuItem cat;
        cat.id = id;
        cat.kind = ItemKind::Category;
        cat.order = order++;
        cat.enabled = true;
        cat.builtIn = true;
        cat.title = title;
        cat.icon = icon;
        cfg.items.push_back(std::move(cat));
    };

    auto addSeparator = [&](const wchar_t* id, const wchar_t* parentId = L"") {
        MenuItem sep;
        sep.id = id;
        sep.kind = ItemKind::Separator;
        sep.parentId = parentId;
        sep.order = order++;
        sep.enabled = true;
        sep.builtIn = true;
        sep.title = L"---";
        cfg.items.push_back(std::move(sep));
    };

    // Core file & folder utilities
    addBuiltIn(L"builtin.attributes", L"Attributes", L"View and modify file attributes and timestamps",
               L"Icons\\Attributes.ico", L"attributes", true, true, true);
    addBuiltIn(L"builtin.edit_with", L"Edit With...", L"Open selected files in an installed editor",
               L"Icons\\Edit with.ico", L"edit_with", true, false, false);
    addBuiltIn(L"builtin.copy_name", L"Copy Name", L"Copy selected file or folder names to clipboard",
               L"Icons\\Copy Name.ico", L"copy_name", true, true, false);
    addBuiltIn(L"builtin.copy_path", L"Copy Path", L"Copy full paths of selected items to clipboard",
               L"Icons\\Copy Path.ico", L"copy_path", true, true, false);
    addBuiltIn(L"builtin.paste_to_file", L"Paste to File", L"Save clipboard text directly into a file here",
               L"Icons\\Paste to File.ico", L"paste_to_file", false, false, true);
    addBuiltIn(L"builtin.take_ownership", L"Take Ownership", L"Take ownership and grant Full Control recursively",
               L"Icons\\Take Ownership.ico", L"take_ownership", true, true, false);

    addSeparator(L"builtin.sep_terminals");

    // Terminals category
    addCategory(L"cat.terminals", L"Terminals", L"Icons\\Terminals.ico");
    addBuiltIn(L"builtin.terminal", L"Windows Terminal", L"Open Windows Terminal in target folder",
               L"Icons\\Terminals.ico", L"terminal", false, true, true, L"cat.terminals");
    addBuiltIn(L"builtin.terminal_admin", L"Windows Terminal (Admin)", L"Open elevated Windows Terminal in target folder",
               L"Icons\\Terminals.ico", L"terminal_admin", false, true, true, L"cat.terminals");
    addBuiltIn(L"builtin.powershell", L"PowerShell", L"Open PowerShell in target folder",
               L"Icons\\Terminals.ico", L"powershell", false, true, true, L"cat.terminals");
    addBuiltIn(L"builtin.powershell_admin", L"PowerShell (Admin)", L"Open elevated PowerShell in target folder",
               L"Icons\\Terminals.ico", L"powershell_admin", false, true, true, L"cat.terminals");
    addBuiltIn(L"builtin.cmd", L"Command Prompt", L"Open Command Prompt in target folder",
               L"Icons\\Terminals.ico", L"cmd", false, true, true, L"cat.terminals");
    addBuiltIn(L"builtin.cmd_admin", L"Command Prompt (Admin)", L"Open elevated Command Prompt in target folder",
               L"Icons\\Terminals.ico", L"cmd_admin", false, true, true, L"cat.terminals");

    // System Folders
    addBuiltIn(L"builtin.system_folders", L"System Folders...", L"Jump to common Windows system directories",
               L"Icons\\System Folders.ico", L"system_folders", false, false, true);

    addSeparator(L"builtin.sep_settings");

    // Settings
    addBuiltIn(L"builtin.settings", L"xMenuForge Settings", L"Configure menus, commands, and execution rules",
               L"Icons\\Settings.ico", L"settings", true, true, true);

    return cfg;
}

static json::Value SerializeConfig(const AppConfig& cfg) {
    json::Value root;
    root["schemaVersion"] = cfg.schemaVersion;

    json::Value app;
    app["rootMenuTitle"] = WideToUtf8(cfg.appearance.rootMenuTitle);
    app["rootMenuIcon"] = WideToUtf8(cfg.appearance.rootMenuIcon);
    app["showIcons"] = cfg.appearance.showIcons;
    app["iconTheme"] = WideToUtf8(cfg.appearance.iconTheme);
    app["modernSubmenuMode"] = WideToUtf8(cfg.appearance.modernSubmenuMode);
    app["windowMaximized"] = cfg.appearance.windowMaximized;
    root["appearance"] = std::move(app);

    json::Value exec;
    exec["defaultConsoleVisibility"] = VisibilityToString(cfg.execution.defaultConsoleVisibility);
    exec["defaultPowerShell"] = WideToUtf8(cfg.execution.defaultPowerShell);
    exec["defaultTerminalProfile"] = WideToUtf8(cfg.execution.defaultTerminalProfile);
    exec["defaultTimeoutSeconds"] = cfg.execution.defaultTimeoutSeconds;
    exec["enableExecutionLogging"] = cfg.execution.enableExecutionLogging;
    exec["logHiddenCommandOutput"] = cfg.execution.logHiddenCommandOutput;
    exec["notifyOnHiddenError"] = cfg.execution.notifyOnHiddenError;
    root["execution"] = std::move(exec);

    json::Value integ;
    integ["modernContextMenuEnabled"] = cfg.integrations.modernContextMenuEnabled;
    integ["classicContextMenuEnabled"] = cfg.integrations.classicContextMenuEnabled;
    root["integrations"] = std::move(integ);

    json::Value::Array arr;
    for (const auto& item : cfg.items) {
        json::Value j;
        j["id"] = WideToUtf8(item.id);
        j["kind"] = KindToString(item.kind);
        j["parentId"] = WideToUtf8(item.parentId);
        j["order"] = item.order;
        j["enabled"] = item.enabled;
        j["builtIn"] = item.builtIn;
        j["title"] = WideToUtf8(item.title);
        j["description"] = WideToUtf8(item.description);
        j["icon"] = WideToUtf8(item.icon);
        j["builtInAction"] = WideToUtf8(item.builtInAction);

        json::Value r;
        r["type"] = RunnerTypeToString(item.runner.type);
        r["executable"] = WideToUtf8(item.runner.executable);
        r["arguments"] = WideToUtf8(item.runner.arguments);
        r["workingDirectoryMode"] = WorkDirToString(item.runner.workingDirectoryMode);
        r["customWorkingDirectory"] = WideToUtf8(item.runner.customWorkingDirectory);
        r["consoleVisibility"] = VisibilityToString(item.runner.consoleVisibility);
        r["runAsAdmin"] = item.runner.runAsAdmin;
        r["timeoutSeconds"] = item.runner.timeoutSeconds;
        r["logOutput"] = item.runner.logOutput;
        json::Value::Object envObj;
        for (const auto& [k, v] : item.runner.environment) {
            envObj[WideToUtf8(k)] = WideToUtf8(v);
        }
        r["environment"] = json::Value(std::move(envObj));
        j["runner"] = std::move(r);

        json::Value rules;
        rules["showOnFiles"] = item.rules.showOnFiles;
        rules["showOnFolders"] = item.rules.showOnFolders;
        rules["showOnBackground"] = item.rules.showOnBackground;
        rules["allowMultiSelect"] = item.rules.allowMultiSelect;
        rules["modifierKey"] = WideToUtf8(item.rules.modifierKey.empty() ? L"none" : item.rules.modifierKey);
        json::Value::Array cats;
        for (const auto& c : item.rules.fileCategories) cats.push_back(WideToUtf8(c));
        rules["fileCategories"] = json::Value(std::move(cats));
        json::Value::Array exts;
        for (const auto& e : item.rules.extensions) exts.push_back(WideToUtf8(e));
        rules["extensions"] = json::Value(std::move(exts));
        j["rules"] = std::move(rules);

        arr.push_back(std::move(j));
    }
    root["items"] = json::Value(std::move(arr));
    return root;
}

static AppConfig DeserializeConfig(const json::Value& root) {
    AppConfig cfg;
    cfg.schemaVersion = root.Get("schemaVersion").AsInt(1);

    if (root.Contains("appearance")) {
        const auto& a = root.Get("appearance");
        cfg.appearance.rootMenuTitle = Utf8ToWide(a.Get("rootMenuTitle").AsStringOr("xMenuForge"));
        cfg.appearance.rootMenuIcon = Utf8ToWide(a.Get("rootMenuIcon").AsStringOr("Icons\\ICON.ico"));
        cfg.appearance.showIcons = a.Get("showIcons").AsBool(true);
        cfg.appearance.iconTheme = Utf8ToWide(a.Get("iconTheme").AsStringOr("auto"));
        cfg.appearance.modernSubmenuMode = Utf8ToWide(a.Get("modernSubmenuMode").AsStringOr("flyout"));
        cfg.appearance.windowMaximized = a.Get("windowMaximized").AsBool(false);
    }

    if (root.Contains("execution")) {
        const auto& e = root.Get("execution");
        cfg.execution.defaultConsoleVisibility = StringToVisibility(e.Get("defaultConsoleVisibility").AsStringOr("show"));
        if (cfg.execution.defaultConsoleVisibility == ConsoleVisibility::Default) {
            cfg.execution.defaultConsoleVisibility = ConsoleVisibility::Show;
        }
        cfg.execution.defaultPowerShell = Utf8ToWide(e.Get("defaultPowerShell").AsStringOr("auto"));
        cfg.execution.defaultTerminalProfile = Utf8ToWide(e.Get("defaultTerminalProfile").AsStringOr(""));
        cfg.execution.defaultTimeoutSeconds = e.Get("defaultTimeoutSeconds").AsInt(120);
        cfg.execution.enableExecutionLogging = e.Get("enableExecutionLogging").AsBool(true);
        cfg.execution.logHiddenCommandOutput = e.Get("logHiddenCommandOutput").AsBool(true);
        cfg.execution.notifyOnHiddenError = e.Get("notifyOnHiddenError").AsBool(true);
    }

    if (root.Contains("integrations")) {
        const auto& i = root.Get("integrations");
        cfg.integrations.modernContextMenuEnabled = i.Get("modernContextMenuEnabled").AsBool(true);
        cfg.integrations.classicContextMenuEnabled = i.Get("classicContextMenuEnabled").AsBool(false);
    }

    if (root.Contains("items") && root.Get("items").IsArray()) {
        for (const auto& j : root.Get("items").AsArray()) {
            MenuItem item;
            item.id = Utf8ToWide(j.Get("id").AsString());
            item.kind = StringToKind(j.Get("kind").AsStringOr("command"));
            item.parentId = Utf8ToWide(j.Get("parentId").AsString());
            item.order = j.Get("order").AsInt(0);
            item.enabled = j.Get("enabled").AsBool(true);
            item.builtIn = j.Get("builtIn").AsBool(false);
            item.title = Utf8ToWide(j.Get("title").AsString());
            item.description = Utf8ToWide(j.Get("description").AsString());
            item.icon = Utf8ToWide(j.Get("icon").AsString());
            item.builtInAction = Utf8ToWide(j.Get("builtInAction").AsString());

            if (j.Contains("runner")) {
                const auto& r = j.Get("runner");
                item.runner.type = StringToRunnerType(r.Get("type").AsStringOr("executable"));
                item.runner.executable = Utf8ToWide(r.Get("executable").AsString());
                item.runner.arguments = Utf8ToWide(r.Get("arguments").AsString());
                item.runner.workingDirectoryMode = StringToWorkDir(r.Get("workingDirectoryMode").AsStringOr("target"));
                item.runner.customWorkingDirectory = Utf8ToWide(r.Get("customWorkingDirectory").AsString());
                item.runner.consoleVisibility = StringToVisibility(r.Get("consoleVisibility").AsStringOr("default"));
                item.runner.runAsAdmin = r.Get("runAsAdmin").AsBool(false);
                item.runner.timeoutSeconds = r.Get("timeoutSeconds").AsInt(0);
                item.runner.logOutput = r.Get("logOutput").AsBool(true);
                if (r.Contains("environment") && r.Get("environment").IsObject()) {
                    for (const auto& [k, v] : r.Get("environment").AsObject()) {
                        item.runner.environment[Utf8ToWide(k)] = Utf8ToWide(v.AsString());
                    }
                }
            }

            if (j.Contains("rules")) {
                const auto& ru = j.Get("rules");
                item.rules.showOnFiles = ru.Get("showOnFiles").AsBool(true);
                item.rules.showOnFolders = ru.Get("showOnFolders").AsBool(true);
                item.rules.showOnBackground = ru.Get("showOnBackground").AsBool(false);
                item.rules.allowMultiSelect = ru.Get("allowMultiSelect").AsBool(true);
                item.rules.modifierKey = Utf8ToWide(ru.Get("modifierKey").AsStringOr("none"));
                for (const auto& c : ru.Get("fileCategories").AsArray()) {
                    item.rules.fileCategories.push_back(Utf8ToWide(c.AsString()));
                }
                for (const auto& e : ru.Get("extensions").AsArray()) {
                    item.rules.extensions.push_back(NormalizeExtension(Utf8ToWide(e.AsString())));
                }
            }

            if (!item.id.empty()) {
                cfg.items.push_back(std::move(item));
            }
        }
    }

    return cfg;
}

AppConfig LoadConfig() {
    std::wstring path = GetConfigPath();
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        AppConfig def = CreateDefaultConfig();
        SaveConfig(def);
        return def;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    ifs.close();

    json::Value root;
    if (!json::Parse(oss.str(), root) || !root.IsObject()) {
        AppendDiagnosticLog(L"WARN", L"Failed to parse config.json; falling back to defaults.");
        return CreateDefaultConfig();
    }
    AppConfig cfg = DeserializeConfig(root);
    if (cfg.items.empty()) {
        return CreateDefaultConfig();
    }
    return cfg;
}

bool SaveConfig(const AppConfig& config) {
    std::wstring path = GetConfigPath();
    std::wstring tmpPath = path + L".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::string serialized = SerializeConfig(config).Dump(2);
    ofs.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    ofs.flush();
    bool good = ofs.good();
    ofs.close();
    if (!good) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

static bool MatchesModifierKey(const MenuItem& item, const SelectionContext& ctx) {
    const std::wstring& mod = item.rules.modifierKey;
    if (mod.empty() || mod == L"none") return true;
    if (mod == L"ctrl") return ctx.ctrlHeld;
    if (mod == L"shift") return ctx.shiftHeld;
    if (mod == L"alt") return ctx.altHeld;
    if (mod == L"ctrl_shift") return ctx.ctrlHeld && ctx.shiftHeld;
    return true;
}

bool MatchesContext(const MenuItem& item, const SelectionContext& ctx) {
    if (!item.enabled) return false;
    if (item.kind == ItemKind::Separator) return true;
    if (!MatchesModifierKey(item, ctx)) return false;
    if (item.kind == ItemKind::Category) return true; // Visibility determined by modifier + visible children

    if (item.builtIn && item.builtInAction == L"settings") {
        return true;
    }

    if (ctx.isBackground) {
        return item.rules.showOnBackground;
    }

    if (!item.rules.allowMultiSelect && ctx.paths.size() > 1) {
        return false;
    }

    if (ctx.isFolder) {
        return item.rules.showOnFolders;
    }

    // Files
    if (!item.rules.showOnFiles) return false;

    if (!item.rules.extensions.empty()) {
        for (const auto& p : ctx.paths) {
            std::wstring ext = NormalizeExtension(p);
            bool found = false;
            for (const auto& allowed : item.rules.extensions) {
                if (_wcsicmp(ext.c_str(), allowed.c_str()) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
    }

    if (!item.rules.fileCategories.empty()) {
        for (const auto& p : ctx.paths) {
            std::wstring cat = GetFileCategoryForPath(p);
            bool found = false;
            for (const auto& allowed : item.rules.fileCategories) {
                if (_wcsicmp(cat.c_str(), allowed.c_str()) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
    }

    return true;
}

bool HasAnyVisibleCommand(const AppConfig& config, const std::wstring& parentId, const SelectionContext& ctx) {
    for (const auto& item : config.items) {
        if (!item.enabled || item.parentId != parentId) continue;
        if (item.kind == ItemKind::Command && MatchesContext(item, ctx)) {
            return true;
        }
        if (item.kind == ItemKind::Category && MatchesModifierKey(item, ctx) && HasAnyVisibleCommand(config, item.id, ctx)) {
            return true;
        }
    }
    return false;
}

std::vector<const MenuItem*> GetVisibleChildren(const AppConfig& config, const std::wstring& parentId, const SelectionContext& ctx) {
    std::vector<const MenuItem*> raw;
    for (const auto& item : config.items) {
        if (!item.enabled || item.parentId != parentId) continue;
        if (item.kind == ItemKind::Command) {
            if (MatchesContext(item, ctx)) raw.push_back(&item);
        } else if (item.kind == ItemKind::Category) {
            if (MatchesModifierKey(item, ctx) && HasAnyVisibleCommand(config, item.id, ctx)) raw.push_back(&item);
        } else if (item.kind == ItemKind::Separator) {
            raw.push_back(&item);
        }
    }

    std::stable_sort(raw.begin(), raw.end(), [](const MenuItem* a, const MenuItem* b) {
        return a->order < b->order;
    });

    // Clean up leading, trailing, and consecutive separators
    std::vector<const MenuItem*> cleaned;
    for (const auto* item : raw) {
        if (item->kind == ItemKind::Separator) {
            if (cleaned.empty() || cleaned.back()->kind == ItemKind::Separator) continue;
        }
        cleaned.push_back(item);
    }
    while (!cleaned.empty() && cleaned.back()->kind == ItemKind::Separator) {
        cleaned.pop_back();
    }
    return cleaned;
}

const MenuItem* FindItemById(const AppConfig& config, const std::wstring& id) {
    for (const auto& item : config.items) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

std::wstring ResolveIconPath(const std::wstring& iconSpec, const AppearanceConfig& appearance, HMODULE hModule) {
    if (iconSpec.empty()) return {};
    if (iconSpec.find(L':') != std::wstring::npos || iconSpec.find(L',') != std::wstring::npos) {
        return iconSpec;
    }

    bool useDark = true;
    if (appearance.iconTheme == L"light") useDark = false;
    else if (appearance.iconTheme == L"dark") useDark = true;
    else useDark = theme::IsSystemDarkMode();

    std::wstring themeFolder = useDark ? L"Dark" : L"Light";

    WCHAR modPath[MAX_PATH] = {};
    if (GetModuleFileNameW(hModule, modPath, MAX_PATH) == 0) return iconSpec;
    PathRemoveFileSpecW(modPath);
    fs::path baseDir(modPath);

    std::wstring rel = iconSpec;
    std::wstring themeCandidate;
    if (rel.rfind(L"Icons\\", 0) == 0) {
        themeCandidate = L"Icons\\" + themeFolder + L"\\" + rel.substr(6);
    } else if (rel.rfind(L"Assets\\", 0) == 0) {
        themeCandidate = L"Icons\\" + themeFolder + L"\\" + rel.substr(7);
    } else {
        themeCandidate = L"Icons\\" + themeFolder + L"\\" + rel;
    }

    fs::path p1 = baseDir / themeCandidate;
    if (fs::exists(p1)) return p1.wstring();

    fs::path p2 = baseDir / rel;
    if (fs::exists(p2)) return p2.wstring();

    return iconSpec;
}

static std::wstring UtcNowIso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc{};
    gmtime_s(&tmUtc, &t);
    wchar_t buf[64] = {};
    wcsftime(buf, 64, L"%Y-%m-%dT%H:%M:%SZ", &tmUtc);
    return buf;
}

void AppendExecutionLog(const ExecutionLogEntry& entry) {
    json::Value j;
    j["timestampUtc"] = WideToUtf8(entry.timestampUtc.empty() ? UtcNowIso8601() : entry.timestampUtc);
    j["commandId"] = WideToUtf8(entry.commandId);
    j["title"] = WideToUtf8(entry.title);
    j["executable"] = WideToUtf8(entry.executable);
    j["arguments"] = WideToUtf8(entry.arguments);
    j["workingDirectory"] = WideToUtf8(entry.workingDirectory);
    j["consoleVisibility"] = WideToUtf8(entry.consoleVisibility);
    json::Value::Array targets;
    for (const auto& t : entry.targets) targets.push_back(WideToUtf8(t));
    j["targets"] = json::Value(std::move(targets));
    j["exitCode"] = entry.exitCode;
    j["durationMs"] = entry.durationMs;
    j["success"] = entry.success;
    j["outputSnippet"] = WideToUtf8(entry.outputSnippet);
    j["errorMessage"] = WideToUtf8(entry.errorMessage);

    std::ofstream ofs(GetExecutionLogPath(), std::ios::binary | std::ios::app);
    if (ofs.is_open()) {
        std::string line = j.Dump(-1);
        ofs << line << "\n";
    }
}

void AppendDiagnosticLog(const std::wstring& level, const std::wstring& message) {
    std::ofstream ofs(GetDiagnosticLogPath(), std::ios::binary | std::ios::app);
    if (ofs.is_open()) {
        ofs << "[" << WideToUtf8(UtcNowIso8601()) << "] [" << WideToUtf8(level) << "] " << WideToUtf8(message) << "\n";
    }
}

} // namespace xmf
