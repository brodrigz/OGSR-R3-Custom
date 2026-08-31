# R3 customization baseline

This fork rebases Radiophobia 3 version 1.20 engine contracts onto current OGSR while keeping reusable capability in the engine and feature policy in game scripts and configuration.

## Integration rule

- Engine code should expose generic operations, data formats, callbacks, or lifecycle primitives.
- Game scripts and configuration should decide which feature uses those operations.
- Do not name or bootstrap an unrelated content module from engine code.
- Register script features through the game's add-on registry and keep attachment idempotent when duplicate protection is useful.

Examples of reusable surfaces in this fork include script wallmark placement, layered HUD sounds, actor-owned script cameras, world-to-UI projection, HUD item override animations, and detector visibility controls.

## Late reload completion

Set `complete_late_reload_on_hide = true` in `[features]` to let an actor's ordinary magazine reload finish when the weapon is hidden during its last fifteen percent. This is intended for quick item-use animations that temporarily hide the active weapon. It is player-only, disabled by default, applies only when the resolved hands reload has no usable commit mark, and deliberately excludes tri-state shell reloads.

The generic saved console command `g_complete_late_reload_on_hide on|off` controls the same feature bit at runtime. Radiophobia's bundled Gameplay-options checkbox uses it, so its change applies immediately and is saved in the user's configuration. Content that does not include that UI can still choose the startup default through `[features]`.

Builds compiled with `DEBUG` retain opt-in reload tracing. Start such an engine with `-trace_reload` to log actor reload requests, resolved motion marks, animation completion, tri-state cartridge commits, and late-hide decisions. Release builds contain no reload diagnostic logging.

## Automatic aim zoom

The saved `g_auto_aim_zoom on|off` command optionally applies the same 0.75
final-FOV multiplier as the free-zoom action while the actor aims through iron,
reflex, or holographic sights. The default classifier rejects 2D scope textures,
active 3D scope viewports, and effective zoom factors above 1.10. A weapon or
active attachable-optic section can explicitly set `auto_aim_zoom = true|false`
to override that default. Radiophobia's bundled Gameplay-options checkbox
controls and persists the feature without requiring a new key binding.

## Lean toggle

The saved `lean_toggle on|off` command changes the existing left/right lean
actions between hold and toggle behavior. With it enabled, pressing the active
lean direction returns the actor upright and pressing the opposite direction
switches sides. Radiophobia's bundled Gameplay-options checkbox controls and
persists the setting; hold-to-lean remains the default.

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

## Porting custom features

Keep compatibility work focused on observable contracts:

1. Identify the content-facing script, configuration, callback, data-layout, or UI requirement.
2. Compare it with the corresponding public OGSR implementation.
3. Add the smallest generic engine surface that preserves the required contract.
4. Keep feature activation in scripts/configuration.
5. Build both Release and Debug when changing debug-only tooling.
6. Validate with a focused runtime route and retain logs for the result.
