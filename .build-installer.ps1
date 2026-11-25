<#
.SYNOPSIS
    Builds the standalone Inno Setup 6 installer (xMenuForge-Setup.exe) for xMenuForge.

.DESCRIPTION
    Packages E:\xMenuForge\dist\xMenuForge into a standalone Windows installer
    (xMenuForge-Setup.exe) in the project folder, complete with Windows 11 Explorer
    shell extension registration (--register / --unregister), Start Menu shortcut,
    code signing (if xCONFLiCTiONx cert is present), and unins000.exe uninstaller.

.PARAMETER RebuildApp
    Runs .\.build.ps1 first to compile and stage the latest Release binaries before packaging.

.PARAMETER Install
    Silently runs the generated installer after compilation to install and register xMenuForge.

.PARAMETER OutputDir
    Directory where xMenuForge-Setup.exe will be placed (defaults to the project folder).

.PARAMETER IsccPath
    Path to Inno Setup 6 compiler (ISCC.exe).
#>
[CmdletBinding()]
param(
    [switch]$RebuildApp,
    [switch]$Install,
    [string]$OutputDir = "",
    [string]$IsccPath  = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
)

Clear-Host
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot      = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = $repoRoot
}

$folderName    = "xMenuForge"
$appName       = "xMenuForge"
$installerBase = "xMenuForge-Setup"
$exeName       = "xMenuForge.Settings.exe"
$hostExeName   = "xMenuForge.Host.exe"
$shellDllName  = "xMenuForge.Shell.dll"
$version       = "1.0.0.0"
$appId         = "7A4E9C10-5B2D-4F81-A310-9C6E2B410001"
$description   = "Windows 11 Context Menu & Shell Extension Manager"
$sourceDir     = Join-Path $repoRoot "dist\xMenuForge"
$repoIcon      = Join-Path $repoRoot "assets\xMenuForge.ico"

# Locate Inno Setup 6 compiler
if (-not (Test-Path $IsccPath)) {
    $isccCmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($isccCmd) {
        $IsccPath = $isccCmd.Source
    } elseif (Test-Path "$env:ProgramFiles\Inno Setup 6\ISCC.exe") {
        $IsccPath = "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    } else {
        throw "Inno Setup 6 compiler (ISCC.exe) not found at: $IsccPath"
    }
}

# Build Release artifacts if requested or missing
$requiredFiles = @(
    (Join-Path $sourceDir $exeName),
    (Join-Path $sourceDir $hostExeName),
    (Join-Path $sourceDir $shellDllName)
)
$missingBinary = $requiredFiles | Where-Object { -not (Test-Path $_) }

if ($RebuildApp -or $missingBinary) {
    $buildScript = Join-Path $repoRoot ".build.ps1"
    if (-not (Test-Path $buildScript)) {
        throw "Build script not found: $buildScript"
    }
    Write-Host "==> Running .build.ps1 for $appName (Release)..." -ForegroundColor Cyan
    & $buildScript -Configuration Release
    if ($LASTEXITCODE -ne 0) {
        throw ".build.ps1 failed with exit code $LASTEXITCODE"
    }
}

foreach ($req in $requiredFiles) {
    if (-not (Test-Path $req)) {
        throw "Required staged Release binary not found: $req. Run .\.build.ps1 first."
    }
}

$destIcon = Join-Path $sourceDir "xMenuForge.ico"
if ((-not (Test-Path $destIcon)) -and (Test-Path $repoIcon)) {
    Copy-Item -Path $repoIcon -Destination $destIcon -Force
}

