# Radiophobia Unofficial Patch: engine and content integration

Radiophobia Unofficial Patch maintains Radiophobia 3 version 1.20 behavior on an updated OGSR engine, with additional bug fixes, gameplay and UI improvements. Reusable capability belongs in the engine; feature policy belongs in game scripts and configuration.

The maintained Radiophobia resources live in `Game/Resources_SoC_1.0006`.
Despite its inherited name, this is the fork's R3 target, not a pristine SoC
resource tree. See its README and `radiophobia-release-files.txt` for package
selection. The release publisher copies these resources directly.

## Integration rule

- Engine code should expose generic operations, data formats, callbacks, or lifecycle primitives.
- Game scripts and configuration should decide which feature uses those operations.
- Do not name or bootstrap an unrelated content module from engine code.
- Register script features through the game's add-on registry and keep attachment idempotent when duplicate protection is useful.

Examples of reusable surfaces in this fork include script wallmark placement, layered HUD sounds, actor-owned script cameras, world-to-UI projection, HUD item override animations, and detector visibility controls.

## Forgiving Reload Interruption

Set `complete_late_reload_on_hide = true` in `[features]` to let an actor's ordinary magazine reload finish when the weapon is hidden during its last fifteen percent. This is intended for quick item-use animations that temporarily hide the active weapon. It is player-only, disabled by default, applies only when the resolved hands reload has no usable commit mark, and deliberately excludes tri-state shell reloads.

The generic saved console command `g_complete_late_reload_on_hide on|off` controls the same feature bit at runtime. Radiophobia's bundled Gameplay-options checkbox uses it, so its change applies immediately and is saved in the user's configuration. Content that does not include that UI can still choose the startup default through `[features]`.

Builds compiled with `DEBUG` retain opt-in reload tracing. Start such an engine with `-trace_reload` to log actor reload requests, resolved motion marks, animation completion, tri-state cartridge commits, and late-hide decisions. Release builds contain no reload diagnostic logging.

## Zoom on ADS

The saved `g_auto_aim_zoom on|off` command optionally applies the same 0.75
final-FOV multiplier as the free-zoom action while the actor aims through iron,
reflex, or holographic sights. The default classifier rejects 2D scope textures,
active 3D scope viewports, and effective zoom factors above 1.10. A weapon or
active attachable-optic section can explicitly set `auto_aim_zoom = true|false`
to override that default. Radiophobia's bundled Gameplay-options checkbox
controls and persists the feature without requiring a new key binding.

## Input behavior

Controls > Input Behavior selects hold or toggle behavior for each action without
requiring separate key bindings. The saved console commands accept
`st_input_hold` or `st_input_toggle`:

| Action | Command |
| --- | --- |
| Aim | `g_aim_input_mode` |
| Sprint | `g_sprint_input_mode` |
| Lean | `g_lean_input_mode` |
| Crouch | `g_crouch_input_mode` |
| Walk | `g_walk_input_mode` |
| Low crouch | `g_low_crouch_input_mode` |

When lean uses toggle behavior, pressing the active direction returns the actor
upright and pressing the opposite direction switches sides. Freelook and lower
weapon retain their existing controls.

## Script-resolution diagnostics

Debug builds support an opt-in script resolver trace. Start a Debug engine with:

```text
-trace_script_resolve
```

The trace records:

- every discovered script namespace and whether it won insertion;
- namespace lookup results;
- the physical file or external archive used when a script is opened.

The implementation is compiled only when `DEBUG` is defined and is disabled unless the command-line switch is present. Release builds contain no resolver logging or mod-specific script-name list.

## Legacy scope textures

`cop_style_scope_texture` defaults to `true`, so legacy `scope_texture` names are resolved through `config/ui/scopes.xml`. A build that requires direct texture lookup can explicitly set `cop_style_scope_texture = false` in `[features]`.

## Scope night-vision metadata

An optic section may declare an opaque `scope_nightvision = <profile>` value. The resolved scope value is cached whenever weapon zoom parameters are initialized, including attach, detach, and replacement paths. Lua can read it through `has_scope_nightvision()` and `get_scope_nightvision()`, and can query `is_3dss_enabled()` without duplicating engine scope resolution.

The engine does not interpret the profile or render an effect. Game configuration and scripts own profile meanings, NV renderer selection, wearable-NV priority, and cleanup policy.

## Lifecycle-safe time-factor query

`level.get_time_factor()` is safe while the single-player server is creating
ALife, before the client game state exists. It uses the normal client clock
during gameplay, falls back to the active server/ALife clock during bootstrap,
and returns `1.0` only when no game or ALife clock exists. This keeps the Lua
API generic while allowing modules with top-level initialization to survive
the Lua-VM recreation introduced by current OGSR.

## Lasers and alternate sights

Weapons with `laser_status = true` use the engine's serialized laser addon flag
and the Folopes-derived shader/controller. An explicit `laser_light_section`
takes precedence and selects OGSR's native light and device-switch behavior.
Lua exposes `get_laser_on()`, `has_shader_laser()`, and `has_native_laser()`.

`rad_laser_control.script` reads the current weapon's `is_alt_aim()` and clears
shader parameters on holstering, non-weapon selection, death, and teardown.
The shader dot scales with surface distance, disappears against the sky, hides
during normal ADS, and stays visible during alternate aiming.

The NVG beacon uses a scene snapshot taken after forward rendering/distortion
and before tone mapping, only when a visible legacy laser needs it. This reuses
the reflection scratch target after water and SSR, with normalized UV sampling.
The beacon's brightness threshold can also pick up unusually bright scene pixels;
appearance should be checked in-game with native resolution and upscaling.

`callback.on_actor_weapon_alt_aim_switch(bool)` forwards sight changes through
the binder to OGSE signals for fake lenses and dedicated-scope NV. Its enum entry
is appended to preserve existing callback IDs. The optional
`CWeapon_OnSwitchSightMode` hook is also supported.

The default bindings are `night_vision_rad` on N and `laser_on` on Mouse 5.
Existing user bindings are retained; reset controls or rebind these actions when
upgrading an installation with older defaults.

The laser controller credits HarukaSai, LVutner, Ishmaeel, and seaz5150, with
QoL changes by Folopes. The night-vision shader retains its BEEF credits.

## HUD font controls

HUD Options provides font width and height sliders through `g_font_scale_x` and
`g_font_scale_y`. The UI range is 0.5–2.0 in steps of 0.01. Changes use the normal
Apply/Cancel/Save behavior; HUD defaults restore both values to 1.0.
