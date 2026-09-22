# Optional sun-bounce RSM prototype

This experiment lives on `feat/RSM`. AO and XeGTAO/bent normals remain intact.
RSM defaults **off** and replaces legacy SSFX indirect light only when enabled.
The SSGI screen-space source/gather is not included. Its temporal rejection,
bilateral reconstruction, and D3D11 validation utilities were adapted here.

Build the Release engine and deploy the changed shaders, including every
`ogsr_rsm*` file and the changed shadow/combine files. Enable with:

```text
r_rsm on
cfg_save
```

Then **quit and restart the game**. `vid_restart` preserves the active setting:
material shaders outlive a device reset, so switching MRT layouts needs a full
restart. Disable with `r_rsm off`, `cfg_save`, and another full restart. No RSM
buffers, compute passes, or capture shader variants are created when off.

These controls update live:

| Command | Default | Meaning |
| --- | --- | --- |
| `r_rsm_intensity` | `1` | Contribution scale, 0–4; zero is a visual A/B, not a performance toggle |
| `r_rsm_radius` | `6` | Maximum bounce distance in metres, 0.5–12 |
| `r_rsm_quality` | `medium` | low/medium/high: 16/32/64 source samples, 8/12/16 visibility steps per eligible link |
| `r_rsm_thickness` | `0.2` | Assumed blocker depth thickness in metres, 0.02–1; larger values can over-occlude |
| `r_rsm_debug` | `0` | 0 normal; 1 bounce contribution; 2 source reflectance in the sun's view; 3 visibility diagnostics |

Debug 3 uses red for the rejected fraction of potential bounce links and green
for the accepted fraction of those same links. Both use the same denominator;
the earlier display divided green by the entire sample budget, misleadingly
making sparse valid lighting look almost entirely rejected. Black can mean no candidates
or no valid receiver. It is not a proof that hidden geometry was accounted for.
All debug views are drawn after temporal AA/upscaling, bloom and motion blur;
they do not enter the game's temporal color history.

## Implementation and limits

The nearest sun cascade writes two additional targets alongside its existing
depth map: RGBA8 base reflectance/validity and RG32F packed world normal/float32
raster depth. Normal packing uses two 10-bit octahedral components stored as an
exact integer-valued float. Normals come from world-position derivatives and
face the sun. Opaque, alpha-tested, skinned model and tree shadow variants are
covered; terrain combines its base texture, normalized mask and four diffuse
detail layers with the same scale as the terrain material. Capture does not
evaluate parallax, puddles, or the material's full bump/specular shading.
This is a diffuse approximation, without the complete material BRDF at emitters.

RSM must use full static meshes and their matching progressive LOD windows.
The reduced `OGF_FASTPATH` shadow meshes lack the UV/tangent attributes required
by the capture shaders. Selecting them caused static level draws to disappear:
the input-layout creation failed, leaving mostly actors/props in the source map.
`Fvisual` and `FProgressive` now bypass those reduced meshes while RSM is active.
The disabled renderer retains its original fast-mesh selection.

Only captured surfaces can emit. They may be outside the camera image, but must
be inside the first cascade and its shadow-caster culling volume. Grass/detail
and other depth-only casters are not emitters. The RSM color targets are unbound
before grass shadows: details reuse their regular G-buffer shader, whose normal
and diffuse outputs otherwise overwrite the source reflectance and geometry.
Comparing captured float32 depth
against final shadow depth prevents those casters inheriting an emitter below.
Empty captures are cleared. Frames without submitted sunlight clear the result
and invalidate history, preventing daytime GI persisting at night.

The gather samples a uniform disk in the sun's projection, reconstructs world
positions and integrates diffuse bounce using the disk's projected world area.
There is no fixed screen-pixel radius cap or extra sun-incidence cosine.
Source albedo and sunlight are converted with the engine's gamma convention;
composition applies receiving material albedo before gamma conversion and fog.
AO continues to shade environment lighting; it is not multiplied into the bounce.

Visibility marches each candidate segment through finite-depth slabs in the sun
depth map and camera depth buffer. **Geometry hidden from both views cannot be
tested.** Thin blockers can fall between steps, and thickness approximations can
leak or over-darken. This is not full world-geometry visibility, multi-bounce GI,
or a replacement for diffuse skylight. Near geometry below 0.6 m is excluded
because HUD geometry uses a different projection; nearby world surfaces are also
excluded. Capture boundaries fade out. Shadow-map movement and sparse source
sampling can still cause temporal instability.

Independent temporal history rejects depth/normal mismatches, accounts for
projection jitter and previous-camera depth, and resets on camera cuts, skipped
frames, trace setting changes, explicit renderer resets and resource recreation.
History is capped at 24 frames (8 with motion); spatial filtering is not fed back.

Memory before driver overhead is approximately:

