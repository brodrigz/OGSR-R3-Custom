# -*- encoding: utf-8 -*-
"""Mod Organizer 2 game plugin for S.T.A.L.K.E.R.: Radiophobia 3.

Install in <MO2>/plugins/basic_games/games/game_radiophobia3.py.
Requires MO2 2.5+ (PyQt6 and the basic_games feature registration API).
Based on the full plugin supplied by the user; supports both engine INI names
and the OGSR external-archive mods/ directory.
"""
from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

from PyQt6.QtCore import (
    QCoreApplication, QDateTime, QDir, QFileInfo, QStandardPaths, qInfo, qWarning,
)
import mobase
from ..basic_features import BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import BasicGameSaveGame, BasicGameSaveGameInfo
from ..basic_game import BasicGame


_GAMEDATA_SUBFOLDERS = (
    "ai", "anims", "config", "configs", "levels", "meshes", "particles",
    "scripts", "shaders", "sounds", "spawns", "textures",
)


class RadiophobiaModDataChecker(BasicModDataChecker):
    """Validate root layouts, wrap loose gamedata folders and unwrap archives."""

    def __init__(self) -> None:
        super().__init__(GlobPatterns(
            valid=[
                "gamedata", "bin_x64", "bin", "db", "game_archs",
                "mods",  # OGSR $mod_dir$: external .xsq/.xdb archives
                "_appdata_", "appdata", "fsgame.ltx", "meta.ini",
                "*.db*", "*.xdb*", "*.mohidden",
            ],
            move={folder: "gamedata/" for folder in _GAMEDATA_SUBFOLDERS},
            delete=[
                "*.txt", "*.md", "*.pdf", "*.url", "*.html", "*.jpg",
                "*.jpeg", "*.png", "*.webp", "readme*", "changelog*",
                "__MACOSX", "*.DS_Store",
            ],
        ))

    def _wrapper(self, filetree: mobase.IFileTree) -> mobase.IFileTree | None:
        entries = [e for e in filetree if not e.name().casefold().endswith(".mohidden")]
        if len(entries) != 1:
            return None
        entry = entries[0]
        if not isinstance(entry, mobase.IFileTree):
            return None
        children = {c.name().casefold() for c in entry}
        if children & {"gamedata", "bin_x64", "bin", "db", "game_archs", "mods"}:
            return entry
        if children & set(_GAMEDATA_SUBFOLDERS):
            return entry
        return None

    def dataLooksValid(self, filetree: mobase.IFileTree) -> mobase.ModDataChecker.CheckReturn:
        status = super().dataLooksValid(filetree)
        if status is mobase.ModDataChecker.INVALID and self._wrapper(filetree):
            return mobase.ModDataChecker.FIXABLE
        return status

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree | None:
        wrapper = self._wrapper(filetree)
        if wrapper is not None:
            filetree.merge(wrapper)
            wrapper.detach()
        return super().fix(filetree)


class RadiophobiaModDataContent(mobase.ModDataContent):
    CONFIG = 1
    SCRIPT = 2
    TEXTURE = 3
    MESH = 4
    SOUND = 5
    ANIM = 6
    SHADER = 7
    LEVEL = 8
    INTERFACE = 9
    ENGINE = 10
    ARCHIVE = 11

    _FOLDERS: dict[str, int] = {
        "config": CONFIG, "configs": CONFIG, "scripts": SCRIPT,
        "textures": TEXTURE, "meshes": MESH, "sounds": SOUND, "anims": ANIM,
        "shaders": SHADER, "levels": LEVEL, "spawns": LEVEL, "ai": LEVEL,
    }

    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [
            mobase.ModDataContent.Content(self.CONFIG, "Configs (.ltx/.xml)", ":/MO/gui/content/inifile"),
            mobase.ModDataContent.Content(self.SCRIPT, "Scripts (.script/.lua)", ":/MO/gui/content/script"),
            mobase.ModDataContent.Content(self.TEXTURE, "Textures", ":/MO/gui/content/texture"),
            mobase.ModDataContent.Content(self.MESH, "Meshes", ":/MO/gui/content/mesh"),
            mobase.ModDataContent.Content(self.SOUND, "Sounds", ":/MO/gui/content/sound"),
            mobase.ModDataContent.Content(self.ANIM, "Animations", ":/MO/gui/content/mesh"),
            mobase.ModDataContent.Content(self.SHADER, "Shaders", ":/MO/gui/content/texture"),
            mobase.ModDataContent.Content(self.LEVEL, "Levels / spawns / AI", ":/MO/gui/content/geometries"),
            mobase.ModDataContent.Content(self.INTERFACE, "UI", ":/MO/gui/content/interface"),
            mobase.ModDataContent.Content(self.ENGINE, "Engine files", ":/MO/gui/content/plugin"),
            mobase.ModDataContent.Content(self.ARCHIVE, "Packed archives (.db/.xdb/.xsq)", ":/MO/gui/content/bsa"),
        ]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        contents: set[int] = set()
        for entry in filetree:
            name = entry.name().casefold()
            if name in ("bin_x64", "bin") and entry.isDir():
                contents.add(self.ENGINE)
            elif name == "mods" and entry.isDir():
                contents.add(self.ARCHIVE)
            elif ".db" in name or ".xdb" in name:
                contents.add(self.ARCHIVE)
        gamedata = filetree.find("gamedata")
        trees = [t for t in (gamedata, filetree) if isinstance(t, mobase.IFileTree)]
        for tree in trees:
            for entry in tree:
                if not entry.isDir():
                    continue
                if (content := self._FOLDERS.get(entry.name().casefold())) is not None:
                    contents.add(content)
            if tree.find("config/ui") or tree.find("configs/ui") or tree.find("textures/ui"):
                contents.add(self.INTERFACE)
        return list(contents)


