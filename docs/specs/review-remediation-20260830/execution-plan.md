# OBS fork review remediation

## Objective

Make plug-in scan authorization content-bound, correct the VST3 output allocation, and make Windows build, test, packaging, and documentation paths deterministic and failure-propagating.

## Non-goals

- Do not submit changes to `obsproject/obs-studio`.
- Do not stop, replace, or launch the installed OBS instance while it is streaming, recording, or otherwise active.
- Do not change plug-in DSP behavior beyond cache authorization and buffer layout.

## Milestones and acceptance

1. Add fail-first compiled tests for plug-in fingerprints and planar output layout sizing.
2. Persist a versioned SHA-256 fingerprint for passed VST2/VST3 modules and reject stale or legacy pass records before in-process loading.
3. Correct VST3 planar output allocation and offsets without changing returned audio bytes.
4. Consolidate local Windows scripts on the `windows-x64` preset, propagate every failure code, and remove stale dependency overrides.
5. Build and run fork tests in Windows CI; keep workflow actions and packaging tools immutable.
6. Update build documentation for the actual `L:\Coding\OBS Project`, VS 18 2026, x64 workflow.
7. Pass formatting/static gates, full CTest, a production-equivalent x64 build, secret/risky-file scan, and tracked-diff review.
8. Commit and push only `feature/performance-optimizations`; deploy only after runtime safety checks permit it.

## Security and failure modes

- A passed scan authorizes exact bytes, not merely a path or class ID.
- Fingerprint calculation or cache-version mismatch fails closed and requests a rescan.
- Build/test subprocess errors must remain non-zero at the outermost caller.
- Packaging tools are pinned and hash-verified before execution.

## Rollback

- Git rollback is the parent of the remediation commit; no history rewriting or force-push.
- Installed OBS remains untouched until a full predeploy backup and inactive-process check pass.

## Progress

- 2026-08-30: Review findings reproduced; implementation started.
- 2026-08-30: VST2/VST3 scan authorization was bound to deterministic SHA-256 content fingerprints; stale cache schemas and changed plug-in bytes now fail closed.
- 2026-08-30: VST3 planar output storage now uses float-element counts and frame-based channel offsets with overflow checks.
- 2026-08-30: Windows build wrappers, CI CTest coverage, pinned NSIS packaging, and current build documentation were consolidated and verified.
- 2026-08-30: The clean-checkout wrapper built `32.2.2-perf` build 407 and passed 4/4 CTest targets. YAML parsing, diff hygiene, credential/risky-file scans, invalid-configuration failure propagation, and critical-artifact hashing passed.
- 2026-08-30: Installed OBS was confirmed closed and a 12,101-file predeploy backup was verified at `L:\Coding\_obs_install_backup\obs-studio-pre-review-remediation-20260830-194350`.
- 2026-08-30: After the owner reset the two-attempt boundary, the hash-guarded installer completed through Windows' required Force New Window elevation. All five critical installed binaries match the verified build. Installed `32.2.2-perf` reached `Startup complete`, loaded VST2/VST3, started neither streaming nor recording, remained responsive, and created no new crash report. OBS was left open for an owner-performed normal close. Evidence: `deployment-receipt.md`.
