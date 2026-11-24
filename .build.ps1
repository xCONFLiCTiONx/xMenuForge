[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$Install,
    [switch]$EnableClassicMenu,
    [switch]$RestartExplorer,
    [switch]$BuildInstaller,
    [string]$InstallDir = "$env:ProgramFiles\xMenuForge"
)

$ErrorActionPreference = "Stop"

# Require Administrator when installing to Program Files / registering AppXSvc
if ($Install) {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator
    )
    if (-not $isAdmin) {
        Write-Host "Elevation required for -Install. Relaunching as Administrator..." -ForegroundColor Yellow
        $argList = @("-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Configuration", $Configuration, "-Install", "-InstallDir", "`"$InstallDir`"")
        if ($EnableClassicMenu) { $argList += "-EnableClassicMenu" }
        if ($RestartExplorer) { $argList += "-RestartExplorer" }
        if ($BuildInstaller) { $argList += "-BuildInstaller" }
        $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $argList -Verb RunAs -Wait -PassThru
        exit $proc.ExitCode
    }
}

$RootDir = $PSScriptRoot
$NativeSrc = Join-Path $RootDir "src\native"
$NativeBuild = Join-Path $RootDir "build\native"
$SettingsProj = Join-Path $RootDir "src\settings\xMenuForge.Settings.csproj"
$StageDir = Join-Path $RootDir "dist\xMenuForge"

Write-Host "=== xMenuForge Build ($Configuration | x64) ===" -ForegroundColor Cyan

# 1. Verify prerequisites
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake.exe not found in PATH. Install CMake or Visual Studio C++ CMake tools."
}
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw "dotnet.exe not found in PATH. Install .NET 10 SDK."
}

# 2. Build Native C++20 Shell Extension & Host
Write-Host "[1/4] Building native C++20 shell extension and host..." -ForegroundColor Yellow
& cmake -S $NativeSrc -B $NativeBuild -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

& cmake --build $NativeBuild --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Native C++20 build failed." }

$NativeOut = Join-Path $NativeBuild $Configuration
$ShellDll = Join-Path $NativeOut "xMenuForge.Shell.dll"
$HostExe = Join-Path $NativeOut "xMenuForge.Host.exe"
if (-not (Test-Path $ShellDll)) { throw "Missing build artifact: $ShellDll" }
if (-not (Test-Path $HostExe)) { throw "Missing build artifact: $HostExe" }

# 3. Build WPF Settings Application
Write-Host "[2/4] Building WPF Settings application..." -ForegroundColor Yellow
$SettingsOut = Join-Path $RootDir "build\settings\$Configuration"
& dotnet publish $SettingsProj -c $Configuration -r win-x64 --no-self-contained -o $SettingsOut
if ($LASTEXITCODE -ne 0) { throw "WPF Settings publish failed." }

# 4. Stage Complete Release Package
Write-Host "[3/4] Staging distribution package at $StageDir..." -ForegroundColor Yellow
New-Item -Path $StageDir -ItemType Directory -Force | Out-Null
Get-ChildItem -Path $StageDir -Filter "*.old" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

function Copy-FileSafe([string]$Src, [string]$DestDir) {
    $dest = Join-Path $DestDir (Split-Path $Src -Leaf)
    try {
        Copy-Item -Path $Src -Destination $dest -Force -ErrorAction Stop
    } catch {
        $backup = "$dest.$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()).old"
        Move-Item -Path $dest -Destination $backup -Force
        Copy-Item -Path $Src -Destination $dest -Force
    }
}

Copy-FileSafe $ShellDll $StageDir
Copy-FileSafe $HostExe $StageDir
Copy-Item -Path (Join-Path $SettingsOut "*") -Destination $StageDir -Recurse -Force
Copy-Item -Path (Join-Path $RootDir "AppxManifest.xml") -Destination $StageDir -Force
Copy-Item -Path (Join-Path $RootDir "assets\xMenuForge.ico") -Destination $StageDir -Force
New-Item -Path (Join-Path $StageDir "Assets") -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $StageDir "Icons") -ItemType Directory -Force | Out-Null
Copy-Item -Path (Join-Path $RootDir "assets\Assets\*") -Destination (Join-Path $StageDir "Assets") -Recurse -Force
Copy-Item -Path (Join-Path $RootDir "assets\Icons\*") -Destination (Join-Path $StageDir "Icons") -Recurse -Force

Write-Host "Staged files ready: $StageDir" -ForegroundColor Green

# 5. Optional Inno Setup Installer
if ($BuildInstaller) {
    Write-Host "[4/4] Building Inno Setup installer..." -ForegroundColor Yellow
    $Iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
    if (-not $Iscc) {
        $Candidates = @(
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
        )
        foreach ($c in $Candidates) {
            if (Test-Path $c) { $Iscc = $c; break }
        }
    }
    if (-not $Iscc) { throw "Inno Setup 6 (ISCC.exe) not found." }
    & $Iscc (Join-Path $RootDir "installer.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }
    Write-Host "Installer created: $(Join-Path $RootDir 'dist\xMenuForge_Setup.exe')" -ForegroundColor Green
}

# 6. Optional Local Install & Explorer Registration
if ($Install) {
    Write-Host "[Install] Installing xMenuForge to $InstallDir..." -ForegroundColor Yellow

    # Stop running Settings/Host so files can be updated cleanly
    Get-Process -Name "xMenuForge.Settings", "xMenuForge.Host" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

    New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
    Get-ChildItem -Path $InstallDir -Filter "*.old" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    Copy-FileSafe $ShellDll $InstallDir
    Copy-FileSafe $HostExe $InstallDir
    Copy-Item -Path (Join-Path $SettingsOut "*") -Destination $InstallDir -Recurse -Force
    Copy-Item -Path (Join-Path $RootDir "AppxManifest.xml") -Destination $InstallDir -Force
    Copy-Item -Path (Join-Path $RootDir "assets\xMenuForge.ico") -Destination $InstallDir -Force
    New-Item -Path (Join-Path $InstallDir "Assets") -ItemType Directory -Force | Out-Null
    New-Item -Path (Join-Path $InstallDir "Icons") -ItemType Directory -Force | Out-Null
    Copy-Item -Path (Join-Path $RootDir "assets\Assets\*") -Destination (Join-Path $InstallDir "Assets") -Recurse -Force
    Copy-Item -Path (Join-Path $RootDir "assets\Icons\*") -Destination (Join-Path $InstallDir "Icons") -Recurse -Force

    $InstalledHost = Join-Path $InstallDir "xMenuForge.Host.exe"
    $regArgs = @("--register")
    if ($EnableClassicMenu) { $regArgs += "--classic" }
    if ($RestartExplorer) { $regArgs += "--restart-explorer" }

    Write-Host "[Install] Registering Explorer integration..." -ForegroundColor Yellow
    $proc = Start-Process -FilePath $InstalledHost -ArgumentList $regArgs -WorkingDirectory $InstallDir -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        throw "Registration failed with exit code $($proc.ExitCode). Check $env:LOCALAPPDATA\xMenuForge\logs\xmenuforge.log"
    }

    Write-Host "xMenuForge installed and registered at $InstallDir!" -ForegroundColor Green
}

Write-Host "=== Build Complete ===" -ForegroundColor Cyan
