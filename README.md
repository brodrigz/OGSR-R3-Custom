# Radiophobia Unofficial Patch

Radiophobia Unofficial Patch combines bug fixes, gameplay and UI improvements,
and an updated OGSR engine for Radiophobia 3 1.20. The supported content target
is Radiophobia, not vanilla Shadow of Chernobyl.

- Engine implementation: `ogsr_engine/`.
- Maintained runtime content: [Game/Resources_SoC_1.0006](Game/Resources_SoC_1.0006/README.md).
  The inherited folder name is retained; its selected resources target Radiophobia.
- Feature contracts: [R3_CUSTOMIZATION.md](R3_CUSTOMIZATION.md).
- Packaging and verification: [release/README.md](release/README.md).
- Upstream background and engine build instructions: [.github/README.md](.github/README.md).

Build `Engine.sln` for Release/x64 after obtaining the dependencies described in
the build instructions. Publish the patch using `release/Publish-RadiophobiaDropIn.ps1`
with `-OutputDirectory` and a new `-Version`. The ZIP is installed over a backed-up
clean Radiophobia 3 1.20 installation, which supplies the base game archives.

For content changes, edit the Game resource tree. All runtime shaders ship;
other files must be listed in `radiophobia-release-files.txt` in that tree.
The publisher validates the selected payload and does not copy unrelated legacy
resources. Run `tests/Test-RadiophobiaRelease.ps1` to check the UI and packaging
selection, optionally passing `-ArchivePath` to verify a generated release.

Release sources and tools belong in Git; generated ZIPs, binaries, and staging
directories remain ignored.
