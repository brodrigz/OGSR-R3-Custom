# Radiophobia 3 feature parity audit

Audit date: 2026-09-15. Reference: `D:\Radiophobia 3 - Clean backup`.

## Implementation update: 2026-09-16

Findings 1, 2, 3, and 5 below have now been addressed in source and selected release
resources. Their original evidence is retained as audit history. The new laser
controller is adapted from Folopes RP3 QoL v5.2; it replaces `zzz_bas_laser_control`
and uses the engine's saved laser flag. The restored alternate-aim callback reaches
the original fake-lens and dedicated-scope NV consumers. HUD Options again exposes
font width/height. Default NVG/laser bindings are N/Mouse 5.

The animation helper and duplicate animation namespace remain unchanged, per the
user's instruction. This update does not establish full private-engine parity.
See [LASER-AND-SIGHT-FIXES.md](LASER-AND-SIGHT-FIXES.md) for implementation and checks.

## Original audit findings

**Full behavioral equivalence is not established.** Static comparison found
missing original laser integration, disconnected alternate-aim notifications,
and missing font controls. A follow-up on 2026-09-16 also confirmed an incorrect
default NVG binding. The review found an animation-helper registration risk.
The packaging checks alone cannot certify Radiophobia feature parity.

This audit added tools and documentation; it did not change runtime resources
or engine code, build the engine, or launch either installation. Earlier resource
and packaging changes remain in the working tree.

## Reference and method

The clean installation's executable matches the private-engine executable used
by the earlier PDB investigation, allowing that investigation to inform this
review. Historical "deferred" labels were checked against current implementation,
not treated as current findings.

| Reference file | SHA-256 |
| --- | --- |
| `bin_x64/xrEngine.exe` | `18AA9CCD12CDA3A9F79E71D85417A7FE975816B11B48BB7A409D68C0CB953578` |
| `bin_x64/xrEngine.pdb` | `C4906165D1EB0897585472CBEBDC5AE4345CDD052AF05550AB43E10DDA59B381` |
| `gamedata.sq_base` | `C826BAB47CCD511C2AB6CEBF731EC0CB03A144C1B7EAE65678C214DAEFDDAC54` |

The base archive was freshly extracted inside this repository. The reference's
existing `_extracted` directory was not used. The other four content archives
contained no script/config entries. Effective content was assembled in this order:
base archive, clean loose files, then the selected fork release payload from
`Get-RadiophobiaPayload.ps1`. This matters because inspecting the Game directory
alone misses inherited scripts and inspecting only the clean files misses overrides.

The resulting script/config inventory contains 2,525 files: 2,474 inherited
unchanged, 30 overridden, and 21 added. It includes 701 Lua scripts and 1,822
LTX/XML files. The unconditional add-on registry has 76 entries versus 71 in
the reference; conditional registrations are not included in those counts.

## Findings requiring work

### 1. Original weapon laser integration is disabled

The reference enables `zzz_bas_laser_control`; the fork comments it out in
`gamedata/scripts/ogse/ogse_signals_addons_list.script` because
`weapon:get_laser_on()` is absent. The reference module queries that API and drives
the shader-based laser through `shader_param_5`, including switch effects.

Modern OGSR's native laser path does not establish compatibility with this content:
`Weapon.cpp` enables it through `laser_light_section`, while 52 clean weapon
definitions use `laser_status = true`. Only two `laser_light_section` definitions
were found, in the tactical-addon configuration. The laser key remains available.

Restore a compatible state API and the original script integration, or explicitly
adapt the original weapon configurations and shader behavior to a verified
replacement. Test several original laser-equipped weapons, toggling, holstering,
weapon switching, and save/load. Preserve modern native laser support.

### 2. Alternate-aim switching does not notify the original fake-lens script

Native switching is implemented in `Weapon.cpp`, including the optional
`CWeapon_OnSwitchSightMode` hook. No implementation of that hook exists in the
effective scripts. The original `on_actor_weapon_alt_aim_switch` callback is not
exported, and its registration in the fork's `bind/bind_stalker.script` is commented.