$setupIconLine = if (Test-Path $destIcon) { "SetupIconFile=`"$destIcon`"" } else { "" }

# Sign staged binaries before packaging if xCONFLiCTiONx code-signing certificate is available
$signCert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -match 'CN=xCONFLiCTiONx' } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
$signTool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1

if ($signCert -and $signTool) {
    foreach ($bin in $requiredFiles) {
        if ((Test-Path $bin) -and ((Get-AuthenticodeSignature $bin).Status -ne 'Valid')) {
            Write-Host "==> Signing $(Split-Path $bin -Leaf) with xCONFLiCTiONx ($($signCert.Thumbprint))..." -ForegroundColor Cyan
            & $signTool.FullName sign /sha1 $signCert.Thumbprint /fd SHA256 /d "$appName" /du "https://xconflictionx.cc" /tr "http://timestamp.digicert.com" /td SHA256 $bin 2>$null | Out-Null
            if ($LASTEXITCODE -ne 0) {
                & $signTool.FullName sign /sha1 $signCert.Thumbprint /fd SHA256 /d "$appName" /du "https://xconflictionx.cc" $bin 2>$null | Out-Null
            }
        }
    }
}

$tempBuildDir = Join-Path $env:TEMP "xPrograms-InstallerBuild\$folderName"
$stageOutDir  = Join-Path $tempBuildDir "out"
New-Item -ItemType Directory -Path $stageOutDir -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$issLines = @(
    "; Generated Inno Setup 6 Script for $appName",
    "#define AppName        `"$appName`"",
    "#define AppFolderName  `"$folderName`"",
    "#define AppVersion     `"$version`"",
    "#define AppPublisher   `"xCONFLiCTiONx`"",
    "#define AppURL         `"https://xconflictionx.cc`"",
    "#define AppContact     `"info@xconflictionx.cc`"",
    "#define AppExeName       `"$exeName`"",
    "#define AppHostExeName   `"$hostExeName`"",
    "#define AppShellDllName  `"$shellDllName`"",
    "#define AppId          `"$appId`"",
    "#define AppDescription `"$description`"",
    "#define SourcePath     `"$sourceDir`"",
    "#define OutputPath     `"$stageOutDir`"",
    "",
    "[Setup]",
    "AppId={{{#AppId}}",
    "AppName={#AppName}",
    "AppVersion={#AppVersion}",
    "AppVerName={#AppName} {#AppVersion}",
    "AppPublisher={#AppPublisher}",
    "AppPublisherURL={#AppURL}",
    "AppSupportURL={#AppURL}",
    "AppUpdatesURL={#AppURL}",
    "AppContact={#AppContact}",
    "AppComments={#AppDescription}",
    "DefaultDirName={autopf}\{#AppFolderName}",
    "DefaultGroupName={#AppName}",
    "DisableProgramGroupPage=yes",
    "PrivilegesRequired=admin",
    "ArchitecturesAllowed=x64compatible",
    "ArchitecturesInstallIn64BitMode=x64compatible",
    "WizardStyle=modern",
    $setupIconLine,
    "UninstallDisplayIcon={app}\xMenuForge.ico",
    "UninstallDisplayName={#AppName}",
    "OutputDir={#OutputPath}",
    "OutputBaseFilename=$installerBase",
    "Compression=lzma2",
    "SolidCompression=yes",
    "CloseApplications=no",
    "RestartApplications=no",
    "VersionInfoVersion={#AppVersion}",
    "VersionInfoDescription={#AppDescription}",
    "",
    "[Languages]",
    "Name: `"english`"; MessagesFile: `"compiler:Default.isl`"",
    "",
    "[Tasks]",
    "Name: `"desktopicon`"; Description: `"{cm:CreateDesktopIcon}`"; GroupDescription: `"{cm:AdditionalIcons}`"; Flags: unchecked",
    "",
    "[Files]",
    "Source: `"{#SourcePath}\{#AppShellDllName}`"; DestDir: `"{app}`"; Flags: ignoreversion restartreplace uninsrestartdelete",
    "Source: `"{#SourcePath}\*`"; DestDir: `"{app}`"; Excludes: `"unins*.*,*.pdb,*.old,{#AppShellDllName}`"; Flags: ignoreversion recursesubdirs createallsubdirs",
    "",
    "[Icons]",
    "Name: `"{group}\{#AppName} Settings`"; Filename: `"{app}\{#AppExeName}`"; WorkingDir: `"{app}`"; IconFilename: `"{app}\xMenuForge.ico`"; Comment: `"{#AppDescription}`"",
    "Name: `"{autodesktop}\{#AppName} Settings`"; Filename: `"{app}\{#AppExeName}`"; WorkingDir: `"{app}`"; IconFilename: `"{app}\xMenuForge.ico`"; Comment: `"{#AppDescription}`"; Tasks: desktopicon",
    "",
    "[Run]",
    "Filename: `"{app}\{#AppHostExeName}`"; Parameters: `"--register`"; StatusMsg: `"Registering Windows 11 Explorer Integration...`"; Flags: runhidden waituntilterminated",
    "Filename: `"{app}\{#AppExeName}`"; Description: `"Launch {#AppName} Settings`"; Flags: nowait postinstall skipifsilent shellexec",
    "",
    "[UninstallRun]",
    "Filename: `"taskkill.exe`"; Parameters: `"/F /IM {#AppExeName}`"; Flags: runhidden; RunOnceId: `"Kill_{#AppFolderName}Settings`"",
    "Filename: `"taskkill.exe`"; Parameters: `"/F /IM {#AppHostExeName}`"; Flags: runhidden; RunOnceId: `"Kill_{#AppFolderName}Host`"",
    "Filename: `"{app}\{#AppHostExeName}`"; Parameters: `"--unregister`"; RunOnceId: `"UnregisterXMenuForge`"; StatusMsg: `"Unregistering Explorer Integration...`"; Flags: runhidden waituntilterminated",
    "",
    "[Code]",
    "procedure CleanupOldRenamedFiles(const AppDir: String);",
    "var",
    "  FindRec: TFindRec;",
    "begin",
    "  if FindFirst(AddBackslash(AppDir) + '*.old', FindRec) then",
    "  begin",
    "    try",
    "      repeat",
    "        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then",
    "          DeleteFile(AddBackslash(AppDir) + FindRec.Name);",
    "      until not FindNext(FindRec);",
    "    finally",
    "      FindClose(FindRec);",
    "    end;",
    "  end;",
    "end;",
    "",
    "procedure MoveLockedFileAside(const FilePath: String);",
    "var",
    "  BackupPath: String;",
    "  Suffix: Integer;",
    "begin",
    "  if not FileExists(FilePath) then",
    "    Exit;",
    "  if DeleteFile(FilePath) then",
    "    Exit;",
    "  Suffix := 0;",
    "  repeat",
    "    BackupPath := FilePath + '.' + IntToStr(Suffix) + '.old';",
    "    Inc(Suffix);",
    "  until (not FileExists(BackupPath)) or (Suffix > 100);",
    "  if RenameFile(FilePath, BackupPath) then",
    "    RestartReplace(BackupPath, '');",
    "end;",
    "",
    "function PrepareToInstall(var NeedsRestart: Boolean): String;",
    "var",
    "  ResultCode: Integer;",
    "  AppDir: String;",
    "begin",
    "  Result := '';",
    "  Exec('taskkill.exe', '/F /IM `"{#AppExeName}`"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);",
    "  Exec('taskkill.exe', '/F /IM `"{#AppHostExeName}`"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);",
    "  Sleep(300);",
    "  AppDir := ExpandConstant('{app}');",
    "  if DirExists(AppDir) then",
    "  begin",
    "    CleanupOldRenamedFiles(AppDir);",
    "    MoveLockedFileAside(AddBackslash(AppDir) + '{#AppShellDllName}');",
    "    MoveLockedFileAside(AddBackslash(AppDir) + '{#AppHostExeName}');",
    "  end;",
    "end;"
)

$issFile = Join-Path $tempBuildDir "$folderName.iss"
Set-Content -Path $issFile -Value ($issLines -join "`r`n") -Encoding UTF8

Write-Host "==> Compiling $installerBase.exe (v$version)..." -ForegroundColor Cyan
$compiled = $false
$stagedExe = Join-Path $stageOutDir "$installerBase.exe"
for ($attempt = 1; $attempt -le 4; $attempt++) {
    if (Test-Path $stagedExe) { Remove-Item $stagedExe -Force -ErrorAction SilentlyContinue }
    & $IsccPath /Q $issFile
    if ($LASTEXITCODE -eq 0 -and (Test-Path $stagedExe)) {
        $compiled = $true
        break
    }
    Write-Host "    [Retry $attempt] Waiting briefly for file handle release..." -ForegroundColor Yellow
    Start-Sleep -Seconds 2
}
if (-not $compiled) {
    throw "ISCC failed to compile $issFile"
}

$finalExe = Join-Path $OutputDir "$installerBase.exe"
Copy-Item -Path $stagedExe -Destination $finalExe -Force

if ($signCert -and $signTool) {
    Write-Host "==> Signing $finalExe with xCONFLiCTiONx ($($signCert.Thumbprint))..." -ForegroundColor Cyan
    & $signTool.FullName sign /sha1 $signCert.Thumbprint /fd SHA256 /d "$appName Setup" /du "https://xconflictionx.cc" /tr "http://timestamp.digicert.com" /td SHA256 $finalExe | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $signTool.FullName sign /sha1 $signCert.Thumbprint /fd SHA256 /d "$appName Setup" /du "https://xconflictionx.cc" $finalExe | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
    }
    Write-Host "[OK] Signed installer: $finalExe" -ForegroundColor Green
}

if (-not (Test-Path $finalExe)) {
    throw "Expected installer artifact not found: $finalExe"
}

$sizeMB = [math]::Round((Get-Item $finalExe).Length / 1MB, 2)
Write-Host "[OK] Installer created at: $finalExe ($sizeMB MB)" -ForegroundColor Green

if ($Install) {
    Write-Host "==> Running silent install for $appName..." -ForegroundColor Yellow
    Get-Process -Name "xMenuForge.Settings", "xMenuForge.Host" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400

    $p = Start-Process -FilePath $finalExe -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-" -Verb RunAs -Wait -PassThru
    if ($p.ExitCode -ne 0) {
        throw "Silent install failed with exit code $($p.ExitCode)"
    }
    Write-Host "[OK] Installed & registered $appName v$version" -ForegroundColor Green
}
