# xToolsMenu feature inventory

Source reviewed: xCONFLiCTiONx/xToolsMenu default branch, 2026-11-23.

This is a behavior inventory for the successor. It is not a promise that every implementation detail or legacy behavior will be copied unchanged.

## Existing shell integration

- Modern Windows 11 integration: sparse-package manifest registering an in-process COM server implementing IExplorerCommand.
- Modern menu root exposes child commands through EnumSubCommands.
- Classic integration: COM IContextMenu / IShellExtInit handler registered for files, directories, and directory backgrounds.
- Runtime context detection uses selected shell items and the site to distinguish selected targets from a directory background.
- The current implementation evaluates command visibility at menu-query time.

## Existing built-in command families

- Attributes dialog.
- Terminal and elevated terminal.
- Edit With.
- System Folders.
- Paste to File.
- Copy Name and Copy Path.
- Take Ownership.
- Settings entry.

The source also defines hidden/super-hidden attribute actions and custom command handling. Each action needs an individual behavior and security review before being carried forward.

## Existing context rules

The current implementation has separate visibility settings for files, directories, and directory backgrounds. Examples from the existing rules:

| Command | Files | Folders | Background |
|---|---:|---:|---:|
| Attributes | Yes | Yes | Yes |
| Copy Name / Copy Path | Yes | Yes | No |
| Edit With | Yes | No | No |
| Take Ownership | Yes | Yes | No |
| Terminal / Admin Terminal | No | Yes | Yes |
| System Folders | No | No | Yes |
| Paste to File | No | No | Yes |

These defaults are a reference point; the new designer should let users define rules rather than hard-code the table.

## Existing custom-command capabilities

- Custom executable or script path.
- Display name and icon.
- Per-target visibility for files, folders, and backgrounds.
- File-category filtering for selected files.
- Per-menu enablement in the old modern/classic settings model.
- Optional elevated execution.

The new command schema should replace registry-per-command settings with a validated, versioned configuration format. It should support explicit argument templates, working-directory policies, environment variables, console visibility, and observable execution results.

## Existing settings application

The current settings app is a native Win32 window with a sidebar and separate Files, Directory, Background, and Custom pages. It includes dark-mode handling and a single-instance guard. The main window is currently created at 850 × 600.

The successor should retain the useful organization and context-specific configuration while providing the planned dedicated areas: Overview, Menu Designer, Commands, Custom Commands, Execution, Appearance, Context Rules, Integrations, and Diagnostics.

## Existing build and packaging observations

- The repository contains multiple Visual C++ projects and a solution.
- The shell extension project targets C++17 in its checked-in project configuration.
- The existing .build.ps1 stages binaries and invokes Inno Setup; it expects a set of separately built executables.
- The current installer registers both modern and classic COM integration and restarts Explorer.

The new build pipeline should be simpler and deterministic, build **Release | x64** by default, validate all prerequisites before modifying the machine, and keep install/uninstall ownership narrowly scoped.

## Items requiring deliberate redesign

1. Replace scattered registry settings with versioned configuration and safe migration.
2. Define one canonical command model shared by the settings UI and shell integration.
3. Isolate command execution from Explorer's COM callback process.
4. Add explicit process visibility, output, timeout, working directory, and exit-code semantics.
5. Make registration, upgrade, repair, rollback, and uninstall idempotent.
6. Avoid broad Explorer restarts unless required; provide a clear recovery path.
7. Validate the supported modern-menu hierarchy on the target Windows build before designing the Menu Designer around it.