The inherited `fakelens.script` still subscribes to that original signal. Its
handler clears/restores the fake scope when switching while aiming and displays
the enabled/disabled notification outside aiming. That notification path is
disconnected. A stale fake lens while switching in ADS is a predicted symptom,
not an observed runtime result.

Bridge the new hook to the existing signal with the expected boolean argument.
Preserve the fork's per-weapon alternate-sight persistence. Validate both directions
of switching with fake lenses enabled, including switching while already aiming.

### 3. Font width/height controls are missing from Options

Clean R3 exposes `g_font_scale_x` and `g_font_scale_y` through its options XML and
gameplay script. The current options no longer expose them. Both console commands
remain registered in `ogsr_engine/xr_3da/xr_ioc_cmd.cpp`.

This is a UI regression, not missing engine functionality. Restore the two controls
within the current options layout and verify application and persistence.

### 4. New animation helper has an unresolved registration contract

The fork registers `ogsr_hud_animation_callbacks` as an OGSE add-on, but the module
defines no `attach`. `ogse_signals.subscribe_module` requires that function.
Additionally, the module's callback definitions stay in its Lua namespace, whereas
`HudItem.cpp` requests `_G.CHudItem__PlayHUDMotion` and
`_G.CHudItem__OnAnimationEnd`.

The isolated LuaJIT probe reproduces the missing `attach` and namespace mismatch.
However, clean `_g.script` already supplies the global play callback. The original
play-animation path must not be described as entirely absent. Two original active
modules (`ui_inv_descr` and `uni_anim_ammo`) also lack local `attach` definitions;
full initialization could involve behavior outside the isolated probe.

Treat this as a registration/integration risk, not a proven startup crash. Verify
the full initialization path, remove redundant registration or provide a proper
adapter, and test scripted animation sounds and animation-end consumers.

### 5. Default NVG key targets the wrong action (2026-09-16 follow-up)

The selected `gamedata/config/default_controls.ltx:39` binds `night_vision kN`.
Clean R3 binds `night_vision_rad kN`. These are distinct actions: the engine's
native `kNIGHT_VISION` and the custom `kNIGHT_VISION_RAD` registered by inherited
`system.ltx`. The wearable NVG script rejects actions other than
`kNIGHT_VISION_RAD`; the options UI also edits `night_vision_rad`.

Consequently, applying the packaged default controls does not send N to the R3
wearable NVG handler. Existing user bindings can mask the defect. The appropriate
binding is `bind night_vision_rad kN`. This is a confirmed input-path defect, not
proof that every NVG configuration or shader is broken. The render pass,
`SSFX_BEEFS_NVG`, shader constants, and wearable controller are present; visual
correctness remains untested. The audit now checks this mismatch explicitly.

## Maintenance and inherited findings

- The fork ships `scripts/animation_common.script` while the reference supplies
  `scripts/animations/animation_common.script`. Both define the same basename-based
  Lua namespace. Their current contents are identical, so this is a shadowing and
  maintenance risk rather than a demonstrated feature difference. Keep one
  authoritative path matching the inherited archive path.
- LuaJIT syntax checks found five failing scripts, all byte-identical inherited
  reference files: `gulag/gulag_selo.script`, `mob/mob_alife_control.script`,
  `lua_help.script`, `copy of _test.script`, and `test/test_ini.script`.
  Their runtime reachability was not established. No selected fork script produced
  a syntax failure.
- `rad_quick_nade` is disabled in both installations. Its obsolete `switch_state`
  calls are not evidence of a newly missing enabled feature.
- Simple literal searches can misclassify dynamic console exports: `pnv_color`
  is registered through the custom shader-export mechanism and was not counted
  as missing. Retired renderer commands are not automatically reasons to restore
  old renderer code.

## Reimplemented features found in current source

These checks establish implementation presence and relevant integration, not
complete runtime equivalence with the private binary.

