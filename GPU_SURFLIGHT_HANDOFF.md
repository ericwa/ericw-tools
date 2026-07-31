# Handoff: GPU direct-phase surface lights + remaining CPU fallbacks

Status as of 2026-07-30 (evening): **section 1 is DONE and validated.** The
direct-phase surface lights (radiosity VPLs with `bounce_level == nullopt`)
now run on the GPU via the recommended shared-batch design: one direct queue,
two kernels recorded into one submission (`trace_direct_combined_batch`,
trace_gpu_vulkan.cc), each with its own per-face ranges and accum buffer,
samples uploaded once and summed at readback. The deferral decisions are
independent (`gpu_direct_plan_t` in ltface.cc): an unsupported entity/sun
light falls only the entity/sun loops back to the CPU, and a styled surflight
/ dirt / LF_LOCALMIN presence keeps only `LightFace_SurfaceLight(nullopt)` on
the CPU (the LOCALMIN guard exists because `LightFace_LocalMin` writes
lightmaps after the enqueue and both sub-phases must be off for that to stay
race-free). Validation on cave_test (`-extra4 -soft 4`): point-light-only A/B
(`-surflightscale 0 -surflightskyscale 0`) reproduces the pre-existing 7-face
shadow-boundary residual untouched; adding direct surflights adds ~10 faces at
exactly ±1 byte (summation-order class); full `-bounce 3 -minlight 12` run is
7646 faces ±1 + 81 faces range 2–72 (vs the pre-port sun-residual bar of 86
faces 2–107); GPU runs are bit-identical run-to-run. Perf: full benchmark
15.8 s → ~12.0 s (CPU 183 s). The rescale and omnidirectional shader branches
are now exercised and validated. Zero shader changes were needed, as
predicted. Section 2 below remains the live inventory of what stays on the
CPU. Section 1 is kept for reference.

Line numbers are against the current tree and will drift; anchor on symbols.

## 1. Direct-phase surface lights

### Why this matters

- **Quake 2 maps hit this path by default.** `light.cc:1327` sets
  `surflight_radiosity = SURFLIGHT_RAD` for the Q2 game target, and every
  `SURF_LIGHT`-flagged texinfo face becomes a radiosity VPL
  (`IsSurfaceLitFace`, surflight.cc:211). On a Q2 map with many emissive
  textures, the direct surflight loop has the same shape and cost as a bounce
  pass — this is where the port pays off most.
- Q1 maps only hit it via a light entity with `"_surface" "texname"` +
  `"_surface_radiosity" "1"` (or global `-surflight_radiosity 1`). Without the
  radiosity key, `_surface` templates spawn jittered **point** lights
  (`MakeSurfaceLights`, entities.cc) which the GPU direct phase already
  handles.

### Producer: MakeRadiositySurfaceLights (surflight.cc)

Creates `surfacelight_t` VPLs on emissive faces, one `per_style_t` with
`bounce_level = std::nullopt`. Differences from the bounce producer
(`MakeBounceLight`, bounce.cc) that the port must respect:

- **`omnidirectional = !is_directional`** — true for sky-texture surface
  lights. The consumer then uses `sky_scale` (= `surflightskyscale`), skips
  the dp1 cull, uses `dpf = dp2 * 0.5`, and adds `cfg.surflightskydist` to the
  falloff distance (`GetSurfaceLighting`, ltface.cc:1921).
- **`rescale` defaults to TRUE for non-sky** radiosity surflights
  (`extended_flags.surflight_rescale` overrides; sky defaults false). The
  bounce producer never sets it. `surflight.comp` implements the branch
  (`dp = 0.5 + dp*0.5`) but it has **never been validated against the CPU**.
- **`atten`** comes from the `_surflight_atten` texinfo flag (default 1.0).
  Shader supports it; untested with != 1.
- **`intensity = totalintensity / points_before_culling`** — NOT
  `/ points.size()` like bounce.cc. Points are diced by `surflightsubdivision`
  (default 16) and then culled/moved by `FixLightOnFace`, so
  `points.size() <= points_before_culling`. The GPU source builder copies
  `setting.intensity` verbatim, so this is only a trap if you ever recompute
  intensity — don't.
