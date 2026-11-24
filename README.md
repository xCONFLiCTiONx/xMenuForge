# xMenuForge

**Your Windows. Your commands. Your context menu.**

xMenuForge is the planned successor to [xToolsMenu](https://github.com/xCONFLiCTiONx/xToolsMenu): a native Windows context-menu platform built around configurable commands, context-aware visibility, and a polished dark settings experience.

The goal is to make Explorer's context menu genuinely useful without making it cluttered. Users will be able to compose their own command collections, organize them into categories, control where each command appears, and define exactly how commands execute.

> **Development status: Architecture and compatibility validation.** No release build is available yet. The modern Windows 11 menu's supported submenu depth is a release-blocking design question and will be proven on a real Windows installation before the menu architecture is committed.

## Product goals

- Native Windows Explorer integration; no background service required for ordinary menu display.
- User-defined commands and categories, with ordering, icons, enable/disable controls, and context rules.
- Context-aware targeting for files, folders, and directory backgrounds.
- File-extension and file-category filtering.
- Per-command execution settings: show, hide, or inherit console visibility; working directory; arguments; environment; elevation; timeout; output and exit-code reporting.
- Built-in tools carried forward from xToolsMenu where they remain useful.
- A fully dark, polished settings application, designed for a maximum main-window size of 1280 × 720.
- Reliable install, update, repair, and uninstall behavior that preserves user configuration.
- Release-first build and packaging workflow.

## Language and platform

The implementation baseline is **C++20** for native Windows integration, COM interfaces, process execution, and shared application logic. The shell extension and settings application will remain native Windows software. The settings UI framework will be selected after the existing application and available Windows SDK/toolchain are audited; it will not be chosen merely for novelty.

## Compatibility rule: prove menu depth first

Windows exposes IExplorerCommand and IEnumExplorerCommand for modern context-menu extensions. The existing xToolsMenu implementation uses a root command with enumerated child commands. Microsoft documents limitations around recursive subcommands; therefore, xMenuForge will not assume that an extra submenu level or an app-extension grouping trick is supported.

A small, isolated proof of concept must establish, on the target Windows build:

1. The maximum supported submenu depth for the chosen registration model.
2. Whether separately registered commands from one app are grouped, and exactly how that grouping behaves.
3. Whether the behavior survives Explorer restart, sign-out, and reboot.
4. Whether icons, context filtering, and command invocation remain reliable.

If the modern menu cannot provide the desired hierarchy, the product will use a supported, clearly documented fallback rather than ship a fragile workaround.

## Planned settings areas

Overview · Menu Designer · Commands · Custom Commands · Execution · Appearance · Context Rules · Integrations · Diagnostics

## Build and development

- Visual Studio / MSBuild C++ toolchain and Windows SDK.
- PowerShell build entry point: .build.ps1.
- Default build configuration: **Release | x64**.
- No Docker, GitHub Actions, paid build services, or unnecessary downloaded runtimes.
- Every meaningful change should be committed, and a feature is not considered complete until its Release build and relevant behavior have been tested.

## Feature reference

The existing xToolsMenu repository is the behavioral reference, not a codebase to copy blindly. See [the feature inventory](docs/feature-inventory.md) and [the architecture decision record](docs/architecture/0001-platform-and-validation.md).

## License

See [LICENSE](LICENSE).
