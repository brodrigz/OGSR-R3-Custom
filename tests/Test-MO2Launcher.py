"""Check the real plugin launcher without requiring an MO2 process."""
import ast
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
from types import SimpleNamespace
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "release/mo2-plugin/game_radiophobia3.py"
tree = ast.parse(SOURCE.read_text(encoding="utf-8"))
game_class = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "Radiophobia3Game")
launcher = next(n for n in game_class.body if isinstance(n, ast.FunctionDef) and n.name == "executables")


class QtPath:
    def __init__(self, path):
        self.path = path

    def absoluteFilePath(self, relative):
        return str(Path(self.path) / relative)


class Executable:
    def __init__(self, title, binary):
        self.title, self.binary = title, binary.path

    def withArgument(self, command):
        self.command = command
        return self

    def withWorkingDirectory(self, directory):
        self.cwd = directory.path
        return self


environment = dict(
    mobase=SimpleNamespace(ExecutableInfo=Executable),
    QCoreApplication=SimpleNamespace(
        applicationFilePath=lambda: r"D:\MO2 with spaces\ModOrganizer.exe",
        applicationDirPath=lambda: r"D:\MO2 with spaces",
    ),
    QFileInfo=QtPath, QDir=QtPath, Path=Path, subprocess=subprocess,
)
exec(compile(ast.Module(body=[launcher], type_ignores=[]), str(SOURCE), "exec"), environment)


def split_windows(command):
    shell32 = ctypes.WinDLL("shell32", use_last_error=True)
    shell32.CommandLineToArgvW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    shell32.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    count = ctypes.c_int()
    argv = shell32.CommandLineToArgvW(command, ctypes.byref(count))
    if not argv:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return list(argv[:count.value])
    finally:
        kernel32.LocalFree(argv)


class LauncherTests(unittest.TestCase):
    def test_virtual_game_path_and_arguments(self):
        # Selecting the engine is left to USVFS at launch time; no cached mod path.
        for root in (r"D:\Radiophobia 3 - Clean backup", r"D:\R3", "D:\\Mód with spaces"):
            with self.subTest(root=root):
                game = SimpleNamespace(
                    gameDirectory=lambda: QtPath(root),
                    binaryName=lambda: r"bin_x64\xrEngine.exe",
                )
                entries = environment["executables"](game)
                self.assertEqual(len(entries), 1)
                entry = entries[0]
                engine = str(Path(root) / r"bin_x64\xrEngine.exe")
                self.assertEqual(entry.title, "Radiophobia 3")
                self.assertEqual(entry.binary, r"D:\MO2 with spaces\ModOrganizer.exe")
                self.assertEqual(entry.cwd, r"D:\MO2 with spaces")
                self.assertEqual(split_windows(entry.command), ["launch", str(Path(engine).parent), engine])
                self.assertEqual(
                    split_windows(entry.command + ' -dev -ltx "custom settings.ltx"'),
                    ["launch", str(Path(engine).parent), engine, "-dev", "-ltx", "custom settings.ltx"],
                )

    def test_no_python_process_wait_or_launch_interceptor(self):
        # Reintroducing a Python-owned launch/wait recreates the observed lock cycle.
        forbidden = {"onAboutToRun", "startApplication", "waitForApplication"}
        calls = {n.func.attr for n in ast.walk(game_class)
                 if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)}
        self.assertFalse(calls & forbidden)


if __name__ == "__main__":
    unittest.main()
