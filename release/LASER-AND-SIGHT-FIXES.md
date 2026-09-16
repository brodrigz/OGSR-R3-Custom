# Laser, alternate-sight, and font compatibility fixes

Implemented 2026-09-16 for the current Game resource tree and release publisher.

## Behavior

- Original `laser_status = true` weapons use the engine's existing serialized
  laser addon flag. Their toggle is immediate, with the Folopes switch sound and
  camera effect supplied by the script. No new save fields are introduced.
- Weapons with `laser_light_section` retain OGSR's native light and device-switch
  behavior. That explicit configuration takes precedence when both keys exist.
- Lua exposes `get_laser_on()`, `has_shader_laser()`, and `has_native_laser()`.
  Queries safely return false for non-weapons (except that the laser-state query
  reads the saved flag for weapons).
- Folopes' laser dot reacts to surface distance, disappears against sky, hides
  during normal ADS, and remains visible during alternate aiming. Its NVG boost
  also recognizes the fork's dedicated-scope NV visual mode.
- The controller reads the current weapon's `is_alt_aim()` instead of retaining
  a global sight-state guess. Switching weapons and restoring a save therefore
  use the fork's existing per-weapon alternate-sight state. Shader parameters are
  cleared on holstering, non-weapon selection, death, and teardown.
- The restored `callback.on_actor_weapon_alt_aim_switch(bool)` is appended to the
  callback enum to preserve existing IDs. The binder forwards it to OGSE signals,
  reconnecting original fake lenses and dedicated-scope NV. The newer optional
  `CWeapon_OnSwitchSightMode` hook is retained.
- HUD Options exposes font width/height sliders (0.5–2.0, step 0.01). They use
  the existing apply/cancel/save options group and return to 1.0 on HUD defaults.
- Default controls restore `night_vision_rad` on N and `laser_on` on Mouse 5.
  Existing user bindings are not rewritten; rebind those actions or reset controls
  if an existing installation has the previous incorrect defaults.

## Folopes source and renderer adaptation

Reference: `D:\Mods\Radiophobia3-Folopes-QoL-Fixes-v5.2\Core (all displays)`.
Adapted `rad_laser_control.script`, `models_laser.ps`, `models_laserbeam.s`, and
the laser-beacon portion of `ogsr_nightvision.ps`. Original controller credits:
HarukaSai, LVutner, Ishmaeel, and seaz5150; QoL laser changes: Folopes. Existing
BEEF night-vision shader credits are retained.

The copied laser shader was adapted to retain native laser rendering and to use
reciprocal pixel `SV_Position.w` correctly for fragment depth. Its deferred-depth
sampling uses the current engine helpers.

The current tone-mapping pass clamps the NVG scene before the final NVG shader,
which would erase Folopes' brightness marker. While night vision is active,
the renderer snapshots the scene after forward rendering/distortion and before
tone mapping. It reuses the existing reflection scratch target after water and SSR
have finished reading it; no additional full-size target is allocated. The NVG
shader samples that HDR snapshot with normalized UVs, independently of output
resolution. The beacon bypass is enabled only for a visible legacy laser.

The brightness marker is Folopes' heuristic, not a dedicated laser mask: an
unusually bright scene pixel can also cross its threshold while a legacy laser
is enabled. Visual tuning, especially with temporal upscaling, needs in-game checks.

## Verification and release

Results: Release/x64 build passed with zero errors (compiler warnings remain);
both changed pixel shaders compiled successfully; all 27 Lua integration
assertions and the UI/selection regression checks passed. The final ZIP checksum,
all 659 manifest hashes, and its selected Game resource bytes were verified.
The static audit now reports only the four retained animation/inherited findings.

Validation artifact (local, ignored):
`release/validation-final/radiophobia-ogsr-3.548-engine-upgrade-laser-sight-font-fixes-20260916.zip`.
It includes the rebuilt engine and records that the source working tree is dirty.

- Release/x64 engine build with Visual Studio 2022.
- Direct FXC Shader Model 5 compilation of the changed laser/NVG shaders.
- `tests/Test-RadiophobiaLaser.lua`: controller transitions, native-path isolation,
  weapon switches, restored state, NVG changes, cleanup, and dedicated-scope NV.
  Passing an extracted clean gamedata directory also tests the real inherited
  fake-lens consumer in both alternate-sight directions.
- `tests/Test-RadiophobiaRelease.ps1`: selected payload, options/script contracts,
  bilingual labels, and regression checks including a broken font binding.
- `tests/Audit-RadiophobiaFeatures.ps1`: resolved audit findings and explicitly
  retained animation-helper/inherited findings.

The publisher now watches gameplay/common-AI/core C++ sources as well as renderer
sources for stale binaries, requires the laser controller and NVG texture binding,
and verifies default laser/NVG actions. New resources are included through the
normal Game manifest; there is no separate compatibility overlay.

No reference installation was modified or launched. Animation-helper code and
registration remain untouched. An in-game route is still required: test legacy
and native laser weapons, nearby walls/distant surfaces/sky, hip fire/ADS/alternate
aim, wearable and scope NV, save/load, native resolution and DLSS/FSR, plus font
Apply/Cancel/Defaults. Compilation and isolated tests cannot establish visual or
complete gameplay parity.