class RadiophobiaSaveGame(BasicGameSaveGame):
    def getName(self) -> str:
        name = self._filepath.stem
        if name.startswith("autosave"):
            return f"[Autosave] {name}"
        if name.startswith("quicksave"):
            return f"[Quicksave] {name}"
        return name

    def getCreationTime(self) -> QDateTime:
        info = QFileInfo(str(self._filepath))
        birth = info.birthTime()
        return birth if birth.isValid() else info.lastModified()

    def allFiles(self) -> list[str]:
        files = [str(self._filepath)]
        stem = self._filepath.stem
        for extra in (".dds", ".sav.bak", ".dds.bak", ".ltx"):
            candidate = self._filepath.parent / f"{stem}{extra}"
            if candidate.exists():
                files.append(str(candidate))
        return files


def _save_preview(save_path: Path) -> Path | None:
    preview = save_path.parent / f"{save_path.stem}.dds"
    return preview if preview.exists() else None


_COMPANION_SUFFIXES = {
    ".dds", ".bak", ".ltx", ".log", ".txt", ".png", ".jpg", ".jpeg", ".tmp",
}


def _contains_saves(folder: Path) -> bool:
    try:
        return any(f.is_file() and f.suffix.casefold() not in _COMPANION_SUFFIXES
                   for f in folder.iterdir())
    except OSError:
        return False


class RadiophobiaLocalSavegames(mobase.LocalSavegames):
    """Declare the standard profile-save mapping for MO2's save-game feature."""

    def __init__(self, game: "Radiophobia3Game") -> None:
        super().__init__()
        self._game = game

    def mappings(self, profile_save_dir: QDir) -> list[mobase.Mapping]:
        mapping = mobase.Mapping()
        mapping.source = profile_save_dir.absolutePath()
        mapping.destination = str(self._game._game_saves_directory())
        mapping.isDirectory = True
        mapping.createTarget = True
        return [mapping]

    def prepareProfile(self, profile: mobase.IProfile) -> bool:
        enabled = profile.localSavesEnabled()
        if enabled:
            try:
                Path(profile.absolutePath(), "saves").mkdir(parents=True, exist_ok=True)
            except OSError as exc:
                qWarning(f"[Radiophobia 3] cannot create profile saves folder: {exc}")
        return enabled


