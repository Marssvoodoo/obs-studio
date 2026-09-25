# Upstream backports and local installation — 2026-09-24

Owner request: cherry-pick the upstream OBS fixes that were missing from the
32.2.2-perf fork and add them. Upstream's `release/32.2` branch has nothing after
32.2.2, so the candidates came from `upstream/master` (heading for 33.0).

## Decisions

Each candidate was analysed for bug presence in the fork, prerequisites and
fork interaction, then checked by three independent skeptics (none refuted).

| Upstream | Fix | Result |
|---|---|---|
| 4315559a0 | libobs: Fix deadlock on executing tasks | Cherry-picked as `a41c395af`. The fork's monitoring-deduplication task can destroy a source inside an audio task, so the held-mutex executor was a real deadlock risk here. |
| 36d134ee9 | frontend: Fix dangling pointer in Audio Mixer logging | Cherry-picked as `3b5debba4`. |
| dfad5c86f | frontend: Ensure pending profile changes are saved | Cherry-picked as `b882989b6`; the three-way merge placed the save after the fork's Restream dock sync. |
| f89ad4bee | obs-outputs: Fix file splitting overshooting when b-frames are used | Cherry-picked as `1be771038` **with a fork adaptation**: upstream's exact-match window (`llabs(max_time - runtime) < 1 ms`) never splits when the keyframe interval does not divide the split duration, because `should_split` only runs on keyframes. The fork keeps the 1 ms tolerance as a one-sided check and resets `received_first_keyframe` on every start. |
| 6785d7d3a | libobs: Fix monitoring hotkey UI updates | **Skipped.** The bug lives in the monitoring-hotkey feature (646836a31, 22e2b7371) that exists only in 33.0; 32.2.2 and the fork do not have it. |

Follow-ups from the post-merge review (every finding survived two skeptics):

- `e983f95bd` places Hybrid MP4/MOV chapters from the first keyframe's PTS
  (`chapter_base_usec`). The backported start-time correction uses the DTS, which
  would have made every chapter late by the encoder reorder delay (two frames with
  NVENC defaults); split files already had that offset. Three skeptics traced the
  muxer timeline, split lifecycle and threading and could not refute the fix.
- `ee0857924` and `e983f95bd` add source contracts pinning all four backports and
  the adaptation. A mutation run confirmed each contract fails when its code is
  removed and the tree passes unmutated.

Noted, not changed (pre-existing upstream behaviour): `mp4_output_actual_stop`
removes the packet callback with a NULL param, so callbacks accumulate across
restarts of a reused output; the split buffer is not cleared on stop; upstream
575c77f19 (keep the last packet when flushing a Hybrid file) is not in the fork.

## Build and tests

`scripts/build-windows-x64.bat` (RelWithDebInfo): 0 compiler warnings, 6/6 CTest.

## Installation

- Release `L:/Coding/_obs_releases/20260924-upstream-backports/`, source commit
  `e983f95bdfc1c74ba7d8c4f0f917e136fdc89ac8`, payload 2,185 files (manifest SHA-256
  `F8CF1610BCF6436106F26300136F09B9FD3AF13E666EB4D6A2759EE7F695D619`).
- Full rollback backup of the previous installation (12,111 files, manifest SHA-256
  `FA5F0B8D958D4A547CA26457AC7261F6161EC77B532B2CDA6833934DA88876C6`):
  `L:/Coding/_obs_install_backup/obs-studio-pre-upstream-backports-20260924/`.
- Guarded installer `L:/Coding/_turnover/obs-deploy-upstream-backports-20260924.ps1`
  (derived from the 2026-09-05 installer; only roots, commit and manifest hashes
  changed). Verify-only pass, then the elevated run on 2026-09-24 23:48 CDT with
  OBS closed; robocopy exit 3; installer inventory check passed.
- Independent readback: 2,185/2,185 payload files match in
  `C:/Program Files/obs-studio`, 12,111 installed files (extras preserved),
  installed `obs64.exe` SHA-256 `887E334F18253885…` equals the staged build, and
  `obs64.exe --version` exits 0 with `OBS Studio - 32.2.2-perf` and no stderr.

OBS was left closed. A normal-profile start, streaming and Hybrid MP4 recording
were not exercised in this session. Rollback, only on request and with OBS closed:
copy the backup folder over the installation.