- **`style`** can be non-zero via `surflight_targetname` / `surflight_style`
  texinfo flags or the template entity's style. v1 restriction: fall back for
  any nullopt-style setting with `style != 0` (see fallback policy below).
- `minlight_scale` on the VPL is consumed **only** by `PostProcessLightFace`
  (ltface.cc:3632, surface-lit faces get self-minlight) — that stays CPU and
  is NOT part of this port.

### Consumer: what differs from the bounce port (almost nothing)

Same `LightFace_SurfaceLight` code path the bounce port replicated. The
`surflight.comp` kernel and `trace_surflight_batch` backend need **zero shader
changes** — the per-pass push constants already parameterize everything:

| parameter        | bounce passes (done)     | direct surflights (this port) |
|------------------|--------------------------|-------------------------------|
| style filter     | `bounce_level == depth`  | `bounce_level == nullopt`     |
| standard_scale   | `bouncescale * 0.5`      | `surflightscale`              |
| sky_scale        | `bouncescale`            | `surflightskyscale`           |
| hotspot_clamp    | 128                      | 16                            |
| gate             | 0.01 (0 at `-emissivequality HIGH`) | same         |

The per-face culls are the same file-local functions
(`SurfaceLight_SphereCull` + `SurfaceLight_VisCull`, ltface.cc:1966/1988) —
call them with `hotspot_clamp = 16`. Note SphereCull internally uses
`cfg.surflightscale`/`surflightskyscale` for its gate estimate regardless of
which pass calls it; that is the CPU's own behavior, keep it.

The queue machinery can be a third instance of the existing pattern (the
`GPU_IndirectQueue_*` block at ltface.cc:3170 is the template — it's the
simpler of the two queues), or a parameterized generalization of it. Sources
are built **once per run** (the nullopt VPL set never changes), unlike the
per-pass rebuild the bounce queue does.

### The one genuinely new problem: two GPU queues in the same phase

The direct-port invariant: *a face may be enqueued only after all CPU writes
to its lightmaps for the phase are done*, because flushes apply results from
another thread concurrently. Today `DirectLightFace` does:

```
WillHandleFace (decision)            <- skip CPU entity/sun loops
CPU LightFace_SurfaceLight(nullopt)  <- writes lightmaps
GPU_DirectQueue_AddFace              <- enqueue at end
```

If surflights also become a GPU queue, a face lands in **two queues whose
flushes run on different threads and both do `sample.color +=` /
`bounce_color +=` on the same style-0 lightmap — a data race** the bounce
port never had (its phases are separated by full flushes).

Recommended design — **one shared batch, two kernels**:

- Extend `gpu_direct_face_record_t` / the direct queue's face range with a
  second source-index range (surflight sources). One `AddFace`, one queue,
  one mutex, one flush path.
- `GPU_DirectQueue_DispatchBatch` records both pipelines against the same
  uploaded samples buffer (two `vkCmdDispatch` in the slot's command buffer
  with a barrier between? No barrier needed — they write different accum
  buffers; add a second accum persistent buffer to the slot), then one apply
  loop sums both accums per sample. The slot already owns both descriptor
  sets (`direct_slot_t.descriptor_set` / `.surflight_descriptor_set`) and the
  shared-buffer dirty flags propagate to both (see `buffers_grew` in
  trace_gpu_vulkan.cc).
- This gets the "upload samples once" optimization (GPU_TODO.md smaller
  follow-ups) for free within the direct phase: today a 1M-sample batch
  uploads ~48 MB; sharing it between the entity/sun and surflight kernels
  halves the memcpy per phase.

Alternative (simpler, slower): separate queues + a global apply mutex around
both apply loops. Works, serializes applies, uploads samples twice. Fine as a
stepping stone; measure before keeping.

**Fallback granularity is soft here.** Unlike entity/sun lights (where a
single unsupported light poisons the whole source list), the CPU surflight
call is additive and order-commutative with the GPU entity/sun work — exactly
how the tree ships today. So "surflight sources unsupported" (styled VPL,
dirt) should only mean *keep calling CPU `LightFace_SurfaceLight(nullopt)`*,
NOT disable the GPU direct phase. Keep the two deferral decisions
independent in `DirectLightFace`.

