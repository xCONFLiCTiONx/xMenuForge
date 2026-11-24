# ADR 0002: Modern context-menu submenu depth

- Status: Accepted
- Date: 2026-11-23

## Decision

Do not design xMenuForge around recursive `IExplorerCommand::EnumSubCommands`.

Microsoft's current API reference for `IExplorerCommand::EnumSubCommands` explicitly states: "Subcommands which themselves have subcommands are not supported by Windows Explorer." A command with children must set `ECF_HASSUBCOMMANDS` in `GetFlags`.

The Windows 11 packaged desktop-app guidance describes the supported modern integration path: native COM DLL implementing `IExplorerCommand`, COM registration in the package manifest, and `windows.fileExplorerContextMenus` registration. It also confirms that commands from the same app can be grouped into an app-attributed flyout, with `EnumSubCommands` supplying child commands. App grouping does not override the documented recursive-subcommand restriction.

## Product consequence

The modern menu supports one command flyout level through this API. A deeper logical hierarchy must be flattened, split into multiple app-attributed roots, or handled by a different UX outside Explorer's native cascading menu. Do not promise a second nested flyout until Microsoft documents support and an actual supported Windows build proves it.

## Sources

- [IExplorerCommand::EnumSubCommands](https://learn.microsoft.com/windows/win32/api/shobjidl_core/nf-shobjidl_core-iexplorercommand-enumsubcommands) (remarks; updated 2024-02-22)
- [Add a File Explorer context menu command to a packaged desktop app](https://learn.microsoft.com/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer) (updated 2026-07-17)