```text
12 * nearest_shadow_map_width^2 + 8 * render_width * render_height
    + 56 * ceil(render_width / 2) * ceil(render_height / 2) bytes
```

That is about **91.5 MiB at 1080p with a 2048-square nearest shadow map**.
Capture uses existing geometry submissions, but adds MRT bandwidth and textured
shadow shading. Enabled shadow variants also carry extra interpolants outside
the captured cascade, and use the full static meshes rather than the reduced
depth-only geometry. This prototype prioritizes evaluating appearance; it is
not yet an optimized GI solution.

## Validation and visual acceptance

```powershell
./Benchmarks/validate_rsm_shaders.ps1
./Benchmarks/validate_rsm_gpu.ps1
```

The shader script compiles 66 variants: capture on/off, all model skinning modes,
tree variants, compute passes, AO composition and diagnostics. The D3D11/WARP
harness executes the actual capture VS/PS, checks opaque and alpha-tested holes,
normal packing and D24 depth agreement, and checks normal composition at
intensities 0/1/4 and display-sized diagnostics. It runs temporal rejection,
reset, extinguished-light, camera-depth and reconstruction checks at even, odd
and tiny dimensions. Shader reflection and the D3D11 debug layer are checked.
During prototype validation, nine representative disabled shadow/composition
shader binaries were also compared with the branch's original AO baseline and
were byte-identical. The Release engine build passed.
Capture execution now also covers static opaque/alpha-tested geometry and
terrain, with packed UVs sampling a distinct texel. A negative input-layout test
reproduces rejection of the position-only shadow mesh; the expected D3D11
messages from that negative test are cleared before validating corrected draws.
The terrain PS check verifies the four differently tinted layers and mask
normalization. Continuous floor/wall tests cover three sun angles, camera
distances of 6/20/50 metres, the terrain shadow offset, and maximum thickness.
Accepted/rejected diagnostic fractions must sum to one when candidates exist.

In a controlled scene whose red emitters are all outside the camera frustum,
mean central bounce is 0.03933 at 128x72 with a 128-square capture and 0.03748 at
512x288 with a 512-square capture. A visible unlit blocker suppresses the tested
bounce to zero. Empty maps, zero sunlight, sky/HUD receivers and stale sources
under replaced depth are rejected. These figures validate those specific cases;
they do not establish live-level appearance, complete occlusion, or GPU cost.

Before considering broader integration, use one fixed sunny location with a
sunlit ground patch next to a shaded wall or overhang. Set debug 0, intensity 1,
radius 6 and medium quality. Toggle intensity 0/1 without moving the camera,
allow history to settle, then turn until the emitting patch leaves the image.
Inspect a nearby closed wall for leakage, and test nighttime/no-sun transition.
Compare total GPU frame time with the option fully off/on using the same saved
camera/weather/AO/upscaler settings. Intensity zero retains the RSM cost.
Measure `sun_shadow_capture` plus `rsm_evaluate`, `rsm_temporal`, `rsm_filter`
and `rsm_resolve`; do not sum parent markers with their children.

Keep this experiment only if its normal-image improvement and total GPU cost
are worthwhile in that location. No live-level GPU cost has been measured.

### Live-save investigation, 2026-09-12

An isolated run loaded the user's `ssgi` save with a copy of the active
`user_ogsr.ltx`: 1080p DLAA, a 3072-square near cascade spanning 25 m, two-sided
sun shadows, grass shadows on, terrain parallax off, RSM radius 6, medium
quality, intensity 1 and thickness 1. The installed engine and shader hashes
matched the checkout. This configuration also had AO quality and bent normals
off, despite selecting XeGTAO as the AO mode.

Readback of the actual capture, G-buffer, constants and GI output reproduced
the weak result in WARP. The original mean red-channel gather value was
0.00006222; replaying that frame with visibility bypassed gave 0.00011094, only
1.78 times as much. These are linear values **before receiving material albedo**.
This disproves the initial assumption that visibility was rejecting every link.
Dark source reflectance, the sampled sunlight, and sparse eligible links produce
a very weak contribution in this particular scene.

The run exposed grass G-buffer outputs overwriting the RSM targets. After
unbinding the targets before grass and adding terrain detail layers, a second
live capture no longer contained those invalid material writes. Mean resolved
red-channel GI remained around 0.000041, similar to the original 0.000040.
The runs have slightly different animation/noise states, so this is not a
pixel-exact A/B. The capture and diagnostic bugs are corrected, but the user's
requested normal-image improvement has **not** been established. Do not treat
synthetic test success as visual acceptance or merge this experiment on that
basis. Temporary capture/auto-exit instrumentation was removed from the engine.

Reference: Dachsbacher and Stamminger, *Reflective Shadow Maps* (I3D 2005),
[DOI 10.1145/1053427.1053460](https://doi.org/10.1145/1053427.1053460).
