# Radiophobia 3 MO2 plugin

Requires MO2 2.5+ with `basic_games`. Close MO2 and copy
`game_radiophobia3.py` into `<MO2>/plugins/basic_games/games/`.

For an existing instance, reset the **Radiophobia 3** executable from the game
plugin in MO2's executable editor. MO2 preserves saved executable entries when
updating plugins, so replacing the Python file alone does not update the launcher.
Reapply any custom engine arguments after the generated `launch ...` arguments.

The executable uses MO2's own launcher to start `bin_x64/xrEngine.exe` inside the
virtual filesystem. Enabling an engine mod selects its executable; disabling it
selects the original installation. MO2 owns process tracking and the refresh after
exit. Profile saves and the mappings applied after Overwrite are retained.

Version 1.1.5 removes the Python launch callback. In 1.1.4 it called
`waitForApplication(handle, True)`, retaining Python's GIL while waiting for the
directory refresher, which itself needs Python. A captured freeze showed the
refresher waiting in `PyEval_AcquireThread` and the UI waiting for its mutex after
a mod checkbox click inside that nested refresh loop.

The launcher follows [MO2's own virtual executable handling](https://github.com/ModOrganizer2/modorganizer/blob/v2.5.2/src/processrunner.cpp).
Run `python tests/Test-MO2Launcher.py` from the repository to check the launch
configuration and Windows argument quoting without starting the game.
