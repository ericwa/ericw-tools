# GPU light backend — remaining performance work

## Known remaining accuracy delta (accepted)

After the sky-geometry / cull-parity / formula-parity fixes, GPU vs CPU direct
lighting on cave.bsp differs on 46/3418 faces (0.2% of bytes, max byte delta
51, avg ~3): all on faces perpendicular to the sun, GPU slightly darker. These
are sun rays traveling parallel to a wall at the ~1 unit sample offset for long
distances — grazing-incidence hits where Embree and hardware ray tracing
legitimately resolve edge/crack intersections differently. Point-light-only
output is bit-identical to the CPU path.

Follow-ups to the persistent-buffer work in `light/trace_gpu_vulkan.cc`
(items #1 and #2 are done: end-of-phase flush ordering fix in `light/light.cc`,
persistent mapped buffers + HOST_CACHED readback in `light/trace_gpu_vulkan.cc`).

Current state: ~97 ms per ~1M-sample flush on an RTX 3070 (~0.8 s direct phase
on cave.bsp). Remaining cost is roughly: ~55 MB upload memcpy, PCIe traffic
during dispatch, synchronous `vkQueueWaitIdle`, and all of it serializes the
lighting worker threads because the flush runs under `g_gpu_direct_queue_mutex`.

## #3 — Dispatch outside the queue mutex + double-buffering

Problem: `GPU_DirectQueue_FlushLocked` (light/ltface.cc) runs the whole GPU
round-trip while holding `g_gpu_direct_queue_mutex`, so every worker thread
calling `GPU_DirectQueue_AddFace` blocks for the full flush. The direct phase
degenerates to the sum of flush times.

Plan:
- In `AddFace`, when the threshold is hit, *swap* the queue vectors
  (samples/sources refs/face ranges/records) into a local "batch" under the
  mutex, then release the mutex before dispatching. Other workers keep filling
  the fresh queue while the GPU traces.
- Double-buffer the persistent Vulkan buffers (two sets + two fences) so batch
  N+1 can upload while batch N traces; replace `vkQueueWaitIdle` with
  `vkWaitForFences` on the batch's fence.
- Applying results to lightmaps (`Lightmap_ForStyle`/`Lightmap_Save` loop) can
  also happen outside the queue mutex, but see the race note below.

Race note (pre-existing, must be addressed as part of this redesign): the flush
applies GPU accum into `lightmap->samples[i].color` for faces whose own
`DirectLightFace` call may still be running CPU-side additive passes (surface
lights, negative lights) on the *same* samples. Today the only reason this
doesn't corrupt output on simple maps is that nothing else writes style-0
lightmaps concurrently (no surface lights in the test map). Options: defer the
CPU-side additive passes for GPU-queued faces until after their accum is
applied, or add a per-face completion handshake, or have the owning thread
apply its own face's accum.

## #4 — Stop serializing AddFace on one global mutex

Problem: every face's `GPU_DirectQueue_AddFace` takes the single global queue
mutex and copies all of the face's samples inside the lock, so the enqueue side
of the direct phase is effectively single-threaded even between flushes.

Plan:
- Shard the queue per worker thread (thread_local batch buffers), each shard
  flushing independently when it reaches threshold/phase-end; or
- Reserve ranges with an atomic cursor into a fixed-capacity batch and copy
  samples outside the lock, taking the mutex only to trigger the flush/swap.
- The GPU submission itself still needs serialization (single queue/command
  buffer), but enqueue and result application should be lock-free per face.

## Smaller follow-ups

- `pick_device`: log the chosen `VkPhysicalDeviceProperties::deviceName` and
  prefer `PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`; today the first ray-query-capable
  device wins by enumeration order (works on this machine, luck elsewhere).
- Upload memcpys still target write-combined memory; if profiling shows the
  ~55 MB upload matters after #3 (it overlaps GPU work then), consider
  HOST_CACHED for uploads too, or device-local buffers with a staging ring.
- `trace_occlusion_batch` still uses the old create/destroy-per-batch pattern;
  port it to persistent buffers if it ever gets used on a hot path.
- Indirect/bounce lighting is the dominant cost on light-count-poor maps
  (~26 s of the ~28 s cave.bsp run) and is untouched by all of the above; a GPU
  path for `IndirectLightFace` ray batches is where the next big win lives.