### Validation playbook

Everything from `GPU_INDIRECT_HANDOFF.md` §3 applies (never byte-compare
`.lit` across runs; per-face compare via each run's own FACES-lump
`lightofs`; the technique is also recorded in the memory file
`gpu-light-branch-context.md`; a working `cmp_faces.py` lives in the session
scratchpad — ~90 lines, recreate if gone).

Test assets (both compiled into `/home/tzcnt/quake/id1/maps/`):

- **`cave_test.map`** (`/home/tzcnt/quake/working/`) is built for this port:
  2 non-sky radiosity templates (rescale=true path) + 1 sky template on
  `sky5_blu` (omnidirectional + surflightskydist path), 94 VPL points total;
  plus delay 1/2/5 + `_falloff` point lights, 66 suns (`_sunlight2`), and a
  `_shadow 1` func_wall. All GPU-supported today; no fallback messages.
- Isolation: `-sunlight 0 -sunlight2 0 -bounce 0` leaves point lights
  (near-exact) + direct surflights only — any new diff is this port.
- Coverage gaps in the test map itself: no `_surflight_atten` texinfo flag
  (all VPLs have atten=1) and no styled surflight — add a face with
  `_surflight_atten` and a `surflight_style` face to the map when porting
  those, or accept them as fallback-only.
- Accepted residual magnitudes on cave_test (`-extra4 -soft 4`): direct
  point-light-only CPU-vs-GPU is 11/9848 faces ≤ 4 bytes (shadow-boundary
  subsample flips, Embree-vs-HW-RT edge precision — one flipped ray moves a
  byte by ~contribution/16); with suns, 86 faces range 2–107; everything the
  bounce passes add on top is exactly ±1 byte; GPU runs are bit-identical
  run-to-run. Judge the surflight port against that bar: boundary-band diffs
  of a few bytes are the accepted class; whole-region proportional shifts are
  a formula bug (see GPU_INDIRECT_HANDOFF §4 "enum/constant drift" — the
  rescale and omnidirectional branches are exactly the kind of thing a test
  map can silently not exercise, which is why cave_test now does).

## 2. Inventory: everything else that stays on the CPU

### Hard fallbacks — GPU direct phase disabled for the whole run
(`GPU_DirectQueue_BuildSourcesLocked`, ltface.cc:2851; a log line names the
trigger)

| condition | porting notes | effort |
|---|---|---|
| `dirt_in_use` (any dirt/AO) | `Dirt_GetScaleFactor` with entity: per-entity on/off/scale/gain overrides + `lightsurf->nodirt` + `pow(occlusion, gain)` math. `sample.occlusion` is already uploaded per sample and the source struct has `flags`/`dirt` fields reserved. Also gates the indirect queue (entity=nullptr there → just the pow/clamp math + nodirt). | medium |
| entity `style != 0` | Per-style accumulation: group sources by style, one accum slot per (sample, style) or one dispatch per style; apply to `Lightmap_ForStyle(style)` (allocation happens CPU-side at apply, so that part is free). Styled lightmap count per face is small. | medium |
| entity `spotlight` | Cone math is ~6 lines (`GetLightValueWithAngle`, ltface.cc:907: dot vs `spotfalloff`/`spotfalloff2` interpolation) + 3 source fields (`spotvec`, two falloffs). Smallest remaining win. | small |
| entity `bleed` | `angle = fabs(angle)` like twosided, plus it skips the behind-plane face cull (`GPU_Direct_SourceReachesFace` already replicates that cull — add the bleed exemption there and a flag bit). | small |
| entity `projectedmip` | Projected-texture lights sample a mip texture per ray (`LightFace_SampleMipTex`, 4×4 matrix + texture fetch). Needs a texture upload + sampling in the kernel. | large |
| entity `LF_LOCALMIN` formula | It's a minlight mechanism (`LightFace_LocalMin` runs CPU-side in DirectLightFace after the GPU enqueue — ordering constraint documented there). Leave on CPU. | n/a |
| entity/sun channel masks != `CHANNEL_MASK_DEFAULT` | Couples with filtered-geometry semantics below. | large |
| sun `style != 0` / `suntexture` | Styled: same as styled entities. Suntexture needs the sun ray to report *which* sky face it hit → per-triangle face-index buffer in the BLAS + texture color lookup at apply or in-kernel. That face-index buffer is general infra (also needed for alpha-test fences). | medium |

