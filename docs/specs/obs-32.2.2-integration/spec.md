# OBS 32.2.2 Integration Specification

## Outcome

Voodoo OBS Studio tracks the official OBS 32.2.2 release while preserving the
fork's mixer, VST2/VST3, workspace, Restream, performance, and security work.

## Requirements

- `feature/performance-optimizations` contains official
  `upstream/release/32.2` commit `ba2f32bdf791005443988a4955e963663e16b1ed`.
- The existing patch-equivalent Windows DLL-loading fix remains present.
- Windows builds report `32.2.2-perf`; public fork documentation uses
  `32.2.2-voodoo1` as the next release name.
- The Release build and registered regression tests pass.
- Deployment uses a timestamped backup and installs only while OBS is stopped.
- The fork branch is pushed and its remote SHA matches the local SHA.

## Exclusions

- Do not merge unreleased `upstream/master` Qt refactors.
- Do not tag or publish the GitHub release in this integration task.
- Do not submit any fork code to the official OBS repository.

## Acceptance

The installed Windows executable reports 32.2.2, starts without an immediate
crash, preserves the CCE plug-in binaries, and exits without leaving OBS or
scanner processes running.
