# Local installation verification — 2026-09-07

Installed the existing custom **OBS Studio 32.2.2-perf** build from source commit
`b8e884b9bf9f94573b5a5ffcdaef7c1a1ecda204`. This is the prepared fork update,
not a stock upstream installer or a new public release.

## Installation evidence

- OBS was closed before, during, and after deployment. No stream, recording,
  normal OBS session, or unrelated service was started or stopped.
- The guarded administrator installer completed at **11:40:42 CDT**. Its pinned
  payload and rollback inventories passed before any replacement. Robocopy's
  successful exit code was 3; the installer completed successfully and recorded
  the installed payload verification.
- Independent installed readback verified **2,185 payload files**, **9,923 extra
  installed files preserved**, and **29 unchanged profile/scene/script/theme
  configuration files**. Extra installed plugins were not deleted.
- Existing full **12,108-file** rollback backup remains available. Existing VST3
  approval-cache files were separately backed up before activation.
- Installed `obs64.exe --version` exits 0 with `OBS Studio - 32.2.2-perf` and no
  stderr. Executable SHA-256:
  `304920A72E13B546A61F69CE327045A0897C2CFAD857AA639BE9225A82FB630F`.
- All **6/6 CTest** cases passed again, including native telemetry fault handling
  and VST3 validation regressions.
- The actual **installed** VST3 scanner successfully initialized a private copy
  of Voodoo FX, producing one schema-3 approval with the independently verified
  fingerprint. It did not update the production approval cache or open an editor.
  The first probe incorrectly used a non-prefixed output filename; the scanner
  correctly rejected it with exit 3. The corrected `.vst3-scan-` output path passed.

## Operator boundary and rollback

OBS remains closed. Full normal-profile startup, the Stats UI, streaming, and
third-party DSP/editor behavior were not exercised in this installation session.
Before using VST3 effects live, run a fresh VST3 scan in Tools > Plugin Manager;
old schema-2 approvals are intentionally invalid. No legacy approvals were copied
into the new schema.

Local receipts and scripts:
`L:/Coding/_scratch/obs-local-update-20260907/`.
Guarded deployment receipt:
`L:/Coding/_obs_releases/20260905-review-fixes/deployment-result.json`.
Rollback installation:
`L:/Coding/_obs_install_backup/obs-studio-pre-review-fixes-20260905/`.
Only perform an explicitly requested rollback with OBS closed; preserve current
user configuration rather than overwriting it from an older snapshot.
