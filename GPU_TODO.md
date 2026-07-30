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

## #3 — Dispatch outside the queue mutex + double-buffering — DONE

Implemented: `GPU_DirectQueue_AddFace` swaps the queue into a `gpu_direct_batch_t`
under the mutex and dispatches/applies it after releasing the lock; the Vulkan
backend has two `direct_slot_t`s (own buffers, command pool, descriptor set,
fence), submits under the global mutex only, and waits on per-batch fences.

The flush-vs-CPU-writes race was resolved by restructuring `DirectLightFace`:
`GPU_DirectQueue_WillHandleFace` decides up front (so the CPU entity/sun loops
are skipped), and the face is enqueued only at the *end* of the direct-phase
CPU work — every queued face has finished its CPU-side lightmap writes, so a
flush on any thread can apply results safely. If the GPU gets disabled mid-run,
the face falls back to the CPU loops at the enqueue point.

Also fixed while here: negative (anti-)lights are excluded from GPU sources —
they are applied by `PostProcessLightFace` on the CPU, so queueing them would
double-apply.

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
  (~22.5 s of the ~23 s cave.bsp run) and is untouched by all of the above; a
  GPU path for `IndirectLightFace` is where the next big win lives. Detailed
  design + validation playbook: see `GPU_INDIRECT_HANDOFF.md`.
