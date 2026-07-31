# Handoff: GPU indirect (bounce) lighting

Why this is the next big win: on `cave.bsp` (`-bounce 3 -minlight 12 -soft 4
-extra4`), the whole run is ~23 s, of which the three Indirect Lighting passes
are ~22.5 s. The GPU direct phase (done — see `GPU_TODO.md`) can only ever save
the ~0.03 s the CPU direct phase costs on maps like this. Indirect is where the
time goes, and its ray workload has the same shape the direct phase already
handles on the GPU: many independent occlusion rays from precomputed sample
points, accumulated into `lightsample_t`.

Everything below is written against the current tree (direct-phase GPU port
with items #1–#3 landed). Line numbers drift; anchor on symbol names.

## 1. How the CPU indirect phase works

### Data flow per bounce pass (light.cc:860–890, LightWorld)

```
Direct Lighting (parallel DirectLightFace)      <- GPU handles entity+sun part
GPU_DirectQueue_Flush                            <- light.cc:862 (must stay before MakeBounceLights)
for depth in 0..bounce-1:
    MakeBounceLights(cfg, bsp, depth)            <- bounce.cc: harvest lightmap.bounce_color -> VPLs
    UpdateEmissiveLightSurfacesList()            <- light.cc: collect lightsurfs with .vpl
    parallel IndirectLightFace(face, depth)      <- ltface.cc:3262 -> LightFace_SurfaceLight
```

The feedback loop is the critical ordering constraint: each pass's
`LightFace_SurfaceLight` **adds into `lightmap->bounce_color`**, and the next
pass's `MakeBounceLights` **reads and clears** it (bounce.cc:160–178, divides
by sample count, multiplies by the face's texture bounce color, creates a
`surfacelight_t` VPL with `bounce_level = depth`). So a GPU indirect queue must
be fully flushed **after each pass's parallel loop, before the next
`MakeBounceLights`** — one flush call per pass inside the bounce loop, same
pattern as the existing `GPU_DirectQueue_Flush(&bsp)` at light.cc:862.

### The VPL structure (include/light/surflight.hh:38)

`surfacelight_t` on each emitting face's `lightsurf_t::vpl`:
- `pos` (face midpoint + 1u along normal), `surfnormal`, `bounds` (only
  meaningful when `visapprox == RAYS`), `points` (emission sample points, each
  lifted 1u off the face).
- `styles[]`: per-style `per_style_t { bounce_level (nullopt = direct surface
  light, N = bounce pass N), omnidirectional (sky emitters), rescale, style,
  intensity (= totalintensity / points.size()), totalintensity, atten, color }`.

Point counts depend on `-emissivequality` (**default LOW**): LOW = 1 point
(midpoint), MEDIUM = midpoint + winding verts, HIGH = winding diced by
`bouncelightsubdivision` (default 64). At LOW, a bounce VPL is a single point —
ray counts per pass ≈ (bouncing faces with nonzero color) × (samples that
survive culls/gate).

Two producers create VPLs with the **same** consumer code path:
- `MakeRadiositySurfaceLights` (surflight.cc): actual emissive-texture surface
  lights, `bounce_level = nullopt`. Consumed during the **direct** phase by the
  `LightFace_SurfaceLight(..., std::nullopt, surflightscale, surflightskyscale,
  16.0f)` call in DirectLightFace (ltface.cc:~3236).
- `MakeBounceLights` (bounce.cc): bounce VPLs, `bounce_level = depth`. Consumed
  by `IndirectLightFace` → `LightFace_SurfaceLight(..., depth,
  bouncescale*0.5, bouncescale, 128.0f)` (ltface.cc:3262).

A GPU surflight kernel therefore covers both the indirect passes **and** the
direct-phase surface lights; only the scales, `hotspot_clamp` (16 vs 128), and
the `bounce_level` filter differ. Port the bounce passes first (that's where
the time is), keep direct surflights on CPU initially — they're additive and
order-commutative, so mixing is safe.

### The consumer inner loop (LightFace_SurfaceLight, ltface.cc:2002)

