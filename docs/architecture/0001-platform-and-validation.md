# ADR 0001: Native platform and shell-menu validation

- **Status:** Accepted for language baseline; UI framework and menu hierarchy remain open pending validation.
- **Date:** 2026-11-23

## Context

xToolsMenu is an established native C++ shell extension. Its modern Windows 11 entry point is a COM IExplorerCommand implementation registered through a sparse package manifest. It also contains a classic IContextMenu implementation and a Win32 settings application. Its command visibility logic distinguishes files, folders, and directory backgrounds, and its custom-command settings include file-category filters.

xMenuForge is intended to become a substantially more configurable successor. Its key differentiator depends on how much hierarchy the modern Windows 11 context menu can actually support. This must be established before the menu model is designed around nested categories.

## Decision

1. Use **C++20** as the primary implementation language.
2. Keep Explorer integration native and COM-based. Keep COM callbacks short, defensive, and free of long-running process work.
3. Separate shell registration, menu-model evaluation, command execution, persistence, and settings UI into independently testable components.
4. Treat user configuration as versioned application data, not as scattered registry values. Registry use is limited to the Windows registration surface where required.
5. The command execution layer must explicitly model console visibility (Default, Show, Hide), working directory, argument construction, environment overrides, elevation, timeout, output capture, and exit status.
6. Do not commit to recursive IExplorerCommand::EnumSubCommands or assume that app grouping creates another submenu level.
7. Build a minimal compatibility proof of concept before implementing the full menu designer.
8. Select the settings UI framework after auditing the target Windows SDK, installed build tools, deployment requirements, accessibility, and the desired visual quality. The framework must support a fully dark interface and a main window no larger than 1280 × 720.

## Required proof-of-concept tests

- Register one root command with one child and verify its placement in the modern context menu.
- Test a child that advertises its own children only as an isolated experiment; do not make it a product dependency unless Windows demonstrably supports it.
- Register multiple independent root commands from the same app identity and record whether/how Windows groups them.
- Test file, folder, and directory-background targets.
- Verify menu behavior after restarting Explorer and after reboot.
- Verify that uninstall removes only xMenuForge-owned registrations.

Record the Windows edition, build number, registration manifest, exact menu layout, and test result. A screenshot and reproducible test instructions should accompany the result.

## Consequences

- The implementation starts with a testable native core rather than a premature UI or menu hierarchy.
- The modern menu may have to use one submenu level, a flat command arrangement, or another supported composition model. The product will not claim arbitrary nesting until proven.
- Classic-menu compatibility is a separate optional integration decision; it will not be retained automatically just because xToolsMenu has it.
- The settings UI framework remains deliberately undecided until the audit is complete.
