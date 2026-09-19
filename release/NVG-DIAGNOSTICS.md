# NVG blackout investigation

The 2026-09-19 00:30:59 game log confirms that the D2 renderer binding,
`ogsr_nightvision_diagnostic_d2` pixel shader, and instrumented gameplay script
loaded. Activation selected `effector_nightvision_bad` with mode 1, gain 1,
two tubes, radius 0.58, and color (0.66, 1, 0.45). The user still reported a
black image, including the diagnostic shader's fixed cyan borders.

The 00:45:02 log adds an E1 GPU capture: after NVG, the original-effect tile
contains RGB (0.152466, 0.231079, 0.104004), and the border is (0, 1, 1).
Both survive the rain stage. All thirteen samples become (0, 0, 0) at
`after-combine2`; the final postprocess preserves that black output. The
blackout effect's color/brightness parameters are neutral in this capture.

The postprocess scene remapper called `CTexture::bind` directly without updating
`CBackend::textures_ps` or invalidating its cached texture-list pointer. After
NVG flips the scene targets, the combine pass can skip restoring its declared
texture because that stale cache says it is already bound. The actual input
then aliases the new output target. `override_PS_texture` now synchronizes the
driver binding, per-slot cache and texture-list invalidation.

`tests/Test-PostprocessTextureRemap.ps1 -Baseline` reproduces the incorrect
binding with the old remapping function. Without `-Baseline`, it tests the fix,
repeated ping-pong, preservation of unrelated textures, restoration of the
same shader's original bindings, and explicit unbinding. The test compiles
the actual production cache/remapping methods against texture/driver doubles.
Live confirmation still requires rebuilding and installing the corrected engine.

## Engine capture

Rebuild the engine before using this command. The existing RC8 ZIP does not
contain the engine instrumentation. Repackage and install the rebuilt engine;
keep D2 enabled with highest priority for the first capture.

1. Load the save and activate NVG.
2. Wait for the activation animation to finish.
3. Enter `r_pnv_debug 1` in the console and close the console.
4. Inspect `[NVDBG E1]` entries in the game log.

The command captures one rendered frame and resets itself to zero. It logs
the actual mode, postprocess color/brightness parameters, texture dimensions
and formats, and thirteen GPU pixel samples at each of these stages:

- Before NVG.
- After NVG (or `nvg-skipped` if mode is not 1).
- After rain droplets.
- After the final combine shader.
- After the final postprocess into the backbuffer, before HUD rendering.

Samples 1–12 are the centers of D2's numbered tiles. Sample 13 is the upper-left
pixel, where D2 draws a constant cyan border. The same locations are used at
every stage. The reported RGBA values come from a staging-texture readback;
they are not reconstructed from the script's requested settings.

The readback briefly synchronizes with the GPU. It runs only when explicitly
requested and does not replace textures or modify the effect. An unknown
`r_pnv_debug` command means the active executable predates this instrumentation.

## Shader diagnostic package

`tests/New-NightVisionDiagnostic.ps1` creates the D2 MO2 ZIP from current
resources. `tests/Test-NightVisionDiagnostic.lua` checks logging with
Radiophobia's disabled `printf` behavior and checks the distinct shader binding.
Gameplay diagnostics must use `log1`; the separate shader VM uses native `log`.
