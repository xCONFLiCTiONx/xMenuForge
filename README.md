# xMenuForge

**Your Windows. Your commands. Your context menu.**

xMenuForge is the native Windows 11 successor to [xToolsMenu](https://github.com/xCONFLiCTiONx/xToolsMenu): a customizable context-menu platform with a native C++20 COM shell extension, an isolated native command execution host, and a fully dark-themed WPF Settings application.

## Architecture

- **`xMenuForge.Shell.dll` (C++20 COM In-Process Server)**
  - **Windows 11 Modern Context Menu (`IExplorerCommand` + `IObjectWithSite` + `IEnumExplorerCommand`)** registered via Sparse Package (`AppxManifest.xml` using `windows.comServer` + `windows.fileExplorerContextMenus`).
  - Strictly adheres to Microsoft's documented `IExplorerCommand::EnumSubCommands` 1-level contract: nested categories are rendered either as a **Category Flyout** (fast dark-themed cursor popup supporting arbitrary nesting) or **Flattened with Visual Separators** (`ECF_SEPARATORBEFORE`).
  - **Optional Classic Context Menu (`IContextMenu` + `IShellExtInit`)** with full recursive Win32 `HMENU` cascading submenus.
  - Zero heavy work inside Explorer: COM `Invoke` immediately hands off to `xMenuForge.Host.exe`.
- **`xMenuForge.Host.exe` (C++20 Command Runner & Native Dialogs)**
  - Isolated process execution engine supporting **Executable / GUI**, **PowerShell** (`pwsh` / `powershell`), **CMD**, and **Windows Terminal** (`wt.exe`).
  - Per-command **Console Visibility** (`Default`, `Show`, `Hide`), working directory policy (`Target`, `Parent`, `Custom`), argument token expansion (`{path}`, `{paths}`, `{dir}`, `{name}`, `{stem}`, `{ext}`, `%1`, `%V`), environment variables, elevation (`runas`), timeouts, and structured JSONL execution logging (`%LOCALAPPDATA%\xMenuForge\logs\executions.jsonl`) with stdout/stderr capture for hidden commands.
  - Built-in tools carried forward from xToolsMenu: **Attributes** (dark-themed dialog with timestamps & Explorer visibility toggles), **Windows Terminal / PowerShell / CMD** (normal & Admin), **Edit With**, **System Folders**, **Paste to File**, **Copy Name**, **Copy Path**, and **Take Ownership** (auto-elevating dark progress dialog).
- **`xMenuForge.Settings.exe` (WPF .NET 10, Obsidian Fluent Dark)**
  - Fully dark-themed (custom borderless title bar, DWM dark frame & rounded corners, dark modals, Per-Monitor V2 DPI aware, max size `1280 × 720`).
  - 9 navigation sections: **Overview**, **Menu Designer** (drag-and-drop + live context menu preview), **Commands**, **Custom Commands**, **Execution**, **Appearance**, **Context Rules**, **Integrations**, and **Diagnostics**.
  - Versioned configuration stored atomically at `%LOCALAPPDATA%\xMenuForge\config.json` with one-click **Import from xToolsMenu**.

## Build & Install (Release | x64)

Prerequisites: Visual Studio 2022/2026 C++ Desktop toolchain + Windows 11 SDK + CMake + .NET 10 SDK (optional: Inno Setup 6 for `xMenuForge_Setup.exe`).

```powershell
# Build Release | x64 and stage to dist\xMenuForge
.\.build.ps1

# Build, install to %LOCALAPPDATA%\Programs\xMenuForge, and register with Explorer
.\.build.ps1 -Install -RestartExplorer

# Build including Inno Setup installer (dist\xMenuForge_Setup.exe)
.\.build.ps1 -BuildInstaller
```

## License

See [LICENSE](LICENSE).
