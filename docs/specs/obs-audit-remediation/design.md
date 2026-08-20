# Design

## Decisions

1. Invalidate the audio graph inside active-child add/remove operations. These
   are the common topology mutation boundary and also cover reference-count
   changes that do not emit a global activation signal.
2. Treat the scanner's passed cache as the only authority for VST loading.
   Missing, failed, skipped, malformed, or out-of-root entries fail closed.
3. Reuse the thread-pool `queue_cap` argument as the expected batch size and
   derive workers from four jobs per worker, capped by logical cores and 16.
4. Keep Program Output UI construction cheap and explicitly enable or disable
   its timer/raw callback from AudioMixer visibility state.
5. Use a temporary packed buffer for the rare overlapping plane-copy fallback;
   the normal SIMD/non-overlap path is unchanged.
6. Retry overlapped result waits after timeout while retaining stack and
   OVERLAPPED lifetime; only completed/cancelled I/O permits thread exit.
7. Consolidate Windows mitigations into one call site without forced relocation
   or stripped-image rejection.
8. Resolve OBS's selected DXGI adapter LUID, match it through CUDA's documented
   `cuDeviceGetLuid`, then resolve the same PCI device through NVML. If any link
   is unavailable, display unavailable data rather than another GPU's data.

## Risks and rollback

- VST scenes without a passed scan will bypass the plug-in until the user runs
  Plugin Manager. This is intentional fail-closed behavior.
- Source-aware worker creation may reduce parallelism when a scene grows after
  pool creation, but remains correct and avoids gaming-hostile oversubscription.
- Deployment uses the existing full pre-32.2.1 backup plus a new timestamped
  predeploy backup. Rolling back restores the prior Program Files tree.
