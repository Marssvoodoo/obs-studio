# Voodoo OBS Studio 32.2.0-rc2-voodoo1

This release moves the production fork to the exact OBS Studio `32.2.0-rc2`
foundation and brings the complete audio, workspace, plug-in, Restream, and
performance feature set into one tested build.

## Production audio

- Added DAW-style mixer strips with fixed-width live readouts.
- Added 17 selectable peak-guide roles for voice, chat, game, music, alerts,
  sound effects, ambience, and other common stream sources.
- Added momentary, short-term, and integrated LUFS plus maximum dBTP to the
  Program Output strip.
- Added six per-source send levels and selectable complete-bus monitoring.
- Added reset-on-session loudness history while keeping the meter read-only.

## VST2 and VST3 management

- Added a stationary Audio Plug-ins page under Tools > Plugin Manager.
- Added visible search locations, current-candidate progress, Stop, Scan, and
  Rescan All controls.
- Added durable passed, failed, and skipped results with skip-failed behavior.
- Isolated every discovery candidate in a supervised worker with a 15-second
  timeout, 768 MiB memory cap, and kill-on-close containment.
- Added a Windows VST3 filter with state restoration, native editors, latency,
  restart handling, parameters, and optional source sidechains.
- Pinned the VST3 SDK revision in Windows release CI so published packages cannot
  silently omit the module.

## Interface and docks

- Moved Preview and Studio Mode into a native movable dock.
- Removed unused preview space and stabilized left/right column resizing.
- Added Standard, Balanced 6 | 6 | 6, and Asymmetric 4 | 2 starting layouts.
- Added save, apply, rename, and delete controls for custom layouts.
- Added bounded restore with rollback for invalid saved layouts.
- Expanded Ignants Voodoo 2.0 across menus, docks, mixer surfaces, meters,
  buttons, tabs, and status states.

## Restream and shutdown

- Restored automatic Restream Chat, Stream Information, and Channels docks.
- Prevented duplicate recovered docks across saved layouts and cold starts.
- Preserved the Restream Vertical canvas and scene collection path.
- Released monitoring sources before OBS context teardown to prevent the
  accepted Windows shutdown crash.

## Performance and safety

- Retained validated SIMD audio/video copies, bounded parallel audio rendering,
  audio buffer pooling, inactive-canvas control, and stop-event prioritization.
- Retained integer-overflow checks, path traversal protection, bounded scan
  caches, and safer Windows process mitigations.
- Retained NVIDIA GPU usage, VRAM, and temperature rows plus configurable Stats
  visibility.

## OBS 32.2 additions

- New Add Source browser.
- Improved FPS selector.
- Missing-file filter support.
- Custom source icons.
- WebP slideshow directory support.
- Frontend copy and paste APIs.
- SDR-to-HDR filter.
- Dynamic bitrate support for multitrack video.
- Upstream performance, security, and stability corrections through RC2.

## Packaging

The release workflow now creates the same 14 platform and debug-symbol download
categories as upstream OBS. Fork Windows portable builds and the x64 installer
are unsigned, and every release includes SHA-256 checksums.
