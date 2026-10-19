; Inno Setup is the sole supported installer. Runtime payload is allowlisted.
#ifndef PayloadInclude
  #error Build using source\build_inno.bat
#endif
#define ProductId "TopoGunChinese.Inno"
#define ProductName "TopoGun 中文补丁"
#ifdef TestMode
  #define ProductId "TopoGunChinese.Inno.IsolatedTests"
  #define ProductName "TopoGun Inno Isolated Tests"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#PackageVersion}
VersionInfoVersion={#PackageVersion}
VersionInfoProductVersion={#PackageVersion}
AppPublisher=神说要凑数
AppPublisherURL=https://space.bilibili.com/281243426
DefaultDirName={autopf}\TopoGun 3
AppendDefaultDirName=no
DisableDirPage=no
DirExistsWarning=no
DisableProgramGroupPage=yes
UsePreviousTasks=no
UninstallFilesDir={app}\ChineseLauncher\.inno
UninstallDisplayIcon={app}\ChineseLauncher\topogun.ico
OutputDir={#PackageOutput}
OutputBaseFilename=TopoGunChineseInstaller
SetupIconFile=..\..\icon\topogun.ico
SetupArchitecture=x64
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
WizardStyle=modern light windows11
WizardSizePercent=110
WizardImageFile=
WizardSmallImageFile=
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
ChangesAssociations=yes
SetupLogging=yes
UninstallLogging=no
#ifdef TestMode
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[LangOptions]
DialogFontName=Microsoft YaHei UI
DialogFontSize=10

[Messages]
SelectDirLabel3=请选择包含 TopoGun.exe 的软件目录。补丁只写入其下的 ChineseLauncher 文件夹。
FinishedLabel=中文补丁已安装。可通过“TopoGun 中文版”快捷方式启动；勾选文件关联后，也可双击 .tgs 工程文件打开。Windows 已手动指定的默认应用需要在“打开方式”中更改。%n%n卸载会恢复仍由本补丁管理的原文件关联，并完整删除 ChineseLauncher 文件夹及其中的全部文件。

[Tasks]
Name: "desktopicon"; Description: "创建公共桌面快捷方式"
Name: "startmenuicon"; Description: "创建开始菜单快捷方式（所有用户）"
Name: "fileassoc"; Description: "使用 TopoGun 中文版打开 .tgs 工程文件（当前用户）"


[Files]
Source: "{#SupportDll}"; DestDir: "{app}\ChineseLauncher\.inno"; DestName: "support.dll"; Flags: ignoreversion
#include PayloadInclude

[Icons]
#ifndef TestMode
Name: "{autodesktop}\TopoGun 中文版"; Filename: "{app}\ChineseLauncher\TopoGunChineseLauncher.exe"; WorkingDir: "{app}"; IconFilename: "{app}\ChineseLauncher\topogun.ico"; IconIndex: 0; Tasks: desktopicon
Name: "{autoprograms}\TopoGun 中文版"; Filename: "{app}\ChineseLauncher\TopoGunChineseLauncher.exe"; WorkingDir: "{app}"; IconFilename: "{app}\ChineseLauncher\topogun.ico"; IconIndex: 0; Tasks: startmenuicon
#endif

[UninstallDelete]
Type: filesandordirs; Name: "{app}\ChineseLauncher"


[Code]
var
  PayloadNames: TArrayOfString;

function CheckTargetEx(Directory: String; CheckVersion, CheckRunning: Boolean; Message: String; Capacity: Cardinal): Boolean;
external 'CheckTargetEx@files:support.dll stdcall setuponly';
function QueryInstallProcesses(Directory: String): Integer;
external 'QueryInstallProcesses@files:support.dll stdcall setuponly';
function StopInstallProcesses(Directory: String; var Stopped: Cardinal; Message: String; Capacity: Cardinal): Boolean;
external 'StopInstallProcesses@files:support.dll stdcall setuponly';

function CheckTarget(Directory: String; CheckVersion: Boolean; Message: String; Capacity: Cardinal): Boolean;
external 'CheckTarget@files:support.dll stdcall setuponly';
function CheckTargetUninstall(Directory: String; CheckVersion: Boolean; Message: String; Capacity: Cardinal): Boolean;
external 'CheckTarget@{app}\ChineseLauncher\.inno\support.dll stdcall uninstallonly delayload';
procedure InitPayload;
begin
  { Generated from the same explicit payload allowlist as the native packer. }
  #include PayloadCode
end;

function InstallRoot: String;
begin
  Result := AddBackslash(ExpandConstant('{app}')) + 'ChineseLauncher';
end;

function StateFile: String;
begin
  Result := InstallRoot + '\.inno\install-state.ini';
end;

#include "association.iss"

function BufferText(Buffer: String): String;
var
  Terminator: Integer;
begin
  Terminator := Pos(#0, Buffer);
  if Terminator > 0 then Result := Copy(Buffer, 1, Terminator - 1)
  else Result := Buffer;
end;

#include "install_policy.iss"

function ValidateDirectory(Directory: String): String;
var
  Message: String;
begin
  SetLength(Message, 1024);
  if CheckTargetEx(Directory, True, False, Message, 1024) then Result := ''
  else Result := BufferText(Message);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Error: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then Exit;
  Error := ValidateDirectory(WizardDirValue);
  Result := Error = '';
  if not Result then SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  PreviousDir, ExistingOwner: String;
  RegistryRoot: Integer;
begin
  InstallWarnings := '';
  AssociationReady := True;
  LegacyReady := True;
  Result := ValidateDirectory(ExpandConstant('{app}'));
  if Result <> '' then Exit;
  ExistingOwner := GetIniString('Install', 'Owner', '', StateFile);
  if (ExistingOwner <> '') and (ExistingOwner <> '{#ProductId}') and
       (Pos('TopoGunChinese.Inno', ExistingOwner) <> 1) then begin
    Result := 'ChineseLauncher 的安装记录属于另一安装器，已停止覆盖。';
    Exit;
  end;
  RegistryRoot := HKLM64;
  #ifdef TestMode
  RegistryRoot := HKCU;
  #endif
  if RegQueryStringValue(RegistryRoot,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#ProductId}_is1',
    'InstallLocation', PreviousDir) and
    (CompareText(RemoveBackslashUnlessRoot(PreviousDir), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) <> 0) then begin
    Result := '当前安装器一次只管理一个 TopoGun 目录。请先卸载旧目录中的中文补丁，再更换安装位置。';
    Exit;
  end;

  Result := EnsureInstallStopped(ExpandConstant('{app}'));
end;

procedure BackupExistingPayload;
var
  I, Suffix: Integer;
  Source, Target, Base, Backup: String;
begin
  Backup := '';
  for I := 0 to GetArrayLength(PayloadNames) - 1 do begin
    Source := InstallRoot + '\' + PayloadNames[I];
    if FileExists(Source) then begin
      if Backup = '' then begin
        Base := InstallRoot + '\.inno\backups\' + GetDateTimeString('yyyymmdd-hhnnss', '-', ':');
        Backup := Base;
        Suffix := 0;
        while DirExists(Backup) do begin
          Suffix := Suffix + 1;
          Backup := Base + '-' + IntToStr(Suffix);
        end;
      end;
      Target := Backup + '\' + PayloadNames[I];
      if not ForceDirectories(ExtractFileDir(Target)) or not CopyFile(Source, Target, True) or
        (GetSHA256OfFile(Source) <> GetSHA256OfFile(Target)) then
        RaiseException('无法完成升级前备份，未开始替换汉化文件：' + Source);
    end;
  end;
  if (Backup <> '') and FileExists(StateFile) then
    if not CopyFile(StateFile, Backup + '\install-state.ini', True) then
      RaiseException('无法备份旧安装记录，未开始替换汉化文件。');
  if Backup <> '' then Log('Pre-install recovery snapshot: ' + Backup);
end;

#include "cleanup.iss"

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then begin
    if EnsureInstallStopped(ExpandConstant('{app}')) <> '' then
      RaiseException('尚未安全关闭目标软件，未开始替换汉化文件。');
    #ifndef TestMode
    if FileExists(ExpandConstant('{userdesktop}\TopoGun 中文版.lnk')) and
      not DeleteFile(ExpandConstant('{userdesktop}\TopoGun 中文版.lnk')) then
      AddInstallWarning('无法清理旧版当前用户桌面快捷方式，请关闭占用程序后重试。');
    if FileExists(ExpandConstant('{userprograms}\TopoGun 中文版.lnk')) and
      not DeleteFile(ExpandConstant('{userprograms}\TopoGun 中文版.lnk')) then
      AddInstallWarning('无法清理旧版当前用户开始菜单快捷方式，请关闭占用程序后重试。');
    #endif
    BackupExistingPayload;
  end;
  if CurStep = ssPostInstall then begin
    DeleteIniSection('Hashes', StateFile);
    if not SetIniString('Install', 'Owner', '{#ProductId}', StateFile) then
      RaiseException('文件已安装，但安装记录无法保存；请保留日志并重新运行安装器。');
      #ifndef TestMode
      { Remove alternate uninstall records from other installer IDs for the
        same product so they cannot later delete this shared ChineseLauncher. }
      if CompareText('{#ProductId}', 'TopoGunChinese.Inno') <> 0 then
        RegDeleteKeyIncludingSubkeys(HKLM64,
          'Software\Microsoft\Windows\CurrentVersion\Uninstall\TopoGunChinese.Inno_is1');
      RegDeleteKeyIncludingSubkeys(HKLM64,
        'Software\Microsoft\Windows\CurrentVersion\Uninstall\TopoGunChinese.Inno.Experimental_is1');
      #endif
    try
      CleanupInstallerBackups;
    except
      AddInstallWarning(GetExceptionMessage);
    end;
    if WizardIsTaskSelected('fileassoc') then InstallFileAssociation
    else RestoreFileAssociation;
    FinishInstallPolicy;
  end;
end;

procedure InitializeWizard;
begin
  WizardForm.WelcomeLabel1.Alignment := taLeftJustify;
  WizardForm.WelcomeLabel2.Alignment := taLeftJustify;
  WizardForm.FinishedHeadingLabel.Alignment := taLeftJustify;
  WizardForm.FinishedLabel.Alignment := taLeftJustify;
  WizardForm.WelcomeLabel1.Left := ScaleX(24);
  WizardForm.WelcomeLabel1.Width := WizardForm.WelcomePage.ClientWidth - ScaleX(48);
  WizardForm.WelcomeLabel2.Left := ScaleX(24);
  WizardForm.WelcomeLabel2.Width := WizardForm.WelcomePage.ClientWidth - ScaleX(48);
  WizardForm.FinishedHeadingLabel.Left := ScaleX(24);
  WizardForm.FinishedHeadingLabel.Width := WizardForm.FinishedPage.ClientWidth - ScaleX(48);
  WizardForm.FinishedLabel.Left := ScaleX(24);
  WizardForm.FinishedLabel.Width := WizardForm.FinishedPage.ClientWidth - ScaleX(48);
  InitPayload;
end;

function CheckUninstallTarget: Boolean;
var
  Message: String;
begin
  SetLength(Message, 1024);
  Result := CheckTargetUninstall(ExpandConstant('{app}'), False, Message, 1024);
  if not Result then SuppressibleMsgBox(BufferText(Message), mbError, MB_OK, IDOK);
end;

function InitializeUninstall: Boolean;
begin
  InitPayload;
  try
    Result := CheckUninstallTarget;
  finally
    UnloadDLL(InstallRoot + '\.inno\support.dll');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    try
      if not CheckUninstallTarget then RaiseException('请先正常关闭 TopoGun 后重试。');
    finally
      UnloadDLL(InstallRoot + '\.inno\support.dll');
    end;

    RestoreFileAssociation;
    CleanupInstallerBackups;
  end;
end;
