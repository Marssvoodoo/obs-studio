# OBS Audit Remediation

## Objective

Correct every confirmed fork-specific issue from the 2026-08-19 review while
preserving OBS 32.2.1 behavior, third-party plug-in compatibility, and the
currently running production instance until a replacement build is verified.

## Required behavior

- Audio graph caches are invalidated whenever active source topology changes.
- VST2 and VST3 plug-ins load in-process only from a current passed scan result.
- Automatic audio worker sizing is bounded by both CPU capacity and batch size.
- Program Output metering performs no audio work while disabled or hidden and
  publishes true peak at most once per callback.
- Overlapping video-plane copies preserve the original source bytes and reject
  arithmetic overflow before calculating address extents.
- Named-pipe cancellation never returns while overlapped I/O is still pending.
- Windows process mitigations are applied once and do not retain the stripped
  image restriction that conflicts with supported third-party modules.
- NVML statistics use the NVIDIA device whose Windows LUID matches the adapter
  selected by OBS; an unprovable match fails closed to unavailable statistics.
- Fork regression tests are registered with CTest and the custom delta passes
  whitespace/build hygiene checks.

## Non-goals

- No changes to stream settings, scenes, profiles, browser-source URLs, or
  external Restream services.
- No upstream OBS pull request.
- No deployment over an active OBS process.
- No replacement of the managed VST scanners or in-process VST hosts.

## Acceptance

- New focused regressions fail before implementation and pass afterward.
- Release targets `libobs`, `obs-studio`, `obs-vst`, and `obs-vst3` build.
- CTest reports registered passing fork regressions.
- Installed artifacts are backed up, copied only after OBS is safely stopped,
  and hash-match the verified Release output.
- The fork branch is committed, pushed, clean, and remote-SHA verified.