For each emissive surface × style with `bounce_level == depth`:
1. `SurfaceLight_SphereCull` (ltface.cc:1966): visapprox-RAYS bounds-disjoint
   cull, then a gate cull using `totalintensity` at the face's bounding-sphere
   distance (skipped entirely when `surflight_gate == 0`).
2. `SurfaceLight_VisCull` (ltface.cc:1988): PVS cull when `visapprox == VIS`
   (note: iterates the **emitting** surface's leaves against the receiving
   face's pvs).
3. For each VPL point c, for each non-`occluded` sample i:
   - `dir = normalize(sample.point - vpl.points[c])`, `dist = max(0.01, len)`;
     `use_normal = !lightsurf->twosided` (twosided ⇒ dp2 = 1).
   - `GetSurfaceLighting` (ltface.cc:1921) computes the color (see §2).
   - Per-ray gate: `qv::gate(indirect, surflight_gate)` = **all** components ≤
     gate ⇒ skip. `surflight_gate` = 0.01, or 0 when `-emissivequality HIGH`.
   - Ray is traced **from the VPL point to the sample** (origin = vpl point!),
     occlusion-only, `CHANNEL_MASK_DEFAULT`, self = receiving face's modelinfo.
4. Unoccluded rays: `indirect *= Dirt_GetScaleFactor(...)`, then
   `sample.color += indirect; lightmap->bounce_color += indirect;` on the
   lightmap for `vpl_setting.style`, `Lightmap_Save`.

### The math to replicate exactly (GetSurfaceLighting, ltface.cc:1921)

```
dp1 = dot(vpl.surfnormal, dir)          // dir: vpl -> sample
dp2 = use_normal ? dot(-dir, sample.normal) : 1.0
if (!omnidirectional):
    if dp1 < -LIGHT_ANGLE_EPSILON: return 0      // sample behind vpl
    if dp2 < -LIGHT_ANGLE_EPSILON: return 0      // vpl behind sample face
    if rescale: dp1 = 0.5 + dp1*0.5; dp2 = 0.5 + dp2*0.5
    dpf = dp1 * dp2
else:
    dpf = dp2 * 0.5                              // sky emitters
dpf = max(0, dpf); if dpf == 0: return 0
if omnidirectional: dist += cfg.surflightskydist
scale = (omnidirectional ? sky_scale : standard_scale) * dpf
// SurfaceLight_ColorAtDist (ltface.cc:852) = LF_QRAD3 with the pass's hotspot_clamp:
value = cfg.scaledist * atten * dist
d = max(value, hotspot_clamp)                    // hotspot_clamp: 128 bounce, 16 direct surflight
return color * (intensity / (d*d)) * scale       // [0,255] range, /255 happens nowhere here!
```

Careful: unlike entity lights, there is **no /255** and **no anglescale** here;
the color returned already includes intensity and is added to `sample.color` as
is. `LIGHT_ANGLE_EPSILON = 0.01f` (light.hh:37).

## 2. Proposed GPU architecture

Mirror the direct-phase design (queue in ltface.cc + compute kernel), reusing
the existing BLAS/TLAS, slot machinery, and persistent-buffer code in
trace_gpu_vulkan.cc. Concretely:

- **Sources**: one GPU record per (emissive surface, style) pair matching the
  pass's `bounce_level`, holding `{surfnormal, intensity, atten, color, flags
  (omnidirectional|rescale), point_begin, point_count}` + a flat `points[]`
  buffer (vec3 + pad). The shader loops the point range per source. This keeps
  per-face source lists at (face × VPL) granularity, not (face × point) — at
  `-emissivequality HIGH` points can be numerous.
- **Per-face source lists** built in the queue's AddFace under the mutex,
  running the *actual* CPU culls per (face, vpl): `SurfaceLight_SphereCull` +
  `SurfaceLight_VisCull` — same trick as `GPU_Direct_SourceReachesFace`. Both
  functions are already file-local and callable from the GPU block.
- **Samples**: identical layout to the direct phase (`direct_phase_sample_t`
  works as is: pos, normal, occlusion, twosided flag, face_index). The sample
  data for a face is *identical across the direct phase and every bounce pass*
  — a later optimization is to upload each face's samples once and reference
  them by offset, instead of re-copying ~50 MB per flush per pass.
- **Push constants**: `{sample_count, source_count, scaledist, gate,
  standard_scale, sky_scale, hotspot_clamp, surflightskydist}` (needs a larger
  push-constant block than the direct kernel's 16 bytes — still tiny).
- **Kernel**: one thread per sample; loop face's sources; loop source's points;
  compute the §1 math; gate; occlusion ray query (terminate-on-first-hit over
  the whole TLAS — sky occludes, same as Embree's occlusion trace); accumulate
  color into a per-sample accum. Also accumulate nothing into `direction` —
  the CPU path does **not** add a normalcontrib for surface lights (check:
  LightFace_SurfaceLight never touches `sample.direction`) — the accum's
  normal field must be ignored/zero on apply.
- **Apply**: per batch, `sample.color += accum; lightmap->bounce_color +=
  accum;` on the **style-0** lightmap (v1 restriction, see fallbacks),
  `Lightmap_Save`. `bounce_color` matters — it feeds the next pass.
- **Ray direction**: to match the CPU exactly, trace from the VPL point toward
  the sample (origin = vpl point, tmax = dist). Origin/tmin epsilon behavior at
  the *emitter* end is what the CPU does; flipping the ray direction would move
  the epsilon to the receiver end and shift shadow edges (this class of
  grazing-precision difference is exactly what caused the accepted 46-face
  residual in the direct sun port — don't add a second source of it).

### Queue scheduling and the race pattern

Reuse the direct-phase solution wholesale:
- `IndirectLightFace` becomes: `if (GPU_IndirectQueue_WillHandleFace(...))`
  skip the CPU `LightFace_SurfaceLight` call, then enqueue **at the end** of
  the function; late CPU fallback if the GPU got disabled between decision and
  enqueue. IndirectLightFace does nothing else CPU-side, so this is simpler
  than DirectLightFace was — but the invariant (a queued face has finished all
  CPU writes for the pass) must hold anyway because flushes apply concurrently.
- Threshold flush: take-batch under mutex, dispatch outside (copy
  `gpu_direct_batch_t` / `GPU_DirectQueue_DispatchBatch`, ltface.cc:2952/2991).
  The Vulkan slot machinery (two slots, fences) is shared with the direct
  kernel as-is — just a different pipeline + descriptor layout; either give the
  surflight kernel its own two slots or extend `direct_slot_t` with the second
  pipeline's descriptor set (buffers can be shared since a slot is owned for a
  whole batch).
- **End-of-pass flush**: add `GPU_IndirectQueue_Flush(&bsp)` after the
  IndirectLightFace parallel loop inside the bounce loop in LightWorld
  (light.cc:~884), before the loop comes back around to `MakeBounceLights`.
  Sources/point buffers must be rebuilt every pass (new VPL set per depth) —
  reset the "sources built" latch at each pass boundary; the per-run latch
  used by the direct queue is not sufficient here.

### Fallback conditions (disable GPU indirect, keep CPU path)

Mirror the direct phase's philosophy: fall back for anything not bit-exactly
replicated. For v1:
- `dirt_in_use` (Dirt_GetScaleFactor with entity=nullptr is still nontrivial).
- Any VPL style != 0 present for the pass (`-bouncestyled`; default off).
- `light_options.visapprox == RAYS` works (bounds cull runs CPU-side in
  AddFace); `VIS` works the same way. No fallback needed for either.
- Non-default `object_channel_mask` faces: return handled-with-nothing (the
  CPU path returns early for them too, ltface.cc:2009).
- `-emissivequality HIGH` sets gate to 0 — supported, just pass gate=0.

## 3. Validation playbook (what worked for the direct port)

1. **Never byte-compare `.lit` files across runs** — `lightofs` allocation
   order is nondeterministic; identical lightmaps land at shuffled offsets
   (~80% of bytes "differ" between bit-identical runs). Compare per-face:
   read each run's own FACES lump `lightofs`, diff the RGB blocks
   face-by-face. A working script existed at (session scratchpad)
   `cmp_faces.py`; the memory file `gpu-light-branch-context.md` describes it.
   Recreate it if gone — ~80 lines of Python, bsp29 header + FACES lump parse.
2. Isolate phases with flags: `-bounce 0` (no indirect at all), `-bounce 1`
   (single pass — errors don't compound), `-sunlight 0` (kill the sun),
   full `-bounce 3`. Compare GPU vs CPU at each level; a diff that appears
   only at `-bounce 1` is in the surflight kernel or its culls; a diff that
   *grows* with passes is in `bounce_color` accounting.
3. Determinism check: run the GPU build twice, expect 0 differing faces.
   Per-face results are deterministic on both paths today; keep it that way
   (batch composition must not affect per-sample math).
4. Expected end state: bit-identical to CPU except rays whose visibility flips
   at grazing incidence (Embree vs HW RT edge precision). The direct port
   settled at 46/3418 faces, max byte delta 51 — that magnitude is the bar.
   Anything larger or systematic (one-sided brighter/darker) is a real bug —
   correlate differing faces with geometry (see scratchpad `diag_faces.py`
   pattern: bucket by orientation, report brighter-vs-darker byte counts).
5. Benchmark command (RTX 3070, this machine):
   `./build-linux/light/light -bounce 3 -minlight 12 -soft 4 -extra4 -nopercent
   [-gpu] /home/tzcnt/quake/id1/maps/cave.bsp`. Incremental rebuild:
   `make -j8 light` in `build-linux/`. Baselines: CPU ≈ 23.6 s, GPU ≈ 23.1 s,
   of which indirect ≈ 22.5 s — that's the number this work attacks.

## 4. Pitfalls learned from the direct-phase port (read before coding)

- **Flush placement**: results must be applied before anything reads the
  lightmaps. For indirect that reader is the *next pass's* `MakeBounceLights`
  (and `PostProcessLightFace` after the last pass). Missing a flush loses
  light silently *and* leaves dangling `lightsurf_t*` in the queue if it fires
  after `ClearLightmapSurfaces` (the original segfault).
- **Apply-vs-CPU-writes race**: only enqueue a face after all CPU-side writes
  to its lightmaps for that phase are done (enqueue-at-end + WillHandleFace
  decision split).
- **Exact culls beat approximate culls**: calling the CPU's own cull functions
  from AddFace gave bit-exact source selection *and* halved the ray count.
  Don't invent additional approximate culls; the older opt-in GPU source-cull
  machinery was removed after exact culling superseded it.
- **HOST_CACHED for the readback buffer** — reading write-combined memory is
  ~150 ms per 50 MB. Already handled by `ensure_persistent_buffer(...,
  host_cached=true)`; keep using it for the new accum buffer.
- **Descriptor updates must precede command recording** (no update between
  record and execution completion).
- **Watch enum/constant drift**: the direct port's shader had wrong formula
  constants (atten², missing scaledist, wrong QRAD3 clamp) that never showed on
  the test map because atten=1/scaledist=1/formula=linear. Test maps hide
  parameter bugs; transcribe `SurfaceLight_ColorAtDist` literally and note
  which parameters the test map doesn't exercise (here: `atten` != 1,
  `surflightskydist`, omnidirectional/sky VPLs, `rescale`).
- **Negative/`PostProcess` interactions**: check which phase *actually* applies
  an effect before porting it (antilights looked direct-phase but live in
  PostProcess; localmin looked direct-phase but is a clamp with ordering
  constraints).

## 5. Perf expectations and sizing

- Pass-0 CPU cost on cave.bsp ≈ 11.4 s with Embree across all cores; passes
  decay (9.4 s, 2.1 s) as bounce energy shrinks and the sphere-gate culls more.
- The CPU cost is not only rays: `GetSurfaceLighting` runs per (point, sample)
  *before* tracing (for the gate), so the arithmetic moves to the GPU too —
  expect superlinear gains vs the direct port, where the CPU math was trivial.
- Batch sizing: samples per batch stay ~1M (48 MB); the sources/points buffers
  are small. The face-source-index list is (faces × surviving VPLs) — on a map
  with thousands of bouncing faces this is the buffer to watch; it's uint32
  per pair, so even 3418 faces × 2000 VPLs ≈ 27 MB, fine.
- If the upload memcpy shows up after this lands, the "upload samples once,
  reference by offset across direct + all passes" optimization (§2) is the
  next lever, followed by `GPU_TODO.md` #4 (sharded enqueue).
