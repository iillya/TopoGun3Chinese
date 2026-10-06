{ Use a private ProgID; never modify another application's command or UserChoice.
  Test builds use a private HKCU subtree, not the real file associations. }
function AssociationRoot: Integer;
begin
  Result := HKCU; { TopoGun registers its original .tgs default per user. }
end;

function AssociationBase: String;
begin
  #ifdef TestMode
  Result := 'Software\TopoGunChinese\InstallerTests\Classes\';
  #else
  Result := 'Software\Classes\';
  #endif
end;

function AssociationId: String;
begin
  Result := 'TopoGunChinese.Project';
end;

function AssociationCommand: String;
begin
  Result := '"' + InstallRoot + '\TopoGunChineseLauncher.exe" "%1"';
end;

procedure RestoreFileAssociation;
var
  Root: Integer;
  Base, Current, Previous, Command: String;
begin
  if GetIniString('FileAssociation', 'Managed', '', StateFile) <> '1' then Exit;
  Root := AssociationRoot;
  Base := AssociationBase;
  if RegQueryStringValue(Root, Base + '.tgs', '', Current) and
     (Current = AssociationId) then begin
    Previous := GetIniString('FileAssociation', 'Previous', '', StateFile);
    if GetIniString('FileAssociation', 'HadDefault', '0', StateFile) = '1' then begin
      if not RegWriteStringValue(Root, Base + '.tgs', '', Previous) then
        RaiseException('无法恢复 .tgs 原有文件关联。');
    end else begin
      if not RegDeleteValue(Root, Base + '.tgs', '') then
        RaiseException('无法移除 .tgs 中文启动关联。');
      RegDeleteKeyIfEmpty(Root, Base + '.tgs');
    end;
  end;
  { Preserve a later change made to our command by another installer/user. }
  if RegQueryStringValue(Root, Base + AssociationId + '\shell\open\command', '', Command) and
     (Command = AssociationCommand) then
    if not RegDeleteKeyIncludingSubkeys(Root, Base + AssociationId) then
      RaiseException('无法清理中文启动器的文件类型记录。');
  DeleteIniSection('FileAssociation', StateFile);
end;

procedure InstallFileAssociation;
var
  Root: Integer;
  Base, Current, HadDefault: String;
begin
  if not WizardIsTaskSelected('fileassoc') then begin
    RestoreFileAssociation;
    Exit;
  end;
  Root := AssociationRoot;
  Base := AssociationBase;
  Current := '';
  HadDefault := '0';
  if RegValueExists(Root, Base + '.tgs', '') then begin
    if not RegQueryStringValue(Root, Base + '.tgs', '', Current) then
      RaiseException('.tgs 关联格式异常，未修改原设置。');
    HadDefault := '1';
  end;
  { Upgrades retain the original backup; later user changes become the new backup. }
  if (GetIniString('FileAssociation', 'Managed', '', StateFile) <> '1') or
     (Current <> AssociationId) then begin
    if not SetIniString('FileAssociation', 'Previous', Current, StateFile) or
       not SetIniString('FileAssociation', 'HadDefault', HadDefault, StateFile) or
       not SetIniString('FileAssociation', 'Managed', '1', StateFile) then
      RaiseException('无法备份原文件关联，未修改原设置。');
  end;
  try
    if not RegWriteStringValue(Root, Base + AssociationId, '', 'TopoGun 工程') or
       not RegWriteStringValue(Root, Base + AssociationId + '\DefaultIcon', '',
         '"' + ExpandConstant('{app}\topogunscene.ico') + '",0') or
       not RegWriteStringValue(Root, Base + AssociationId + '\shell\open\command', '', AssociationCommand) or
       not RegWriteStringValue(Root, Base + '.tgs', '', AssociationId) then
      RaiseException('无法注册 .tgs 中文启动关联。');
  except
    RestoreFileAssociation;
    RaiseException('文件关联设置失败，已尝试恢复原设置；请查看安装日志。');
  end;
end;
