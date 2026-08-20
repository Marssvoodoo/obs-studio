# Voodoo OBS Studio 32.2.2-voodoo1

This maintenance release moves the production fork to the exact OBS Studio
`32.2.2` foundation while preserving its custom mixer, routing, VST2/VST3,
workspace, Draw Dock, Restream, performance, and security features.

## Upstream maintenance

- Integrated the official 32.2.2 release ancestry and version metadata.
- Preserved the Windows first-start plug-in loading fix. The fork already
  carried the patch-equivalent change that removes the inherited
  `PreferSystem32Images` policy before the official hotfix release.
- Included the official macOS 12 compatibility block and current release CI
  metadata without importing unrelated unreleased Qt refactors.

## Windows DLL resolution

- OBS and its helper processes continue to use explicit default DLL
  directories instead of preferring system-installed copies over packaged
  dependencies.
- DEP, bottom-up and high-entropy ASLR, extension-point protection, strict
  debug handle checks, absolute game-capture paths, and font blocking remain
  enabled without rejecting legitimate stripped third-party plug-ins.

## Fork features retained

- Role-aware mixer targets, Program Output loudness and true-peak metering,
  six sends, monitor buses, and source-aware bounded audio workers.
- Managed fail-closed VST2/VST3 discovery and hosting.
- Movable Preview, custom layouts, Draw Dock, Restream recovery, vertical
  canvas support, and the Ignants Voodoo theme.
- Validated SIMD copy paths, cache and lifetime hardening, GPU telemetry, and
  registered fork regression tests.

## Compatibility

- OBS 32.2 requires NVIDIA driver 570 or later for the NVIDIA SDK 13 encoder
  stack.
- Windows builds use version `32.2.2-perf`; Twitch Enhanced Broadcasting still
  receives canonical semantic version `32.2.2`.

## Local validation

- The full Windows Release build completed and all three registered CTest
  targets passed.
- The installed executable reports 32.2.2 and matches the verified build's
  SHA-256. VST2/VST3 modules and both scanner executables are installed.
- A minimized, non-streaming launch reached `Startup complete`, loaded VST2 and
  VST3, and started neither streaming nor recording.
- A normal user-driven close remains required before creating the public tag.
  The automated hidden-window close was not accepted; a follow-up harness that
  broadcast `WM_CLOSE` to every browser dock was invalid and produced a
  harness-induced `obs-browser.dll` crash, so it is not release evidence.
