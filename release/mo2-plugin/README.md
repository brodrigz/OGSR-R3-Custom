# MO2 setup for Radiophobia Unofficial Patch

Requires MO2 2.5+ with `basic_games`. Close MO2 and copy
`game_radiophobia3.py` into `<MO2>/plugins/basic_games/games/`.

For an existing instance, reset the **Radiophobia 3** executable from the game
plugin in MO2's executable editor. MO2 preserves saved executable entries when
updating plugins, so replacing the Python file alone does not update the launcher.
Reapply any custom engine arguments after the generated `launch ...` arguments.

The executable uses MO2's own launcher to start `bin_x64/xrEngine.exe` inside the
virtual filesystem. Enabling Radiophobia Unofficial Patch selects its executable; disabling it
selects the original installation. MO2 owns process tracking and the refresh after
exit. Profile saves and the mappings applied after Overwrite are retained.

Using MO2's launcher avoids blocking its directory refresh in a Python callback
when switching the patch after exiting the game.

The launcher follows [MO2's own virtual executable handling](https://github.com/ModOrganizer2/modorganizer/blob/v2.5.2/src/processrunner.cpp).
Run `python tests/Test-MO2Launcher.py` from the repository to check the launch
configuration and Windows argument quoting without starting the game.