class Radiophobia3Game(BasicGame, mobase.IPluginFileMapper):
    Name = "Radiophobia 3 Support Plugin"
    Author = "Generated for MO2 basic_games"
    Version = "1.1.5"
    GameName = "S.T.A.L.K.E.R.: Radiophobia 3"
    GameShortName = "radiophobia3"
    GameBinary = r"bin_x64\xrEngine.exe"
    GameDataPath = ""
    GameDocumentsDirectory = r"%GAME_PATH%\_appdata_"
    GameSavesDirectory = r"%GAME_PATH%\_appdata_\savedgames"
    GameSaveExtension = "sav"
    # Each engine keeps its own profile settings; these files are not synchronized.
    GameIniFiles = ["user.ltx", "user_ogsr.ltx"]
    GameSupportURL = r"https://www.moddb.com/mods/radiophobia"

    _FSGAME_RE = re.compile(
        r"^\s*\$(?P<key>[a-z0-9_]+)\$\s*=\s*(?P<value>[^;\r\n]+)", re.IGNORECASE | re.M
    )

    def __init__(self) -> None:
        BasicGame.__init__(self)
        # The native mobase interfaces require each base to be initialized.
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        if not super().init(organizer):
            return False
        self._register_feature(RadiophobiaModDataChecker())
        self._register_feature(RadiophobiaModDataContent())
        self._local_savegames = RadiophobiaLocalSavegames(self)
        self._register_feature(self._local_savegames)
        self._register_feature(BasicGameSaveGameInfo(_save_preview))
        return True

    def mappings(self) -> list[mobase.Mapping]:
        """Reapply profile saves after MO2 has mapped its Overwrite directory.

        With GameDataPath="", Overwrite maps the whole game root recursively.
        MO2 applies LocalSavegames before Overwrite, so an existing savedgames
        directory in Overwrite can replace the profile's creation target.
        IPluginFileMapper runs last, restoring the profile as the write target.
        Existing saves in Overwrite are not moved or copied automatically.
        """
        profile = self._organizer.profile()
        if profile is None or not profile.localSavesEnabled():
            return []
        save_dir = Path(profile.absolutePath()) / "saves"
        save_dir.mkdir(parents=True, exist_ok=True)
        mappings = self._local_savegames.mappings(QDir(str(save_dir)))
        qInfo(
            f"[Radiophobia 3] profile save write target: {save_dir} "
            f"(game path: {self._game_saves_directory()})"
        )
        return mappings

    def _fsgame_entries(self) -> dict[str, tuple[str, str]]:
        entries: dict[str, tuple[str, str]] = {}
        fsgame = Path(self.gameDirectory().absolutePath()) / "fsgame.ltx"
        try:
            text = fsgame.read_text(errors="ignore")
        except OSError:
            return entries
        for match in self._FSGAME_RE.finditer(text):
            parts = [p.strip() for p in match.group("value").split("|")]
            aliases = [p for p in parts if p.startswith("$")]
            alias = aliases[-1].strip("$").casefold() if aliases else ""
            last = parts[-1] if parts else ""
            relative = "" if last.startswith("$") else last.strip("\\/ ")
            entries[match.group("key").casefold()] = (alias, relative)
        return entries

    def _resolve_fs_path(self, key: str, _depth: int = 0) -> Path | None:
        game_path = Path(self.gameDirectory().absolutePath())
        key = key.strip("$").casefold()
        if key in ("fs_root", ""):
            return game_path
        if _depth > 8:
            return None
        entry = self._fsgame_entries().get(key)
        if entry is None:
            return None
        alias, relative = entry
        base = self._resolve_fs_path(alias, _depth + 1) if alias else game_path
        if base is None:
            return None
        return base / relative if relative else base

    def _appdata_directory(self) -> Path:
        game_path = Path(self.gameDirectory().absolutePath())
        setting = self._organizer.pluginSetting(self.name(), "appdata_folder")
        if isinstance(setting, str) and setting.strip():
            return game_path / setting.strip()
        return self._resolve_fs_path("app_data_root") or game_path / "_appdata_"

    def documentsDirectory(self) -> QDir:
        return QDir(str(self._appdata_directory()))

    def _game_saves_directory(self) -> Path:
        override = self._organizer.pluginSetting(self.name(), "saves_directory")
        if isinstance(override, str) and override.strip():
            path = Path(override.strip())
            if not path.is_absolute():
                path = Path(self.gameDirectory().absolutePath()) / path
            return path
        declared = self._resolve_fs_path("game_saves")
        return declared if declared is not None else self._appdata_directory() / "savedgames"

    def _overwrite_saves_candidates(self) -> list[Path]:
        try:
            overwrite = Path(self._organizer.overwritePath())
        except (AttributeError, OSError):
            return []
        relative = self._game_saves_directory()
        game_path = Path(self.gameDirectory().absolutePath())
        candidates = [overwrite / "_appdata_" / "savedgames", overwrite / "savedgames"]
        try:
            candidates.insert(0, overwrite / relative.relative_to(game_path))
        except ValueError:
            pass
        return candidates

    def _saves_candidates(self) -> list[Path]:
        game_path = Path(self.gameDirectory().absolutePath())
        candidates = [
            self._game_saves_directory(), *self._overwrite_saves_candidates(),
            self._appdata_directory() / "savedgames", game_path / "_appdata_" / "savedgames",
            game_path / "appdata" / "savedgames", game_path / "savedgames",
        ]
        documents = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.DocumentsLocation)
        if documents:
            candidates.append(Path(documents) / "STALKER-SHOC" / "savedgames")
        seen: set[str] = set()
        unique: list[Path] = []
        for candidate in candidates:
            key = str(candidate).casefold()
            if key not in seen:
                seen.add(key)
                unique.append(candidate)
        return unique

    def savesDirectory(self) -> QDir:
        override = self._organizer.pluginSetting(self.name(), "saves_directory")
        if isinstance(override, str) and override.strip():
            return QDir(str(self._game_saves_directory()))
        candidates = self._saves_candidates()
        for candidate in candidates:
            if candidate.is_dir() and _contains_saves(candidate):
                self._log_saves_directory(candidate)
                if candidate in self._overwrite_saves_candidates():
                    qWarning(
                        "[Radiophobia 3] saves were found in MO2's Overwrite folder. "
                        "The game writes into the data directory, so usvfs redirects "
                        "them there. Enable profile-specific save games and move them "
                        f"to <profile>\\saves, or they will stay in {candidate}."
                    )
                return QDir(str(candidate))
        for candidate in candidates:
            if candidate.is_dir():
                self._log_saves_directory(candidate)
                return QDir(str(candidate))
        fallback = self._game_saves_directory()
        self._log_saves_directory(fallback)
        return QDir(str(fallback))

    def _is_profile_path(self, path: Path) -> bool:
        try:
            profiles = Path(self._organizer.basePath(), "profiles").resolve()
            return profiles in path.resolve().parents
        except (OSError, ValueError, AttributeError):
            return "profiles" in {p.casefold() for p in path.parts}

    def _log_saves_directory(self, path: Path) -> None:
        if getattr(self, "_logged_saves_dir", None) != str(path):
            self._logged_saves_dir = str(path)
            qInfo(f"[Radiophobia 3] using save folder: {path}")

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        path = Path(folder.absolutePath())
        if not path.is_dir():
            if self._is_profile_path(path):
                try:
                    path.mkdir(parents=True, exist_ok=True)
                    qInfo(
                        f"[Radiophobia 3] created empty profile saves folder: {path} -- "
                        f"local savegames is enabled, so it starts empty. Copy saves "
                        f"from {self._appdata_directory() / 'savedgames'}, or disable "
                        f"local savegames for the profile."
                    )
                except OSError as exc:
                    qWarning(f"[Radiophobia 3] cannot create {path}: {exc}")
            else:
                qWarning(f"[Radiophobia 3] listSaves: folder does not exist: {path}")
            return []
        try:
            files = [f for f in sorted(path.iterdir()) if f.is_file()]
        except OSError as exc:
            qWarning(f"[Radiophobia 3] listSaves: cannot read {path}: {exc}")
            return []
        saves = [f for f in files if f.suffix.casefold() not in _COMPANION_SUFFIXES]
        extensions = sorted({f.suffix.casefold() or "<none>" for f in files})
        qInfo(
            f"[Radiophobia 3] listSaves: {path} -> {len(files)} file(s), "
            f"{len(saves)} save(s); extensions present: {', '.join(extensions)}"
        )
        return [RadiophobiaSaveGame(f) for f in saves]

    def initializeProfile(self, path: QDir, settings: mobase.ProfileSetting) -> None:
        if settings & mobase.ProfileSetting.SAVEGAMES:
            try:
                Path(path.absolutePath(), "saves").mkdir(parents=True, exist_ok=True)
            except OSError as exc:
                qWarning(f"[Radiophobia 3] cannot create profile saves folder: {exc}")
        if settings & mobase.ProfileSetting.CONFIGURATION:
            for ini in self.iniFiles():
                source = Path(self.documentsDirectory().absoluteFilePath(ini))
                target = Path(path.absoluteFilePath(Path(ini).name))
                if target.exists():
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                if source.exists():
                    shutil.copyfile(source, target)
                else:
                    target.touch()

    def executables(self) -> list[mobase.ExecutableInfo]:
        # Start the engine after USVFS injection so an enabled engine mod can
        # override the EXE. Let MO2's native runner own the process and refresh:
        # waitForApplication(..., True) in a Python callback holds the GIL while
        # the directory refresher needs it, deadlocking MO2 after the game exits.
        engine = self.gameDirectory().absoluteFilePath(self.binaryName())
        command = subprocess.list2cmdline([
            "launch", str(Path(engine).parent), engine,
        ])
        return [mobase.ExecutableInfo(
            "Radiophobia 3", QFileInfo(QCoreApplication.applicationFilePath()),
        ).withArgument(command).withWorkingDirectory(
            QDir(QCoreApplication.applicationDirPath()),
        )]

    def settings(self) -> list[mobase.PluginSetting]:
        return super().settings() + [
            mobase.PluginSetting(
                "appdata_folder",
                "Name of the appdata folder inside the game directory. Leave empty to "
                "read it from fsgame.ltx (default: _appdata_).", "",
            ),
            mobase.PluginSetting(
                "saves_directory",
                "Full path to the savedgames folder. Leave empty to detect it "
                "automatically. Overrides appdata_folder when set.", "",
            ),
        ]
