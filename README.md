# OBS Studio Claude Code Enhanced

A performance-optimized and security-hardened fork of [OBS Studio](https://obsproject.com), based on upstream version **32.1.2**.

---

## What's Changed

### Performance Optimizations

- **SIMD-Optimized Audio Pipeline** — SSE2/AVX accelerated audio mixing with non-temporal stores for large buffers, reducing CPU cache pressure during encoding
- **Multi-Threaded Audio Rendering** — Lock-free thread pool for parallel audio source rendering with automatic scaling based on source count
- **Audio Memory Pooling** — Pre-allocated buffer pools for audio render and output buffers, eliminating per-frame allocations
- **Optimized Video Frame Copy** — SIMD-accelerated `copy_video_plane_optimized()` with prefetching and non-temporal stores for planes >256KB
- **Canvas Enable/Disable** — Explicit API to disable inactive canvases, preventing unnecessary rendering work
- **Stop Event Prioritization** — WaitForMultipleObjects now prioritizes stop events over processing, improving shutdown responsiveness

### Security Hardening

- **Integer Overflow Guards** — Overflow checks on all buffer size calculations in audio pipeline capacity doubling, video frame plane sizing, GPU conversion width multiplications, and linesize computations
- **Thread Safety** — Atomic operations for shared thread pool state (`num_jobs`), NULL guards against spurious wakeups in worker threads
- **SIMD Copy Validation** — Stride and dimension validation before all SIMD memory copy operations to prevent out-of-bounds access
- **Plugin Path Hardening** — Replaced `GetEnvironmentVariableA("ProgramFiles")` with `SHGetKnownFolderPath(FOLDERID_ProgramFiles)` to prevent environment variable manipulation attacks
- **Path Traversal Protection** — Validation on `OBS_PLUGINS_PATH`, `OBS_PLUGINS_DATA_PATH`, and `OBS_PERF_EXPORT_CSV` environment variables, rejecting `..` traversal sequences and UNC paths
- **Pool Destruction Safety** — Documented and verified correct ordering of source teardown before audio pool destruction during shutdown

### Cherry-Picked Upstream Fixes

**From v32.1.0-cce1:**

- **NVENC Resource Destruction** — Fix resource destruction order (PR #13105)
- **Integer Overflow Fixes** — Fix integer overflow in D3D11, OpenGL, and image file texture size calculations (PR #13184)
- **Windows IPC Pipe** — Adjustments for windows ipc-pipe handling (PR #13184)
- **Plugin Manager Safe Mode** — Improved safe mode behavior for plugin manager
- **Scene List Events** — Only send frontend event when scene list actually changes
- **Process Pipe FD Management** — Fix fd double-close and leak in POSIX process pipes
- **obs-websocket 5.7.2** — Version bump

**Added in v32.1.2-cce2:**

- **Crash Handler Hardening** — Disallow overwriting the libobs crash handler (libobs stability)
- **Windows Process Mitigation** — Apply process mitigation policies (exploit hardening)
- **Canvas Video Reset/Restore** — Follow-up fix to the canvas enable/disable cluster
- **obs-websocket 5.7.3** — Latest websocket plugin version
- **Qt Safety** — Don't store `QT_TO_UTF8` to `std::string` (use-after-free hazard)
- **System Theme Fix** — Checked control button color not changing in System theme
- **Frontend Cleanups** — themeWatcher warning, OBSUpdate includes, nested menu styling, About contributing link
- **obs-vst** — Migrated to v2 of `obs_properties_add_button`

**Added in v32.1.2-cce3:**

- **GPU stats in Stats panel (NVIDIA)** — Three new rows surface live GPU telemetry via NVML:
  - **GPU Usage** — current utilization percentage
  - **VRAM** — used / total in GB; turns yellow above 85% and red above 95%
  - **GPU Temperature** — °C reading from the on-board sensor; yellow above 80 °C, red above 85 °C

  NVML is dynamic-loaded (`nvml.dll` on Windows, `libnvidia-ml.so.1` on Linux) so the binary stays linkage-free; non-NVIDIA systems and macOS show `—` instead of failing.

**Added in v32.1.2-cce4:**

- **Customizable Stats panel** — A new **Configure…** button next to Reset/Close opens a dialog with a checkbox per row. Hide any combination of CPU, Disk Space, Time Until Disk Full, Memory, GPU Usage, VRAM, GPU Temperature, FPS, Average Render Time, Missed Frames, or Skipped Frames. Selections persist under `[Stats]/show_*` in the user config and apply immediately on accept. Defaults to all-visible so existing setups are unchanged.

**Added in v32.1.2-cce5 (stability/correctness):**

- **Parallel audio render fixes** — Resolved a `parallel_render_pending` latch that was re-armed without a matching clear (could stall `obs_source_filter_add`/`remove`) and a mid-tick double-render window. The audio thread pool now waits for every woken worker to leave the batch before the per-tick job array is reused, closing a use-after-free.
- **Reverted "Skip Unused Source Ticking"** — it could freeze deferred source updates and async/media frame timing for off-screen sources. All non-removed sources are ticked again (matching upstream); this also removes a lock-ordering risk from the per-tick marking pass.
- **GPU conversion correctness** — Unimplemented color formats are left untouched (upstream behavior) instead of a generic copy that could corrupt/truncate output.
- **Canvas NULL-guard** — Restored the NULL-`ovi` guard in `obs_canvas_reset_video_internal`.
- **Windows mitigation relaxed** — No longer forces image relocation (`EnableForceRelocateImages`/`DisallowStrippedImages`), which could block legitimate stripped plugin DLLs; bottom-up + high-entropy ASLR remain on.
- **Frontend / cleanup** — Perf-CSV writer joins its worker on teardown (avoids `std::terminate` on abnormal exit); Stats label sizing order fixed; minor false-sharing padding and dead-code cleanups.

### Plugins & Tools

- **In-Game Stats Overlay Plugin** (`obs-overlay`) — Real-time performance stats overlay with D3D11 colorspace conversion support
- **Program Files Plugin Discovery** — Automatically discovers third-party plugins from `C:\Program Files\obs-studio\obs-plugins\64bit\` with module deduplication
- **Performance Monitoring Tools** — PowerShell scripts for automated benchmarking (`obs-run-scenarios.ps1`) and live performance monitoring (`obs-perf-monitor.ps1`)
- **CSV Performance Export** — Set `OBS_PERF_EXPORT_CSV` environment variable to continuously export performance metrics during sessions

---

## Files Modified

| Area | Files |
|------|-------|
| Audio Pipeline | `libobs/obs-audio.c`, `libobs/obs-audio-optimized.c`, `libobs/obs-audio-threaded.c`, `libobs/obs-audio-threaded.h` |
| Video Pipeline | `libobs/obs-video.c`, `libobs/media-io/video-frame.c`, `libobs/media-io/video-frame.h` |
| Core | `libobs/obs.c`, `libobs/obs.h`, `libobs/obs-internal.h` |
| Frontend | `frontend/widgets/OBSBasic.cpp` |
| Plugins | `plugins/obs-overlay/` |
| Tools | `tools/obs-perf-monitor.ps1`, `tools/obs-run-scenarios.ps1` |

## Building

```bash
cmake --build "build" --config Release --parallel 8
```

Output binary: `build/rundir/Release/bin/64bit/obs64.exe`

## Base Version

Based on [obsproject/obs-studio](https://github.com/obsproject/obs-studio) tag `32.1.2` (upstream master).

## License

GNU General Public License v2 (or any later version) — same as upstream OBS Studio. See [COPYING](COPYING) for details.
