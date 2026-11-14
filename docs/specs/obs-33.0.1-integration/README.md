# OBS 33.0.1 integration and local installation — 2026-11-14

Owner request: check upstream, take the 33.0.1 update into the fork, install it locally for testing.

## Merge

Branch `sync/33.0.1` (from `feature/performance-optimizations`), upstream tag `33.0.1` merged as
`1100f5e15`. A commit-by-commit rebase was started and abandoned: 40+ fork commits touch files
upstream rewrote, several of them the same file ten times. One merge left 15 conflicted paths
plus the `obs-browser` submodule.

| Area | Resolution |
|---|---|
| obs-browser | Upstream 2.27.5 (CEF 150) still keeps CEF's pooled texture handle after the paint callback. Pooled-texture repair re-ported onto 2.27.5 as submodule commit `e8f7363`. |
| Plugin loading | Upstream moved module path setup into `frontend/plugin-manager/PluginModuleLoader_*`. Unsafe-path rejection and the Program Files legacy search path ported to the Windows loader. The rejection used to cover every platform; it is Windows-only now. Upstream's module dedup replaces the fork's. |
| Plugin Manager | Audio plug-in scanner hosted as a page of upstream's stacked layout. |
| libobs audio | Fork keeps its relocated parallel-render functions; upstream's `monitoring_enabled` change applied to them. |
| Hybrid MP4 | Fork split tolerance and chapter placement kept (pinned by `source-contracts.cmake`). |
| obs-vst3 | `add_core_module` (upstream removed `add_obs_plugin`). |
| Version | `OBS_VERSION_OVERRIDE` 33.0.1-perf. |

Follow-up `5a6d39d45`: OBS 33.0 repurposed `OBS_PLUGINS_PATH` and moved the legacy override to
`OBS_LEGACY_PLUGINS_PATH` / `OBS_LEGACY_PLUGINS_DATA_PATH`. This machine sets the old pair for
StreamFX, so StreamFX silently stopped loading. The Windows loader treats the old pair as the
legacy override when `OBS_PLUGINS_DATA_PATH` is present.

## Evidence

- Configure: CEF 150.0.20, obs-deps 2026-10-23 (FFmpeg 62), VS 18 2026, SDK 10.0.26100.
- Build RelWithDebInfo x64: exit 0, 0 errors, 825 sources compiled in the first build.
- CTest 8/8 passed, including both browser-texture regressions against the re-ported repair.
- New source contracts for the re-ported behaviour; 8/8 injected mutations fail the suite.
- Portable runs with a copy of the real profile (collection "MARSSVOODOO 5"): starts, CEF
  150.0.7871.252 runtime, StreamFX 1.0.0b747 loads through the compatibility code, Quick Access
  Dock loads.

## Findings from the portable runs

- **StreamFX needs FFmpeg 61** (`avcodec-61`, `avutil-59`, `swscale-8`) beside `obs64.exe`. OBS 33
  ships FFmpeg 62 only. The installed folder already had the 61 set and the overlay install keeps it.
- **Quick Access Dock 1.0.4 crashed** (`QuickAccessUtility::FrontendCallback`, access violation) on
  the first launch with an empty profile. It did not crash in 8 launches with the real profile.
- **One shutdown hang in 16 closes.** Main thread in `obs_browser!obs_module_unload` joining the
  CEF manager thread, which was inside `BrowserShutdown` -> `CefShutdown` and never returned. Not
  reproduced in the next 10 closes (1.2-1.6 s each, same as 32.2.2). Cause unknown.
- Freeze, Move, StreamUP, Stream Deck and background-removal plugins were not exercised: they live
  in `C:/ProgramData/obs-studio/plugins`, which portable mode does not search.
- Browser rendering, Hybrid MP4 recording, Stats and audio routing were not exercised.

## Installation

- Release `L:/Coding/_obs_releases/20261114-obs-33.0.1/`, source commit `5a6d39d45`, payload 2,364
  files (manifest SHA-256 `5198A229838A6A0C1D9E094B9DCD30B3F92620A51655122722F35624855FB3E1`).
- Rollback backup of the 32.2.2-perf installation, 12,220 files (manifest SHA-256
  `D94E6B5E4DC64D01CAE77D8BD90F017EB64705ED06AF5F24DBF876E385599DCA`):
  `L:/Coding/_obs_install_backup/obs-studio-pre-33.0.1-20261114/`.
- Profile backup, 16,799 files: `L:/Coding/_obs_install_backup/obs-profile-pre-33.0.1-20261114/`.
- Guarded installer `L:/Coding/_turnover/obs-deploy-obs-33.0.1-20261114.ps1` (derived from the
  2026-09-25 installer; roots, commit and manifest hashes changed). Verify-only pass, then the
  elevated run at 16:19 CST with OBS closed; robocopy exit 3; installer inventory check passed.
- Independent readback: 2,364/2,364 payload files match in `C:/Program Files/obs-studio`, 14,166
  installed files (extras preserved), installed `obs64.exe` SHA-256 `4146E242D97D3FC6...` equals the
  build, `obs64.exe --version` exits 0 with `OBS Studio - 33.0.1-perf`.

The 32.2.2 first-party modules remain in `obs-plugins/64bit`; libobs skips them as legacy core
modules. OBS was left closed. The first normal-profile start migrates plugin and browser data.

Rollback, only on request and with OBS closed: copy the install backup over the installation and
the profile backup over `%APPDATA%/obs-studio`.

Not updated: `tools/obs-install-browser-pooled-texture.ps1` (pins the 32.2.2-era browser commit),
README and release notes.
