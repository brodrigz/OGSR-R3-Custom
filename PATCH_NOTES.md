# Bugfix
- Fix tracers being rendered wrongly
- Fix NVG binding, activation and black-screen rendering (R3 helmets and Seb's separate NVG devices)
- Fix knife and weapon HUD getting stuck after scripted item animations
- Fix dragging items onto quick slots dropping them on the ground
- Fix leftover lean when switching sides
- Fix reversed mouse wheel zoom on the PDA map
- Fix overlapping HUD and character info elements on 16:10 displays
- Fix options hints and dropdowns overlapping nearby controls
- Fix missing minimap and FSR 3 option labels, update AO and anti-aliasing descriptions in English and Russian
- Align the flashlight HUD icon above the ammo counter
- Prevent loading sounds and old sound memories from alerting nearby NPCs
- Fix MO2 freezing when switching Radiophobia Unofficial Patch after playing (requires updated MO2 plugin)

# Graphics
- Updated the bundled NVIDIA DLSS runtime to 310.9.1
- Implemented new ambient occlusion method XeGTAO
  Performs about 80% faster than vanilla GTAO, looks better, radius can be changed with r_xegtao_radius)
- Added an option to choose DLSS presets (CNN & Transformer models)
  DLSS and FSR quality changes now reload the required video resources when applied
- Added optional XeGTAO bent normals for directional ambient lighting
- Implemented Folopes' laser shader improvements (distance-based dot size and brightness, sky suppression, better visibility under NVGs)
  Restores laser toggling on original R3 weapons (Mouse 5 by default), laser hides during normal ADS but stays visible in alt-aim mode, native OGSR lasers are preserved
- Enabled sky debanding and preset-based bloom by default

# Optimization
- Optmized all AO methods
- Added optional half-resolution AO rendering (r_ao_resolution)
- Sound cache optimizations(heavily mitigates stutters from Seb's pack)
- Grass rendering optimizations
- Reduce particle task overhead
- Optimize scheduler queue maintenance
- Skip unused scene-depth snapshots when 3D scopes and DoF are inactive
- Reduce interaction prompt text measurement and laser update overhead
- Skip the extra NVG scene copy when no visible legacy laser needs it

# Features
- Restore NPC muzzle flash
- Restore R3 alt sights mode
  The selected aim mode persists per weapon, fake lenses and scope NV update when switching modes
- Restore R3 inspect weapon mode
- Mission objective tracking improvements
- Added Dynamic Dialogue UI option (Oblivion style dialogue UI)
- Added ability to drop items from inventory by dragging them out
- Added Freelook action
- Added Interactible dot markers (implemented in-engine, includes a subset of IDM's features)
  Interaction prompts use the configured keycap without duplicated key hints
- Added Item wheel (implemented in-engine, uses assets from HarukaSai's mod)
- Added Item wheel favorites (pin consumables, grenades and detectors from inventory)
- Added weapon attachment tab to Item wheel (attach / detach scopes, silencers and grenade launchers)
- Added Tactical compass (scripted HUD using engine minimap markers)
- Configurable HUD (Minimap & compass has menu options for position and scale)
- Restore font width and height controls in HUD options
- Added Flashlight HUD icon
- Keys can be binded to multiple actions with shared-key fallthrough (uses first available action)
- Mouse wheel can now be binded to any action
- Added action to cycle between nearby interactible items
- Added Quick use action for world items (use consumables or unload weapons without opening inventory)
- Added loot shortcut to take everything except weapons and armor (R)
- Added Weapon lower / raise action (keeps the weapon lowered through dialogue)
- Sprint exits crouch and lean
- Added Input Behavior tab with Hold / Toggle options for aim, sprint, lean, crouch, walk and low crouch
  Each action uses a single bind instead of separate hold / toggle binds
- Added Lowered weapon on sprint option (replaces default sprint anim)
- Added Sticky aim option (keep ADS after reload / jam)
- Added World bullet penetration option (on/off/reduced)
- Reworked options UI
- Added backpack animation option when Seb's pack is installed
- Gasmask breathing now follows indoor / outdoor level changes
- Added engine support for Anomaly-style HUD animation callbacks
- Added hand attachment support for YAKR knives (requires updated Seb's pack compatibility patch)
- Added name filter and CSV export to the engine's GPU profiler
- Release packages now use the maintained Game resources directly, with UI and compatibility checks before packaging
