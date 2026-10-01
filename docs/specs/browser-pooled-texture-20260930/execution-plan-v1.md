# Browser pooled-texture repair — execution plan v1

Owner requested review and application of obs-browser PR 531. Work directly on
the owner's OBS fork, starting at `af0f8b220`; browser baseline `3f0a2cdf`.
Keep OBS 32.2.2 and CEF 127/6533. No other suggested upgrade is in scope.

## Contract and design

On Windows CEF 6367+, accelerated paint must copy the callback-scoped pooled
texture into a persistent texture owned by OBS. Reuse that destination until
dimensions/format change; preserve the previous frame on open, allocation or
synchronization failure. Flush the copy before returning or releasing a keyed
mutex. Leave CPU paint, older Windows CEF, macOS and Linux paths unchanged.

The installed CEF headers explicitly prohibit caching or later access to the
pooled handle. PR 531 addresses that but copies even after mutex failure.
This fork's D3D11 acquire API returns -1 for a texture without a mutex, so the
upstream comment describing that as a no-op is incorrect here. Inspect the
native texture descriptor: synchronize only if a keyed mutex is present, and
use a nonblocking acquisition so a busy texture cannot stall the video thread.

Use a small graphics-only helper for the ownership/copy policy, called from the
real callback while its existing graphics lock is held. Behavior tests cover
producer reuse, retained frames on failure, destination reuse/resize/format
changes and copy/flush/release ordering. Run a separate real D3D11 probe before
calling the graphics path verified; neither mocks nor a build prove visible
tearing eliminated or an FPS improvement.

## Milestones

- [x] Inspect current callback, PR, installed CEF contracts and graphics backend.
- [x] Apply bounded Windows copy repair and focused regressions.
- [x] Observe focused tests and a real graphics probe; run full CTest/build.
- [x] Review diff, formatting and changed-file credential/size sweep.
- [x] Publish the browser commit to an owner fork and pin it in the parent fork.
- [x] Commit/push the parent and verify its remote hash; record the release receipt.
- [x] Prepare a hash-pinned replacement for obs-browser.dll and rollback copy.
- [x] Install only after OBS is closed and verify installed bytes.
- [ ] Review live browser-source visuals after the owner reopens OBS.

## Deployment boundaries

OBS PID 467596 is open at initial inspection. Do not close or restart the owner
session or replace a loaded module. Complete and stage the verified change;
installation must recheck that OBS is closed before copying. Preserve all user
configuration and the original installed module. A read-only GPU test may use
an isolated graphics device; no streaming, recording, video generation or
production scene edits. Record any pending activation/runtime gate honestly.

Source: https://github.com/obsproject/obs-browser/pull/531

## Progress

- Owner fork remote `feature/performance-optimizations` matches the initial HEAD.
- Browser baseline is upstream 2.26.9. Created the owner fork at
  https://github.com/Marssvoodoo/obs-browser for the separate repair branch.
- RelWithDebInfo obs-browser module builds. All eight CTest cases pass,
  including mock failure paths and the real NVIDIA RTX 5090 D3D11 probe.
- The negative control demonstrates that the original borrowed texture changes
  when the producer reuses it; the owned copy retains the previous pixels.
  Destination reuse across 24 frames, resize and keyed-mutex handoff/skip pass.
- The same GPU probe passes against the installed obs.dll/libobs-d3d11.dll.
- GPU fixture corrections were isolated to absolute module loading, Chromium's
  SHARED|NTHANDLE flags, matching staging format and producer completion before
  simulated callback entry. Earlier failed logs are preserved with final logs
  under `L:\Coding\_scratch\obs-browser-texture-20260930`.
- These checks verify resource ownership and readback. They do not prove
  visible browser tearing eliminated, GPU command completion at callback exit,
  or an FPS improvement. Live browser-source visual QA remains after activation.
- Browser commit `295e9816efcec0c4e655a2c5dede0a548e92e3e6` was pushed to
  `Marssvoodoo/obs-browser`, branch `voodoo/cef-pooled-texture-fix`; the remote
  hash matches. The parent submodule URL now points to that owner fork.
- Staged only obs-browser.dll (2,910,720 bytes) and its PDB. Release manifest:
  `L:\Coding\_obs_releases\20260930-browser-pooled-texture\manifest.json`, SHA256
  `363C5233471978A233CE24E542659D4AC3F96865DD45AA0030D3A88A603AA1E7`.
  Exact original files are backed up under
  `L:\Coding\_obs_install_backup\obs-browser-pre-pooled-texture-20260930`.
- At staging, `tools/obs-install-browser-pooled-texture.ps1` syntax and read-only
  preflight passed. The OBS-open guard returned 20 before writes; installed
  original hashes were unchanged. Run elevated after closing OBS; `-Rollback` restores the
  same two original files. No CEF, configuration, scenes or other plugins change.
- Source repair parent commit `cc3f3ee32c6986b141345ff23a9222a4648eea6c`
  was pushed and its remote hash verified. The immutable staging receipt remains
  a snapshot of that commit; this document records the later installation.
- Owner closed OBS and authorized installation. Elevated installer PID 529576
  exited 0 at 2026-10-01 01:21:05 UTC (Sep30 20:21 CDT). Installed DLL/PDB hashes
  match the pinned manifest. Original rollback, obs.dll, libobs-d3d11.dll and
  libcef.dll hashes remain verified. No OBS process was running after install.
- Installation proof:
  `L:\Coding\_obs_releases\20260930-browser-pooled-texture\installation-verification-20261001.json`,
  SHA256 `F78BD371ABA8E6F1F396ACA101C2D3169FEF934DF46D3CC15F3E56EC8B685D36`.
  OBS was not relaunched. Live animated browser-source visual QA remains pending;
  installation does not establish visible tearing eliminated or an FPS gain.
