using System.Text.Json.Serialization;

namespace xMenuForge.Settings.Models;

public sealed class AppConfig
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; set; } = 1;

    [JsonPropertyName("appearance")]
    public AppearanceConfig Appearance { get; set; } = new();

    [JsonPropertyName("execution")]
    public ExecutionConfig Execution { get; set; } = new();

    [JsonPropertyName("integrations")]
    public IntegrationsConfig Integrations { get; set; } = new();

    [JsonPropertyName("items")]
    public List<MenuItemConfig> Items { get; set; } = new();
}

public sealed class AppearanceConfig
{
    [JsonPropertyName("rootMenuTitle")]
    public string RootMenuTitle { get; set; } = "xMenuForge";

    [JsonPropertyName("rootMenuIcon")]
    public string RootMenuIcon { get; set; } = @"Icons\ICON.ico";

    [JsonPropertyName("showIcons")]
    public bool ShowIcons { get; set; } = true;

    [JsonPropertyName("iconTheme")]
    public string IconTheme { get; set; } = "auto"; // auto | dark | light

    [JsonPropertyName("modernSubmenuMode")]
    public string ModernSubmenuMode { get; set; } = "flyout"; // flyout | flatten
}

public sealed class ExecutionConfig
{
    [JsonPropertyName("defaultConsoleVisibility")]
    public string DefaultConsoleVisibility { get; set; } = "show"; // show | hide

    [JsonPropertyName("defaultPowerShell")]
    public string DefaultPowerShell { get; set; } = "auto"; // auto | pwsh | powershell

    [JsonPropertyName("defaultTerminalProfile")]
    public string DefaultTerminalProfile { get; set; } = "";

    [JsonPropertyName("defaultTimeoutSeconds")]
    public int DefaultTimeoutSeconds { get; set; } = 120;

    [JsonPropertyName("enableExecutionLogging")]
    public bool EnableExecutionLogging { get; set; } = true;

    [JsonPropertyName("logHiddenCommandOutput")]
    public bool LogHiddenCommandOutput { get; set; } = true;

    [JsonPropertyName("notifyOnHiddenError")]
    public bool NotifyOnHiddenError { get; set; } = true;
}

public sealed class IntegrationsConfig
{
    [JsonPropertyName("modernContextMenuEnabled")]
    public bool ModernContextMenuEnabled { get; set; } = true;

    [JsonPropertyName("classicContextMenuEnabled")]
    public bool ClassicContextMenuEnabled { get; set; } = false;
}

public sealed class MenuItemConfig
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = "";

    [JsonPropertyName("kind")]
    public string Kind { get; set; } = "command"; // command | category | separator

    [JsonPropertyName("parentId")]
    public string ParentId { get; set; } = "";

    [JsonPropertyName("order")]
    public int Order { get; set; }

    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = true;

    [JsonPropertyName("builtIn")]
    public bool BuiltIn { get; set; }

    [JsonPropertyName("title")]
    public string Title { get; set; } = "";

    [JsonPropertyName("description")]
    public string Description { get; set; } = "";

    [JsonPropertyName("icon")]
    public string Icon { get; set; } = "";

    [JsonPropertyName("builtInAction")]
    public string BuiltInAction { get; set; } = "";

    [JsonPropertyName("runner")]
    public RunnerConfig Runner { get; set; } = new();

    [JsonPropertyName("rules")]
    public ContextRulesConfig Rules { get; set; } = new();
}

public sealed class RunnerConfig
{
    [JsonPropertyName("type")]
    public string Type { get; set; } = "executable"; // executable | powershell | cmd | windows_terminal

    [JsonPropertyName("executable")]
    public string Executable { get; set; } = "";

    [JsonPropertyName("arguments")]
    public string Arguments { get; set; } = "";

    [JsonPropertyName("workingDirectoryMode")]
    public string WorkingDirectoryMode { get; set; } = "target"; // target | parent | custom

    [JsonPropertyName("customWorkingDirectory")]
    public string CustomWorkingDirectory { get; set; } = "";

    [JsonPropertyName("consoleVisibility")]
    public string ConsoleVisibility { get; set; } = "default"; // default | show | hide

    [JsonPropertyName("runAsAdmin")]
    public bool RunAsAdmin { get; set; }

    [JsonPropertyName("timeoutSeconds")]
    public int TimeoutSeconds { get; set; }

    [JsonPropertyName("logOutput")]
    public bool LogOutput { get; set; } = true;

    [JsonPropertyName("environment")]
    public Dictionary<string, string> Environment { get; set; } = new();
}

public sealed class ContextRulesConfig
{
    [JsonPropertyName("showOnFiles")]
    public bool ShowOnFiles { get; set; } = true;

    [JsonPropertyName("showOnFolders")]
    public bool ShowOnFolders { get; set; } = true;

    [JsonPropertyName("showOnBackground")]
    public bool ShowOnBackground { get; set; }

    [JsonPropertyName("allowMultiSelect")]
    public bool AllowMultiSelect { get; set; } = true;

    [JsonPropertyName("fileCategories")]
    public List<string> FileCategories { get; set; } = new();

    [JsonPropertyName("extensions")]
    public List<string> Extensions { get; set; } = new();
}

public sealed class ExecutionLogEntry
{
    [JsonPropertyName("timestampUtc")]
    public string TimestampUtc { get; set; } = "";

    [JsonPropertyName("commandId")]
    public string CommandId { get; set; } = "";

    [JsonPropertyName("title")]
    public string Title { get; set; } = "";

    [JsonPropertyName("executable")]
    public string Executable { get; set; } = "";

    [JsonPropertyName("arguments")]
    public string Arguments { get; set; } = "";

    [JsonPropertyName("workingDirectory")]
    public string WorkingDirectory { get; set; } = "";

    [JsonPropertyName("consoleVisibility")]
    public string ConsoleVisibility { get; set; } = "";

    [JsonPropertyName("targets")]
    public List<string> Targets { get; set; } = new();

    [JsonPropertyName("exitCode")]
    public int ExitCode { get; set; }

    [JsonPropertyName("durationMs")]
    public long DurationMs { get; set; }

    [JsonPropertyName("success")]
    public bool Success { get; set; }

    [JsonPropertyName("outputSnippet")]
    public string OutputSnippet { get; set; } = "";

    [JsonPropertyName("errorMessage")]
    public string ErrorMessage { get; set; } = "";
}