| Feature | Current evidence |
| --- | --- |
| Static and skeletal wallmarks | `script_wallmarks_script.cpp`: placement overloads, TTL, randomized rotation, manager export |
| HUD animation speed aliases | `player_hud.cpp`: three-field motion aliases and speed handling |
| Layered weapon/explosion sounds | `HudSound.cpp`, `Explosive.cpp`, `WeaponBM16.cpp`, `WeaponShotgun.cpp`, `WeaponMagazined.cpp`: layered playback and actor/last-shot/misfire selection |
| Animated fire-mode changes | `WeaponMagazined.cpp` and `WeaponMagazinedWGrenade.cpp`: switch animations |
| Difficulty and condition controls | `console_commands.cpp`, `EntityCondition.cpp`, `game_difficulties.script`: hit power, dispersion, degradation, power-loss bias |
| Detector actor API | `script_actor.cpp`: the two private detector wrappers checked by the API audit |
| Footstep signal integration | `step_manager.cpp` and binder: material information and three-argument callback |
| R3 scope XML behavior | `xr_3da/x_ray.cpp`: CoP-style scope texture handling enabled by default |
| Alternate sights and weapon inspection | `Weapon.cpp`: switching and bore/inspection actions; notification caveat above |
| Additional input actions | `xr_level_controller.cpp`: slot 13, alternate sights, bore |
| Original saved gameplay preferences | All 11 original `rad3_*` IDs remain in the current gameplay/HUD options script |

The targeted callback audit found no stale direct references against 76 exported
callbacks. The input audit checked 69 actions across the effective LTX/XML content
without registry/binding findings. That check accepted both valid NVG action names
and missed their semantic mismatch; the follow-up above corrects that omission.
These checks also do not detect deliberately removed
registrations; the disconnected alternate-aim callback illustrates that limit.

The private object-API scan produced three actual calls to missing bindings:
two belong to the already-disabled quick-grenade module and one to the laser
module disabled by the fork. Other symbol differences were not automatically
classified as script-facing feature losses.

The fork's additional features and renderer upgrades should be retained. In
particular, FSR2-to-FSR3 replacement is an intentional upgrade, not missing parity.
Nothing in these findings requires reverting to the older OGSR engine.

## Limits of the private-engine reconstruction check

The historical PDB/source-checksum audit identified 90 modified engine files
relative to official OGSR 3.490 (94 with third-party files) and three R3-only
wallmark source files. The current review compared those 90 paths against OGSR
3.548: 66 differ and 24 remain identical to upstream 3.548.

Neither count proves behavioral parity or missing functionality. Upstream may
already implement or supersede an old modification; conversely, a changed file
can still miss a private behavior. The PDB does not recover complete C++ bodies.
Unresolved areas include some actor/NPC animation and IK behavior, physics/sound,
AI update behavior, and inventory/trade/dialog details. The path-by-path inventory
is saved locally as `.r3-feature-audit/custom-engine-review.csv`.

Consequently this review cannot certify that **all** private code has been
re-created. Closing that claim requires targeted binary analysis and paired runtime
scenarios, particularly for the unresolved areas. No gameplay, visual, audio,
save-compatibility, or performance equivalence was measured in this audit. The
currently compiled fork executable was not certified against the source tree.

## Reproducing the static checks

After extracting the clean `gamedata.sq_base` to a directory containing `scripts`
and `config`, run from the repository root:

```powershell
./tests/Audit-RadiophobiaFeatures.ps1 `
    -CleanRoot 'D:\Radiophobia 3 - Clean backup' `
    -ExtractedGamedataRoot './release/.r3-feature-audit/base'

./ogsr_engine/LuaJIT/bin/x64/Lua_JIT.exe `
    ./tests/Probe-RadiophobiaAnimationBridge.lua `
    ./Game/Resources_SoC_1.0006/gamedata/scripts/ogsr_hud_animation_callbacks.script
```

The first tool writes `effective-content.csv`, `findings.csv`, and `summary.json`
under `release/.r3-feature-audit/report`. `-FailOnFindings` returns failure when
findings exist. This is a targeted regex-based static audit, not a full Lua parser
or a comprehensive feature detector. The isolated animation probe currently exits
nonzero by design when it reproduces the registration problem.

Other local evidence includes `actor-api.csv`, `object-api.csv`, `callbacks.csv`,
and `input-actions.csv`, produced with the previously reviewed audit tools under
`D:\Radiophobia 3 - current\migration\tools`. Those tools and the historical PDB
reports are external provenance; the new repository audit does not require them.
Generated reports and extracted proprietary/reference content remain ignored.
