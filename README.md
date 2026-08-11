<div align="center">

# Voodoo OBS Studio

### A production-focused OBS fork for live audio, flexible workspaces, and resilient plug-in hosting

[![Base](https://img.shields.io/badge/OBS_Base-32.2.1-c1121f?style=for-the-badge)](https://github.com/obsproject/obs-studio/releases/tag/32.2.1)
[![Windows](https://img.shields.io/badge/Windows-x64_%7C_arm64-202020?style=for-the-badge&logo=windows11)](#downloads)
[![VST](https://img.shields.io/badge/Audio-VST2_%2B_VST3-c1121f?style=for-the-badge)](plugins/obs-vst3/README.md)
[![Built with](https://img.shields.io/badge/Built_with-ChatGPT_Sol_5.6_%2B_Fable-c1121f?style=for-the-badge)](ACKNOWLEDGEMENTS.md)
[![License](https://img.shields.io/badge/License-GPL--2.0-202020?style=for-the-badge)](COPYING)

[Download releases](https://github.com/Marssvoodoo/obs-studio/releases) ·
[Mixer and interface guide](INTERFACE_CUSTOMIZATION.md) ·
[VST3 guide](plugins/obs-vst3/README.md) ·
[Release guide](RELEASES.md)

</div>

> [!IMPORTANT]
> Voodoo OBS Studio is a community fork and is not an official OBS Project
> release. It keeps OBS scene collections and profiles compatible while adding
> production tools that are not available in standard OBS.

## Built for a live production desk

| Production audio | Plug-in control | Flexible workspace | Stream reliability |
| --- | --- | --- | --- |
| Role-aware peak targets, LUFS, true peak, six sends, and monitor buses | Managed VST2/VST3 scans with stop, rescan, failure history, and isolated discovery workers | Movable Preview, stable dock sizing, custom layouts, and the Ignants Voodoo 2.0 theme | Restream dock recovery, safer shutdown ordering, hardened paths, and bounded caches |

## Audio that explains what it is measuring

### DAW-style mixer strips

- Compact channel strips use fixed-width readouts so live meter updates do not
  resize the mixer or adjacent docks.
- Each source can be assigned one of 17 practical roles, including stream mic,
  party chat, console chat, game, music, alerts, soundboard, dialogue, yells,
  impacts, ambience, and voiceover.
- The peak guide reports **LOW**, **GOOD**, **HOT**, **CLIP**, **MUTED**, or
  **UNASSIGNED** against the selected role instead of applying one generic
  target to every source.
- Automatic role detection is available as a starting point, but every role can
  be selected explicitly from the channel menu.

### Program Output metering

The fixed Program Output strip measures the configured streaming mix and shows:

- momentary, short-term, and integrated loudness;
- maximum true peak in dBTP;
- per-channel RMS and sample peak;
- the active stream mix or track;
- a reset control for the integrated and maximum measurements.

The meter follows the BS.1770/EBU measurement model and has been calibrated with
known test tones and true-peak vectors. It is intentionally read-only: it does
not change gain, insert a limiter, or alter encoded audio.

### Sends and monitoring

- All six OBS output routes have independent per-source send levels.
- Sources can monitor any complete output bus through the configured monitoring
  device.
- Send values persist with the scene collection and use lock-free snapshots in
  the render path.
- The default 0.0 dB send state matches normal OBS behavior.

## Managed VST2 and VST3 effects

**Tools > Plugin Manager > Audio Plug-ins** provides a stationary scan view with:

- separate VST2 and VST3 scans;
- the current search location and current plug-in path;
- Stop, Scan, and Rescan controls;
- passed, failed, and skipped results with reasons;
- an option to skip previously failed plug-ins;
- durable partial results when a scan is stopped;
- one supervised worker per candidate, a 15-second timeout, a 768 MiB memory
  cap, and kill-on-close containment.

The Windows VST3 filter supports effect processing, saved component/controller
state, native editor windows, latency reporting, restart requests, parameter
changes, and an optional OBS-source sidechain. Discovery is isolated; a selected
effect still runs as trusted native code inside OBS. See the
[VST3 technical guide](plugins/obs-vst3/README.md) before using third-party
effects in a live show.

## Workspaces that stay where they are placed

- Preview and Studio Mode live in the movable **Stream Preview / Program** dock.
- Side-column resize drags hold the opposite column steady.
- Browser minimum sizes constrain browser content rather than forcing the whole
  dock column to jump.
- **Balanced 6 | 6 | 6**, **Asymmetric 4 | 2**, and standard layouts are built
  in.
- Custom layouts can be saved, applied, renamed, and deleted.
- Invalid saved layouts fail closed and restore the prior workspace.
- Restream Chat, Stream Information, and Channels are recovered without
  creating duplicate docks.

The included **Ignants Voodoo 2.0** theme uses a charcoal, black, white, amber,
green, and deep-red palette across menus, tabs, docks, mixer channels, faders,
buttons, meters, and status surfaces.

## Performance and safety

- SIMD audio mixing and video-plane copies use validated dimensions and strides.
- Parallel audio rendering uses a bounded worker pool and waits for every active
  worker before reusing a batch.
- Audio buffer pooling removes recurring allocations from hot render paths.
- Inactive canvases can be disabled instead of rendering unnecessarily.
- Stop events are prioritized during Windows processing and shutdown.
- Integer-overflow guards cover audio, video, GPU conversion, and texture-size
  calculations.
- Plug-in and export environment paths reject traversal and unsafe UNC paths.
- Windows process mitigations retain bottom-up and high-entropy ASLR without
  blocking legitimate stripped plug-ins.
- NVIDIA systems can show GPU use, VRAM, and temperature in a configurable Stats
  panel.

## Upstream 32.2 foundation

The current branch is merged with the exact upstream `32.2.1` tag. It keeps
the fork features above while gaining the 32.2 Add Source browser, improved FPS
selector, missing-file filter support, custom source icons, WebP slideshow
directory support, frontend copy/paste APIs, SDR-to-HDR filtering, multitrack
dynamic bitrate, reliable game-capture hook updates, better hook diagnostics,
and upstream performance, security, and stability fixes.

## Downloads

Fork release tags use the format
`<base-version>-voodoo<revision>`, for example
`32.2.1-voodoo1`. A tag build creates the same platform and debug-symbol
download categories as upstream OBS:

| Platform | Download |
| --- | --- |
| macOS Apple silicon | `OBS-Studio-<version>-macOS-Apple.dmg` |
| macOS Apple silicon symbols | `OBS-Studio-<version>-macOS-Apple-dSYMs.tar.xz` |
| macOS Intel | `OBS-Studio-<version>-macOS-Intel.dmg` |
| macOS Intel symbols | `OBS-Studio-<version>-macOS-Intel-dSYMs.tar.xz` |
| Ubuntu 24.04 x86_64 | `OBS-Studio-<version>-Ubuntu-24.04-x86_64.deb` |
| Ubuntu 24.04 debug symbols | `OBS-Studio-<version>-Ubuntu-24.04-x86_64-dbsym.ddeb` |
| Ubuntu 26.04 x86_64 | `OBS-Studio-<version>-Ubuntu-26.04-x86_64.deb` |
| Ubuntu 26.04 debug symbols | `OBS-Studio-<version>-Ubuntu-26.04-x86_64-dbsym.ddeb` |
| Source archive | `OBS-Studio-<version>-Sources.tar.gz` |
| Windows x64 portable | `OBS-Studio-<version>-Windows-x64.zip` |
| Windows x64 installer | `OBS-Studio-<version>-Windows-x64-Installer.exe` |
| Windows x64 symbols | `OBS-Studio-<version>-Windows-x64-PDBs.zip` |
| Windows arm64 portable | `OBS-Studio-<version>-Windows-arm64.zip` |
| Windows arm64 symbols | `OBS-Studio-<version>-Windows-arm64-PDBs.zip` |

Fork Windows packages and installers are unsigned. Every draft release includes
SHA-256 checksums. macOS signing and notarization depend on credentials configured
by the fork owner. VST3 hosting is currently a Windows-only feature.

## Build on Windows

The accepted VST3 SDK revision is `v3.8.0_build_66` at commit
`9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`.

```powershell
git clone --recursive https://github.com/Marssvoodoo/obs-studio.git
Set-Location obs-studio
git clone --recursive --branch v3.8.0_build_66 `
  https://github.com/steinbergmedia/vst3sdk.git .deps/vst3sdk
cmake --preset windows-x64 -DENABLE_VST3=ON
cmake --build --preset windows-x64 --config Release --parallel
```

Release output is generated through CPack and the pinned packaging workflow.
See [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for local environment notes
and [RELEASES.md](RELEASES.md) for the complete release process.

## Documentation

- [Interface customization and mixer calibration](INTERFACE_CUSTOMIZATION.md)
- [Windows VST3 host architecture and acceptance](plugins/obs-vst3/README.md)
- [Release downloads and verification](RELEASES.md)
- [Credits and contributor acknowledgements](ACKNOWLEDGEMENTS.md)
- [Performance test results](PERFORMANCE_TEST_RESULTS.md)
- [Optimization implementation notes](OPTIMIZATION_IMPLEMENTATION_GUIDE.md)

## Credits and acknowledgements

Voodoo OBS Studio is directed and maintained by
[@Marssvoodoo](https://github.com/Marssvoodoo), with development assistance
from **ChatGPT Sol 5.6** and **Fable**. The fork preserves direct mentions for
the people and projects whose work was incorporated. See
[ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) for the complete list.

## License and upstream

Voodoo OBS Studio is licensed under the GNU General Public License v2 or later,
the same license used by OBS Studio. See [COPYING](COPYING). OBS Studio is
developed by the [OBS Project](https://github.com/obsproject/obs-studio); this
fork is maintained and released independently.
