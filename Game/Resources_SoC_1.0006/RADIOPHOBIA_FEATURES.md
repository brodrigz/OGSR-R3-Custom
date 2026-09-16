# Radiophobia resource features

These resources are maintained in this directory and selected for publication by
`radiophobia-release-files.txt`. See README.md for the source and packaging rules.

It restores Radiophobia's difficulty/options integration, valid input/UI
definitions, callback and blood-pool wallmark usage, and the settings required
by the engine feature set. Vanilla R3 blanks the PDA Contacts tab; this payload
restores the Contacts button in `pda.xml` and `pda_16.xml` so the existing
contacts window is reachable again. The Gameplay menu includes a saved Late Reload
Completion checkbox; it changes the engine feature immediately. The external-mod
package supplies the initial late-reload default; explicit scope-texture
enablement is harmless and keeps the content
policy clear even though the engine now defaults it on. Late ordinary reloads
without a usable hands-animation commit mark complete when interrupted during
their final fifteen percent; marked and tri-state reloads keep native timing.
The same Gameplay page includes Automatic Aim Zoom in the free left-column slot
beside Late Reload Completion. It is disabled by default and adds free-zoom-like
magnification to iron, reflex, and holographic aiming without stacking onto
scopes that already magnify.

The overlay includes the native item wheel (QAW art used with permission). Hold
the Item wheel bind (default B on a fresh controls file) for meds, food,
grenades, and pinned eatables/grenades. Inventory right-click adds or removes a
pin. Existing `user.ltx` keymaps need the bind set in Options.

Gameplay Options also expose native hold-to-sprint and sticky aim (restore ADS
after reload/jam). Alt-aim mode now persists per weapon without a checkbox.
Walk toggle and low-crouch toggle are
in the keybinding list and ship unbound. Loot uses E for take-all and R for
everything except weapons and armor. Indoor gasmask breathing is
`rad_breath_indoors.script`; it is not `rad_qol_moves`. A backpack open/close
animation checkbox is in Gameplay Options but stays hidden unless Seb's pack
is installed (`hoc_backpack_inventory_anim.script`). It saves
`rad3_backpack_anim`, so that pack does not need to replace the Options UI.

The Advanced Video page includes DLSS and FSR 3 quality controls with English
and Russian labels. Applying a quality change performs the required video
resource restart without showing the full application-restart warning.
XeGTAO bent normals follow the same video-restart behavior. The options XML
retains Radiophobia's redesigned layout, including separate Gameplay/HUD tabs;
the publisher validates it against the shipped scripts before creating a ZIP.

The same payload restores the three original 2D dedicated night sights
(`1pn93`, `pn23`, and `1pn93n2_1gs`). The engine resolves each scope's opaque
profile, while the bundled scripts drive the existing R3 shader-NV visual only
after completed 2D ADS. It does not enable fullscreen NV for 3DSS optics.

The add-on registry otherwise follows vanilla R3: it retains
`ogsr_items_anims`, registers `rad_breath_indoors`, does not register
`rad_items_anims` or `rad_qol_moves`, and keeps the unsupported laser
controller disabled.

The main menu's existing Engine version label is populated from the runtime
engine string. It identifies the OGSR 3.548 baseline as well as the active
executable's configuration and compile timestamp.

Do not install this patch on arbitrary R3-based mods: several UI and gameplay
scripts are Radiophobia-specific. No save conversion is included or promised.
