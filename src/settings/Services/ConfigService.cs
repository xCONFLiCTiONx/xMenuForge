using System.Diagnostics;
using System.IO;
using System.Text.Json;
using Microsoft.Win32;
using xMenuForge.Settings.Models;

namespace xMenuForge.Settings.Services;

public sealed class RegistrationHealth
{
    public bool ShellDllExists { get; set; }
    public bool HostExeExists { get; set; }
    public bool ManifestExists { get; set; }
    public bool ModernPackageRegistered { get; set; }
    public string PackageFullName { get; set; } = "";
    public bool ModernComRegistered { get; set; }
    public bool ClassicComRegistered { get; set; }
    public bool ClassicHandlersRegistered { get; set; }
    public bool ConfigExists { get; set; }
    public string InstallDirectory { get; set; } = "";
}

public static class ConfigService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true
    };

    public static readonly string[] AllFileCategories =
    [
        "Images", "Text", "Code", "Documents", "Audio", "Video", "Archives", "DiskImages", "Executables", "Other"
    ];

    public static string AppDataDir
    {
        get
        {
            var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "xMenuForge");
            Directory.CreateDirectory(dir);
            return dir;
        }
    }

    public static string ConfigPath => Path.Combine(AppDataDir, "config.json");
    public static string LogsDir
    {
        get
        {
            var dir = Path.Combine(AppDataDir, "logs");
            Directory.CreateDirectory(dir);
            return dir;
        }
    }
    public static string ExecutionLogPath => Path.Combine(LogsDir, "executions.jsonl");
    public static string DiagnosticLogPath => Path.Combine(LogsDir, "xmenuforge.log");
    public static string InstallDir => AppContext.BaseDirectory.TrimEnd('\\');

    public static AppConfig CreateDefaultConfig()
    {
        var cfg = new AppConfig { SchemaVersion = 1 };
        cfg.Appearance.WindowMaximized = true;
        int order = 0;

        void AddBuiltIn(string id, string title, string desc, string icon, string action,
                        bool files, bool folders, bool bg, string parentId = "", string modKey = "none",
                        string[]? cats = null, string[]? exts = null)
        {
            cfg.Items.Add(new MenuItemConfig
            {
                Id = id,
                Kind = "command",
                ParentId = parentId,
                Order = order++,
                Enabled = true,
                BuiltIn = true,
                Title = title,
                Description = desc,
                Icon = icon,
                BuiltInAction = action,
                Rules = new ContextRulesConfig
                {
                    ShowOnFiles = files,
                    ShowOnFolders = folders,
                    ShowOnBackground = bg,
                    ModifierKey = modKey,
                    FileCategories = cats?.ToList() ?? new(),
                    Extensions = exts?.ToList() ?? new()
                }
            });
        }

        void AddCustomApp(string id, string title, string desc, string icon, string exe, string args,
                          bool files, bool folders, bool bg, bool admin, string parentId = "", string modKey = "none",
                          string[]? exts = null)
        {
            cfg.Items.Add(new MenuItemConfig
            {
                Id = id,
                Kind = "command",
                ParentId = parentId,
                Order = order++,
                Enabled = true,
                BuiltIn = false,
                Title = title,
                Description = desc,
                Icon = icon,
                Runner = new RunnerConfig
                {
                    Type = "executable",
                    Executable = exe,
                    Arguments = args,
                    RunAsAdmin = admin
                },
                Rules = new ContextRulesConfig
                {
                    ShowOnFiles = files,
                    ShowOnFolders = folders,
                    ShowOnBackground = bg,
                    ModifierKey = modKey,
                    Extensions = exts?.ToList() ?? new()
                }
            });
        }

        void AddCategory(string id, string title, string icon, string modKey = "none")
        {
            cfg.Items.Add(new MenuItemConfig
            {
                Id = id,
                Kind = "category",
                Order = order++,
                Enabled = true,
                BuiltIn = true,
                Title = title,
                Icon = icon,
                Rules = new ContextRulesConfig { ModifierKey = modKey }
            });
        }

        void AddSep(string id, string parentId = "")
        {
            cfg.Items.Add(new MenuItemConfig
            {
                Id = id,
                Kind = "separator",
                ParentId = parentId,
                Order = order++,
                Enabled = true,
                BuiltIn = true,
                Title = "---"
            });
        }

        // Top-level essentials & real native Windows 11 submenus (Edit With, Terminals, System Folders)
        AddBuiltIn("builtin.attributes", "Attributes", "View and modify file attributes and timestamps", @"Icons\Attributes.ico", "attributes", true, true, true);
        AddBuiltIn("builtin.edit_with", "Edit With", "Open selected files in an installed editor", @"Icons\Edit with.ico", "edit_with", true, false, false);

        // Terminals (native Win11 top-level submenu)
        AddCategory("cat.terminals", "Terminals", @"Icons\Terminals.ico");
        AddBuiltIn("builtin.terminal", "Windows Terminal", "Open Windows Terminal in target folder", @"Icons\Terminals.ico", "terminal", false, true, true, "cat.terminals");
        AddBuiltIn("builtin.terminal_admin", "Windows Terminal (Admin)", "Open elevated Windows Terminal in target folder", @"Icons\Terminals.ico", "terminal_admin", false, true, true, "cat.terminals");
        AddBuiltIn("builtin.powershell", "PowerShell", "Open PowerShell in target folder", @"Icons\Terminals.ico", "powershell", false, true, true, "cat.terminals");
        AddBuiltIn("builtin.powershell_admin", "PowerShell (Admin)", "Open elevated PowerShell in target folder", @"Icons\Terminals.ico", "powershell_admin", false, true, true, "cat.terminals");
        AddBuiltIn("builtin.cmd", "Command Prompt", "Open Command Prompt in target folder", @"Icons\Terminals.ico", "cmd", false, true, true, "cat.terminals");
        AddBuiltIn("builtin.cmd_admin", "Command Prompt (Admin)", "Open elevated Command Prompt in target folder", @"Icons\Terminals.ico", "cmd_admin", false, true, true, "cat.terminals");

        // System Folders (native Win11 top-level submenu)
        AddCategory("cat.system_folders", "System Folders", @"Icons\System Folders.ico");
        AddBuiltIn("builtin.sys_startmenu_user", "Start Menu (User)", "Open user Start Menu folder", @"Icons\System Folders.ico", "sys_startmenu_user", false, false, true, "cat.system_folders");
        AddBuiltIn("builtin.sys_startmenu_all", "Start Menu (All Users)", "Open All Users Start Menu folder", @"Icons\System Folders.ico", "sys_startmenu_all", false, false, true, "cat.system_folders");
        AddBuiltIn("builtin.sys_temp", "Temp Folder", "Open user Temp folder", @"Icons\System Folders.ico", "sys_temp", false, false, true, "cat.system_folders");
        AddBuiltIn("builtin.sys_appdata_local", "AppData (Local)", "Open Local AppData folder", @"Icons\System Folders.ico", "sys_appdata_local", false, false, true, "cat.system_folders");
        AddBuiltIn("builtin.sys_appdata_roaming", "AppData (Roaming)", "Open Roaming AppData folder", @"Icons\System Folders.ico", "sys_appdata_roaming", false, false, true, "cat.system_folders");
        AddBuiltIn("builtin.sys_programdata", "ProgramData", "Open ProgramData folder", @"Icons\System Folders.ico", "sys_programdata", false, false, true, "cat.system_folders");
        AddBuiltIn("builtin.sys_startup", "Startup (User)", "Open user Startup folder", @"Icons\System Folders.ico", "sys_startup", false, false, true, "cat.system_folders");

        // Flat top-level file/folder/system tools
        AddBuiltIn("builtin.copy_name", "Copy Name", "Copy selected file or folder names to clipboard", @"Icons\Copy Name.ico", "copy_name", true, true, false);
        AddBuiltIn("builtin.copy_path", "Copy Path", "Copy full paths of selected items to clipboard", @"Icons\Copy Path.ico", "copy_path", true, true, false);
        AddBuiltIn("builtin.copy_file_contents", "Copy File Contents", "Read file text directly to clipboard", @"C:\WINDOWS\System32\imageres.dll,90", "copy_file_contents", true, false, false);
        AddBuiltIn("builtin.copy_sha256", "Copy File Hash (SHA-256)", "Compute SHA-256 hash natively and copy to clipboard", @"C:\Windows\System32\imageres.dll,77", "copy_sha256", true, false, false);
        AddBuiltIn("builtin.copy_tree_view", "Copy Tree View", "Copy recursive folder tree paths to clipboard", @"C:\WINDOWS\System32\imageres.dll,7", "copy_tree_view", false, true, true);
        AddBuiltIn("builtin.paste_to_file", "Paste to File", "Save clipboard text directly into a file here", @"Icons\Paste to File.ico", "paste_to_file", false, false, true);
        AddBuiltIn("builtin.unzip_here", "Unzip Here", "Extract archive into a subfolder of the same name", @"C:\WINDOWS\System32\imageres.dll,165", "unzip_here", true, false, false, "", "none", null, [".zip"]);
        AddBuiltIn("builtin.search_clipboard", "Search Clipboard Name", "Search folder recursively for the name on your clipboard", @"C:\WINDOWS\System32\imageres.dll,168", "search_clipboard", false, true, true);

        // Modifier-gated power tools (Hold Ctrl / Alt / Ctrl+Alt)
        AddBuiltIn("builtin.take_ownership", "Take Ownership", "Take ownership and grant Full Control recursively", @"Icons\Take Ownership.ico", "take_ownership", true, true, false, "", "ctrl");
        AddBuiltIn("builtin.flip_image", "Flip Image Horizontally", "Flip selected image horizontally in place", @"C:\WINDOWS\System32\imageres.dll,108", "flip_image", true, false, false, "", "ctrl", ["Images"]);
        AddBuiltIn("builtin.create_junction", "Create Junction...", "Create an NTFS directory junction to this folder", @"C:\WINDOWS\System32\imageres.dll,166", "create_junction", false, true, false, "", "ctrl");
        AddBuiltIn("builtin.mount_image", "Mount Disk Image", "Mount ISO/VHD/VHDX disk image", @"C:\Windows\System32\imageres.dll,56", "mount_image", true, false, false, "", "ctrl", ["DiskImages"]);
        AddBuiltIn("builtin.unmount_image", "Unmount Disk Image", "Dismount mounted ISO/VHD/VHDX disk image", @"C:\Windows\System32\imageres.dll,268", "unmount_image", true, false, false, "", "ctrl", ["DiskImages"]);
        AddBuiltIn("builtin.lock_file", "Lock File (Exclusive)", "Hold an exclusive file lock until closed", @"C:\Windows\System32\imageres.dll,297", "lock_file", true, false, false, "", "ctrl");
        AddBuiltIn("builtin.lock_check", "Check File Locks", "Show processes locking this file via Restart Manager", @"C:\Windows\System32\imageres.dll,54", "lock_check", true, true, false, "", "ctrl");
        AddBuiltIn("builtin.view_sqlite_db", "View SQLite DB", "Inspect SQLite database tables via sqlite3", @"C:\WINDOWS\System32\imageres.dll,262", "view_sqlite_db", true, false, false, "", "ctrl", null, [".db", ".sqlite", ".sqlite3"]);
        AddBuiltIn("builtin.restart_explorer", "Restart Explorer", "Restart Windows Explorer cleanly", @"C:\Windows\explorer.exe,0", "restart_explorer", false, false, true, "", "ctrl");
        AddBuiltIn("builtin.wifi_passwords", "WiFi Passwords", "Display saved Wi-Fi SSIDs and passphrases", @"C:\WINDOWS\System32\imageres.dll,139", "wifi_passwords", false, false, true, "", "ctrl");
        AddBuiltIn("builtin.clear_icon_cache", "Clear Icon Cache", "Rebuild Windows icon cache and refresh Explorer", @"C:\WINDOWS\System32\imageres.dll,16", "clear_icon_cache", false, false, true, "", "alt");
        AddBuiltIn("builtin.clear_print_queue", "Clear Print Queue", "Stop spooler, clear stuck print jobs, restart spooler", @"C:\WINDOWS\System32\imageres.dll,45", "clear_print_queue", false, false, true, "", "alt");
        AddBuiltIn("builtin.add_to_path", "Add Folder to System PATH", "Append selected folder to Machine PATH", @"C:\WINDOWS\System32\imageres.dll,89", "add_to_path", false, true, true, "", "alt");
        AddBuiltIn("builtin.ip_renew", "IP Renew", "Release & renew DHCP lease and flush DNS", @"C:\WINDOWS\System32\imageres.dll,20", "ip_renew", false, false, true, "", "alt");
        AddBuiltIn("builtin.check_internet", "Check for Internet", "Monitor connectivity to 8.8.8.8 and alert when online", @"C:\WINDOWS\System32\imageres.dll,20", "check_internet", false, false, true, "", "alt");
        AddBuiltIn("builtin.ip_watcher", "IP Watcher", "Monitor a host or IP for online/offline transitions", @"C:\WINDOWS\System32\imageres.dll,272", "ip_watcher", false, false, true, "", "alt");
        AddBuiltIn("builtin.flush_dns", "Flush DNS", "Flush Windows DNS resolver cache", @"C:\WINDOWS\System32\imageres.dll,114", "flush_dns", false, false, true, "", "ctrl_alt");

        // Editable external program launchers
        AddCustomApp("app.antigravity", "Open in Antigravity", "Open file or folder in Antigravity IDE",
                     @"C:\Users\Michael\AppData\Local\Programs\Antigravity IDE\Antigravity IDE.exe,0",
                     @"C:\Users\Michael\AppData\Local\Programs\Antigravity IDE\Antigravity IDE.exe",
                     "\"{path}\"", true, true, true, false);
        AddCustomApp("app.chrome", "Open in Chrome", "Open file in Google Chrome",
                     @"C:\Program Files\Google\Chrome\Application\chrome.exe,8",
                     @"C:\Program Files\Google\Chrome\Application\chrome.exe",
                     "\"{path}\"", true, false, false, false, "", "none",
                     [".html", ".htm", ".svg", ".pdf", ".webp", ".png", ".jpg", ".jpeg", ".gif", ".txt", ".json", ".xml"]);
        AddCustomApp("app.directory_icon_tool", "Directory Icon Tool", "Customize folder icons with DirectoryIconTool",
                     @"C:\Users\Michael\AppData\Local\xPrograms\DirectoryIconTool\DirectoryIconTool.exe,0",
                     @"C:\Users\Michael\AppData\Local\xPrograms\DirectoryIconTool\DirectoryIconTool.exe",
                     "\"{path}\"", false, true, true, false, "", "ctrl");
        AddCustomApp("app.unlocker", "Unlocker", "Unlock locked files or folders with Unlock Files",
                     @"C:\Users\Michael\AppData\Local\xPrograms\UnlockFiles\Unlock Files.exe,0",
                     @"C:\Users\Michael\AppData\Local\xPrograms\UnlockFiles\Unlock Files.exe",
                     "\"{path}\"", true, true, true, true, "", "ctrl");

        AddSep("builtin.sep_settings");
        AddBuiltIn("builtin.settings", "xMenuForge Settings", "Configure menus, commands, and execution rules", @"Icons\Settings.ico", "settings", true, true, true);

        return cfg;
    }

    public static AppConfig Load()
    {
        try
        {
            if (!File.Exists(ConfigPath))
            {
                var def = CreateDefaultConfig();
                Save(def);
                return def;
            }
            var json = File.ReadAllText(ConfigPath);
            var cfg = JsonSerializer.Deserialize<AppConfig>(json, JsonOptions);
            if (cfg == null || cfg.Items.Count == 0)
            {
                return CreateDefaultConfig();
            }
            return cfg;
        }
        catch
        {
            return CreateDefaultConfig();
        }
    }

    public static void Save(AppConfig config)
    {
        Directory.CreateDirectory(AppDataDir);
        var tmp = ConfigPath + ".tmp";
        var json = JsonSerializer.Serialize(config, JsonOptions);
        File.WriteAllText(tmp, json);
        File.Move(tmp, ConfigPath, overwrite: true);
    }

    public static List<ExecutionLogEntry> LoadExecutionLogs(int maxEntries = 100)
    {
        var list = new List<ExecutionLogEntry>();
        if (!File.Exists(ExecutionLogPath)) return list;
        try
        {
            var lines = File.ReadAllLines(ExecutionLogPath);
            for (int i = lines.Length - 1; i >= 0 && list.Count < maxEntries; i--)
            {
                var line = lines[i].Trim();
                if (string.IsNullOrEmpty(line)) continue;
                try
                {
                    var entry = JsonSerializer.Deserialize<ExecutionLogEntry>(line, JsonOptions);
                    if (entry != null) list.Add(entry);
                }
                catch { }
            }
        }
        catch { }
        return list;
    }

    public static int ImportFromXToolsMenu(AppConfig config)
    {
        int imported = 0;
        try
        {
            using var root = Registry.CurrentUser.OpenSubKey(@"Software\xToolsMenu\CustomCommands");
            if (root == null) return 0;

            int nextOrder = config.Items.Count > 0 ? config.Items.Max(i => i.Order) + 1 : 0;
            foreach (var subName in root.GetSubKeyNames())
            {
                using var sub = root.OpenSubKey(subName);
                if (sub == null) continue;
                var exePath = sub.GetValue("Path") as string ?? "";
                if (string.IsNullOrWhiteSpace(exePath)) continue;

                var id = "custom.imported." + new string(subName.ToLowerInvariant().Where(char.IsLetterOrDigit).ToArray());
                if (config.Items.Any(i => i.Id == id || i.Title.Equals(subName, StringComparison.OrdinalIgnoreCase)))
                    continue;

                var args = sub.GetValue("Args") as string ?? "";
                var icon = sub.GetValue("IconPath_Dark") as string ?? sub.GetValue("IconPath") as string ?? exePath;
                bool showFile = Convert.ToInt32(sub.GetValue("ShowFile") ?? 1) != 0;
                bool showDir = Convert.ToInt32(sub.GetValue("ShowDir") ?? 1) != 0;
                bool showBg = Convert.ToInt32(sub.GetValue("ShowBG") ?? 1) != 0;
                bool runAdmin = Convert.ToInt32(sub.GetValue("RunAsAdmin") ?? 0) != 0;

                config.Items.Add(new MenuItemConfig
                {
                    Id = id,
                    Kind = "command",
                    ParentId = "",
                    Order = nextOrder++,
                    Enabled = true,
                    BuiltIn = false,
                    Title = subName,
                    Description = "Imported from xToolsMenu",
                    Icon = icon,
                    Runner = new RunnerConfig
                    {
                        Type = exePath.EndsWith(".ps1", StringComparison.OrdinalIgnoreCase) ? "powershell" : "executable",
                        Executable = exePath,
                        Arguments = args,
                        RunAsAdmin = runAdmin,
                        ConsoleVisibility = "default"
                    },
                    Rules = new ContextRulesConfig
                    {
                        ShowOnFiles = showFile,
                        ShowOnFolders = showDir,
                        ShowOnBackground = showBg
                    }
                });
                imported++;
            }
        }
        catch { }
        return imported;
    }

    public static async Task<RegistrationHealth> QueryHealthAsync()
    {
        return await Task.Run(() =>
        {
            var h = new RegistrationHealth
            {
                InstallDirectory = InstallDir,
                ShellDllExists = File.Exists(Path.Combine(InstallDir, "xMenuForge.Shell.dll")),
                HostExeExists = File.Exists(Path.Combine(InstallDir, "xMenuForge.Host.exe")),
                ManifestExists = File.Exists(Path.Combine(InstallDir, "AppxManifest.xml")),
                ConfigExists = File.Exists(ConfigPath)
            };

            h.ModernComRegistered = Registry.CurrentUser.OpenSubKey(@"Software\Classes\CLSID\{7A4E9C10-5B2D-4F81-A310-9C6E2B410001}\InprocServer32") != null;
            h.ClassicComRegistered = Registry.CurrentUser.OpenSubKey(@"Software\Classes\CLSID\{7A4E9C10-5B2D-4F81-A310-9C6E2B410002}\InprocServer32") != null;
            h.ClassicHandlersRegistered =
                Registry.CurrentUser.OpenSubKey(@"Software\Classes\*\shellex\ContextMenuHandlers\xMenuForge") != null &&
                Registry.CurrentUser.OpenSubKey(@"Software\Classes\Directory\shellex\ContextMenuHandlers\xMenuForge") != null &&
                Registry.CurrentUser.OpenSubKey(@"Software\Classes\Directory\Background\shellex\ContextMenuHandlers\xMenuForge") != null;

            try
            {
                var psi = new ProcessStartInfo("powershell.exe",
                    "-NoProfile -NonInteractive -Command \"$p = Get-AppxPackage -Name 'xCONFLiCTiONx.xMenuForge' -ErrorAction SilentlyContinue; if ($p) { Write-Output $p.PackageFullName }\"")
                {
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true
                };
                using var proc = Process.Start(psi);
                if (proc != null)
                {
                    var output = proc.StandardOutput.ReadToEnd().Trim();
                    proc.WaitForExit(10000);
                    if (!string.IsNullOrEmpty(output))
                    {
                        h.ModernPackageRegistered = true;
                        h.PackageFullName = output;
                    }
                }
            }
            catch { }

            return h;
        });
    }

    public static async Task<(bool Ok, string Message)> RunHostActionAsync(string arguments)
    {
        var hostExe = Path.Combine(InstallDir, "xMenuForge.Host.exe");
        if (!File.Exists(hostExe))
        {
            return (false, $"xMenuForge.Host.exe not found in {InstallDir}.");
        }

        return await Task.Run(() =>
        {
            try
            {
                var psi = new ProcessStartInfo(hostExe, arguments)
                {
                    WorkingDirectory = InstallDir,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                using var proc = Process.Start(psi);
                if (proc == null) return (false, "Failed to start xMenuForge.Host.exe.");
                proc.WaitForExit(60000);
                return proc.ExitCode == 0
                    ? (true, "Operation completed successfully.")
                    : (false, $"Host exited with code {proc.ExitCode}. Check diagnostics log for details.");
            }
            catch (Exception ex)
            {
                return (false, ex.Message);
            }
        });
    }

    public static void RestartExplorer()
    {
        var psi = new ProcessStartInfo("powershell.exe",
            "-NoProfile -NonInteractive -Command \"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 600; if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }\"")
        {
            CreateNoWindow = true,
            UseShellExecute = false
        };
        Process.Start(psi);
    }
}