### Backend-wide fallback — any filtered Embree geometry present
(`has_filtered_embree_geometry`, trace_gpu_vulkan.cc; logged at init)

If Embree's filter scene is non-empty, the whole run is CPU. As of
2026-07-30 this is decided at **decision time**: the three queue source
builders in ltface.cc check `gpu_light::has_filtered_geometry()` before any
face is enqueued, so no face ever skips its CPU loops on a promise a flush
would have to break (previously the backend refused the batch only at flush
time, silently dropping the queued faces' light — validated fixed via a
`_shadowself` cave_test variant: queues disable up front, output bit-identical
to CPU). The backend entry points still refuse such batches as a backstop.
The remaining (much smaller) version of that gap: a genuine mid-run Vulkan
error (device loss) still drops the in-flight batch's light — recovering
would mean re-lighting the dropped batch's faces on the CPU from the flush
path (the face records hold the lightsurf/lightmaps pointers; they would
also need per-face want_direct/want_surflight flags). Triggers (Embree_TraceInit classification,
trace_embree.cc:552): translucent water/glass (`alpha < 1`, Q2 trans flags),
fence textures (`{` prefix — alpha-test), `_switchableshadow` bmodels
(style-keyed shadow bits), non-default object channel masks. Porting is
per-feature and large: colored transmittance accumulation instead of boolean
occlusion, alpha-test needs texture sampling + the face-index buffer,
switchable shadows need per-ray style outputs. Water/glass transmittance is
the most common trigger in the wild and the sensible first step.

Related: plain-`_shadow 1` bmodels are NOT filtered — they land in the solid
set and the GPU BLAS (validated via cave_test's func_wall, +6 solid faces,
0 filtered). `_shadowself`/`_shadowworldonly` land in filtered.

### Soft/per-item fallbacks (correct, cheap, already graceful)

- `GPU_IndirectQueue_BuildSourcesLocked`: `dirt_in_use`, styled bounce VPLs
  (`-bouncestyled` + styled lightmaps). Falls back to CPU indirect only.
- Faces without `CHANNEL_MASK_DEFAULT` in `object_channel_mask`: both queues
  return handled-with-nothing, matching the CPU's early return.
- Generic `trace_occlusion_batch` (raystreams ≥ 262144 rays,
  trace_embree.hh): falls back when `shadow_mask != CHANNEL_MASK_DEFAULT`.

### CPU by design (not worth porting)

- `PostProcessLightFace` (ltface.cc:3602): minlight, surflight self-minlight
  (`minlight_scale`), `LightFace_AutoMin`, **negative lights/suns** — the
  antilight loops do trace rays via the generic raystream, so a map saturated
  with antilights would surface as a PostProcess hotspot; none observed yet.
- `MakeBounceLights` / `MakeRadiositySurfaceLights` (VPL construction,
  `bounce_color` harvest) — serial-ish, milliseconds.
- Sample-point/extents setup, `-extra4`/`-soft` supersampling layout,
  `Lightmap_Save`/style allocation — shared by both paths by construction.
- Lightgrid (`LightPoint_*`): per-point raystreams too small to batch on the
  queue model; large streams already route through the generic
  `trace_occlusion_batch` path when eligible.

## 3. Perf context (RTX 3070, this machine)

- cave_test.bsp `-bounce 3 -minlight 12 -soft 4 -extra4 -nopercent`:
  CPU 185.4 s → GPU 15.8 s. At `-bounce 0` (direct only, 66 suns + 94 VPL
  points): CPU 12.8 s → GPU 6.1 s — the GPU number still contains the CPU
  surflight loop, which is what this port removes.
- After this port, the remaining GPU-run wall-clock is dominated by sample
  upload memcpy and AddFace mutex serialization — see GPU_TODO.md #4
  (sharded enqueue) and the shared-samples design in §1, which addresses the
  upload half for the direct phase.
