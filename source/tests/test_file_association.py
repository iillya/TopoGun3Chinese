"""Exercise association installation/restoration in the test-only HKCU subtree."""
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import winreg

ROOT = Path(__file__).resolve().parents[2]
BASE = r'Software\TopoGunChinese\InstallerTests\Classes'
EXT = BASE + r'\.tgs'
PROG = 'TopoGunChinese.Project'


def value(key, name=''):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key) as handle:
            return winreg.QueryValueEx(handle, name)[0]
    except FileNotFoundError:
        return None


def write(key, text, name=''):
    assert key.startswith(BASE + '\\')
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as handle:
        winreg.SetValueEx(handle, name, 0, winreg.REG_SZ, text)


def clear_fixture():
    # Only known keys in this script's isolated namespace, never Software\Classes.
    for key in (EXT, BASE):
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key)
        except FileNotFoundError:
            pass


def check_arguments(command, launcher):
    shell = ctypes.WinDLL('shell32')
    shell.CommandLineToArgvW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
    kernel = ctypes.WinDLL('kernel32')
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    project = r'D:\项目 空格\烟雾.tgs'
    count = ctypes.c_int()
    argv = shell.CommandLineToArgvW(command.replace('%1', project), ctypes.byref(count))
    assert argv
    try:
        assert [argv[i] for i in range(count.value)] == [str(launcher), project]
    finally:
        kernel.LocalFree(argv)


def main():
    manifest = json.loads((ROOT / 'build/inno/test-manifest.json').read_text(encoding='utf-8'))
    assert manifest['test_mode']
    installer = Path(manifest['installer']).resolve()
    assert installer.is_relative_to(ROOT / 'build/inno/test-package')
    assert hashlib.sha256(installer.read_bytes()).hexdigest() == manifest['sha256']
    assert value(EXT) is None and value(BASE + '\\' + PROG) is None
    target = ROOT / 'build/inno/association-host'
    target.mkdir(parents=True, exist_ok=True)
    assert not (target / 'ChineseLauncher').exists()
    host = target / 'TopoGun.exe'
    shutil.copy2(Path(r'C:\Program Files\TopoGun 3\TopoGun.exe'), host)
    original_hash = hashlib.sha256(host.read_bytes()).hexdigest()
    for name in ('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'topogunscene.ico'):
        shutil.copy2(Path(r'C:\Program Files\TopoGun 3') / name, target / name)
    launcher = target / 'ChineseLauncher/TopoGunChineseLauncher.exe'

    def install(tasks='fileassoc'):
        subprocess.run([str(installer), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                        '/DIR=' + str(target), '/TASKS=' + tasks], check=True)

    def uninstall():
        subprocess.run([str(target / 'ChineseLauncher/.inno/unins000.exe'), '/VERYSILENT',
                        '/SUPPRESSMSGBOXES', '/NORESTART'], check=True)
        for _ in range(100):
            if not launcher.parent.exists():
                break
            time.sleep(0.1)
        assert not launcher.parent.exists()

    for case in ('absent', 'previous', 'later_change', 'opt_out'):
        if case != 'absent':
            write(EXT, 'Existing.TopoGunProject')
        write(EXT, 'keep this unrelated value', 'FixtureMarker')
        install()
        assert value(EXT) == PROG
        command_key = BASE + '\\' + PROG + r'\shell\open\command'
        check_arguments(value(command_key), launcher)
        assert value(BASE + '\\' + PROG + r'\DefaultIcon') == '"' + str(target / 'topogunscene.ico') + '",0'
        install()  # An upgrade must retain the original backup, not back up itself.
        assert value(EXT) == PROG
        if case == 'later_change':
            write(EXT, 'User.Selected.OtherApp')
        if case == 'opt_out':
            install('')
            assert value(EXT) == 'Existing.TopoGunProject'
        uninstall()
        expected = None if case == 'absent' else 'Existing.TopoGunProject'
        if case == 'later_change':
            expected = 'User.Selected.OtherApp'
        assert value(EXT) == expected, case
        assert value(EXT, 'FixtureMarker') == 'keep this unrelated value'
        assert value(command_key) is None
        assert hashlib.sha256(host.read_bytes()).hexdigest() == original_hash
        clear_fixture()
        print('PASS:', case)
    print('PASS: isolated association upgrade/restore/opt-out, user changes, Unicode/space argument quoting')


if __name__ == '__main__':
    main()
