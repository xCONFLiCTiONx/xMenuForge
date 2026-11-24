# xMenuForge modern context-menu POC

This is an intentionally small Windows-only C++20 shell extension. It is not the product implementation.

## What it probes

- A native in-process COM DLL implementing `IExplorerCommand`.
- Sparse-package identity and `windows.fileExplorerContextMenus` registration.
- Two independent root verbs registered under one app identity.
- File, selected-folder, and folder-background registration.
- A root command with leaf subcommands.
- A grandchild-depth probe. **Microsoft explicitly documents that commands which themselves have subcommands are not supported by File Explorer.** The extra level is included as a negative/diagnostic probe, not as a supported feature.

## Important result before running anything

The current Microsoft contract says the requested second submenu layer is unsupported. See [IExplorerCommand::EnumSubCommands](https://learn.microsoft.com/windows/win32/api/shobjidl_core/nf-shobjidl_core-iexplorercommand-enumsubcommands), specifically its Remarks section. The Windows 11 packaged-app guidance confirms that modern-menu integration uses a native COM DLL plus sparse-package identity and manifest registration: [Microsoft Learn](https://learn.microsoft.com/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer).

This means xMenuForge must **not** be architected around recursive `IExplorerCommand::EnumSubCommands`. The POC still validates the actual Windows 11 registration and first-level flyout path, and tests whether any newer shell build behaves differently from the documented contract.

## Build

Run from a Visual Studio Developer PowerShell with the Windows 11 SDK and CMake installed:

```powershell
Clear-Host
cmake -S .\poc -B .\poc\build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build .\poc\build --config Release
```

The manifest is a registration template. The installer/test harness is intentionally not automated until package identity signing and external-location registration are verified on the target machine; this avoids making an unsafe or non-reversible Explorer registration assumption.

## Current limitations

- This first implementation pass is source-only and has not been compiled or run on the user's Windows machine.
- The grandchild probe currently represents the depth boundary in the test plan; the COM implementation intentionally exposes only the first level to Explorer.
- No settings UI, installer, or product command model is included.
