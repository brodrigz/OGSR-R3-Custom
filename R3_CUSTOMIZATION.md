# R3 customization baseline

This fork rebases Radiophobia 3 version 1.20 engine contracts onto current OGSR while keeping reusable capability in the engine and feature policy in game scripts and configuration.

## Integration rule

- Engine code should expose generic operations, data formats, callbacks, or lifecycle primitives.
- Game scripts and configuration should decide which feature uses those operations.
- Do not name or bootstrap an unrelated content module from engine code.
- Register script features through the game's add-on registry and keep attachment idempotent when duplicate protection is useful.

Examples of reusable surfaces in this fork include script wallmark placement, layered HUD sounds, actor-owned script cameras, world-to-UI projection, HUD item override animations, and detector visibility controls.

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

## Porting custom features

Keep compatibility work focused on observable contracts:

1. Identify the content-facing script, configuration, callback, data-layout, or UI requirement.
2. Compare it with the corresponding public OGSR implementation.
3. Add the smallest generic engine surface that preserves the required contract.
4. Keep feature activation in scripts/configuration.
5. Build both Release and Debug when changing debug-only tooling.
6. Validate with a focused runtime route and retain logs for the result.
