# Optional screen-space indirect lighting

SSGI defaults to **off**. It replaces the old `ssfx_indirect_light` effect when
enabled and works independently of AO, including XeGTAO with bent normals.

To enable, enter these console commands, then quit and restart the game:

```text
r_ssgi on
cfg_save
```

Use `r_ssgi off`, `cfg_save`, and restart to disable. **`vid_restart` is not
sufficient:** material shaders survive a device reset. The renderer deliberately
keeps the active SSGI setting across `vid_restart` to prevent a mismatch between
shader outputs and render targets. No SSGI buffers or compute passes are created
when the renderer starts with SSGI off.

The remaining controls update live:

| Command | Default | Meaning |
| --- | --- | --- |
| `r_ssgi_quality` | `medium` | `low`, `medium`, `high`: 16, 40, 72 depth taps per half-resolution pixel |
| `r_ssgi_radius` | `3` | Local bounce radius in metres; range 0.25–10 |
| `r_ssgi_thickness` | `0.25` | Assumed surface thickness in metres; range 0.02–2 |
| `r_ssgi_intensity` | `1` | Contribution scale; range 0–4; zero hides GI but keeps its cost |
| `r_ssgi_debug` | `0` | 0: normal image; 1: indirect contribution; 2: diffuse/emissive source including environment; 3: history length |

Debug views are drawn after temporal AA/upscaling, tone mapping, bloom, and motion
blur. They do not enter the game's temporal color history. Mode 3 is an age
display: white means 24 accumulated frames, not strong GI or proof of valid
lighting. The GI history itself remains temporally filtered in every mode.

## Rendering

Sun, local lights, and deferred emissive materials write a second RGBA16F
accumulator only in the enabled shader variant. It contains linear diffuse
outgoing lighting and emissive alpha, excluding specular highlights. The usual
lighting output remains unchanged. The existing material model's diffuse and
foliage transmission terms are preserved.

Before gathering, an additive fullscreen pass adds diffuse environment lighting
to that same source buffer. It uses the composition pass's environment cubes,
material response, hemisphere value, AO, and bent normal. Environment specular
is excluded and emissive alpha is preserved. This allows sky-lit surfaces to
bounce light even when direct sun or local lighting is absent. No GI, fog, or
postprocessed scene color is fed back into the source.

An independent SM5 compute pass uses angular visibility bitmasks and finite
surface thickness to estimate one diffuse bounce from visible surfaces. It uses
the closest covered pixel in each physical 2x2 footprint and reconstructs view
positions with the renderer's projection jitter convention. Sampling is based
on [Therrien et al.'s visibility-bitmask approach](https://cdrinmatane.github.io/posts/ssaovb-code/),
with cosine/solid-angle weights, hemisphere rejection, distance and edge fades,
and an emissive outlier clamp. It does not reuse XeGTAO's filtered depth mips.
The projected radius is derived from metres, depth, FOV, and render resolution;
there is no fixed pixel cap. Quality bounds the number of taps, while off-screen
taps are rejected.

Velocity reprojection includes current/previous jitter and compares history
depth in the previous camera's view space. World-space normals reject surface
changes. History is clamped to the current compatible neighborhood, capped at
24 frames (8 under motion), and reset after camera cuts, missed frames, explicit
temporal resets, resource recreation, or trace-setting changes. Moving objects
with significant depth changes can conservatively lose history because velocity
contains only screen-space displacement.

A depth/normal-aware filter and full-resolution reconstruction retain the
existing environment lighting when no matching surface exists. Spatial filtering
is not fed back into history. Composition applies the receiving material's
albedo before gamma conversion and fog. XeGTAO continues to shade environment
lighting; its AO is not applied a second time to the computed bounce.

Resources cost approximately 28 bytes per internal-resolution pixel (about
55.4 MiB at 1920x1080), plus alignment/driver overhead: two full-resolution and
six half-resolution RGBA16F textures. The additional diffuse MRT adds light
accumulation bandwidth, and environment preparation adds a fullscreen pass;
measuring only the compute markers understates total cost.

## Limits and validation

This is local, single-bounce screen-space GI. It cannot recover unseen or hidden
surfaces and retains the existing ambient lighting as its fallback. Forward
transparent effects are not bounce sources. Near geometry below 0.6 m is excluded
conservatively because HUD rendering uses another projection; this also excludes
world geometry within that distance. Assumed thickness and half-resolution
sampling can still leak or miss thin geometry. No performance or image-quality
claim is made for a live level until it is measured there.

Run from the repository root:

```powershell
./Benchmarks/validate_ssgi_shaders.ps1
./Benchmarks/validate_ssgi_gpu.ps1
```

The shader script checks the compute passes and enabled/disabled variants of all
affected light accumulators and composition paths, including AO modes, fog, NVG,
and exclusion of the legacy indirect-light pass. It also compiles environment
source variants and the separate diagnostic overlay (65 variants total).
The headless D3D11/WARP harness
executes the actual compiled compute shaders with FP16 textures at 64x48, 65x49,
1x1, and 3x5. It checks colored bounce, no coplanar self-lighting, no backface
emission, emissive alpha, sky/HUD exclusion, history rejection and reset,
extinguished lights, camera depth transformation, reconstruction boundaries,
constant irradiance, incompatible surfaces, finite results, cbuffer reflection,
and debug-layer warnings/errors. WARP timings are not gaming GPU benchmarks.

The harness also runs the actual source/composition pixel shaders: additive
diffuse environment light, emissive-alpha preservation, AO attenuation, absence
of source feedback bindings, intensity 0/1/4 in normal composition, and the
history overlay at twice the render resolution.

A quantitative corner test uses a unit-radiance red wall, a perpendicular
receiver, a 60-degree vertical FOV, and the default trace settings. It averages
bounce over the central 10% of each image dimension. The original 256-pixel
radius cap reduced that average from 0.105853 at 192x108 to 0.00047183 at
1920x1080. With the cap removed, the 1080p result is 0.0944427, with every tested
center pixel above 0.001 instead of 4.2%. Regression checks require both averages
to exceed 0.05 and stay within 25% across resolutions. These are controlled shader
measurements, not measurements from a live game level.

For an in-game comparison, capture the same camera path and weather with SSGI
off, legacy SSFX indirect light, and SSGI at each quality. Keep AO, resolution,
upscaler, and other settings fixed; warm history before capture. Use Cordon with
sunlit walls, a window-lit interior, and moving foliage. Include camera turns,
moving lamps, save/load, FOV changes, night vision, scopes, and a resolution
change. Inspect debug modes 1–3 for flicker, leaks, missing sources, and stale
history. With SSGI enabled, compare `r_ssgi_intensity 0` and `1` at debug 0 for
a live lighting A/B without changing the renderer variant or legacy IL setting.
Record total GPU frame time and the `ssgi_source`, `ssgi_evaluate`, `ssgi_temporal`,
`ssgi_filter`, and `ssgi_resolve` markers; also compare light-accumulation time.
