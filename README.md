# OGSR R3 Custom

This fork carries Radiophobia 3 1.20 behavior onto an upgraded OGSR engine and
adds its own engine and gameplay improvements. The supported content target is
Radiophobia, not vanilla Shadow of Chernobyl.

- Engine implementation: `ogsr_engine/`.
- Maintained runtime content: [Game/Resources_SoC_1.0006](Game/Resources_SoC_1.0006/README.md).
  The inherited folder name is retained; its selected resources target Radiophobia.
- Feature contracts: [R3_CUSTOMIZATION.md](R3_CUSTOMIZATION.md).
- Packaging and verification: [release/README.md](release/README.md).
- Clean-install parity findings: [release/R3-FEATURE-AUDIT.md](release/R3-FEATURE-AUDIT.md).
- Upstream background and engine build instructions: [.github/README.md](.github/README.md).

Build `Engine.sln` for Release/x64 after obtaining the dependencies described in
the build instructions. Publish an upgrade using `release/Publish-RadiophobiaDropIn.ps1`
with `-OutputDirectory` and a new `-Version`. The ZIP is installed over a backed-up
clean Radiophobia 3 1.20 installation, which supplies the base game archives.

For content changes, edit the Game resource tree. All runtime shaders ship;
other files must be listed in `radiophobia-release-files.txt` in that tree.
The publisher validates the selected payload and does not copy unrelated legacy
resources. Run `tests/Test-RadiophobiaRelease.ps1` to check the UI and packaging
selection, optionally passing `-ArchivePath` to verify a generated release.

Release sources and tools belong in Git; generated ZIPs, binaries, staging
directories, and local migration backups remain ignored. A separate compatibility
overlay under `release/` is no longer needed.
