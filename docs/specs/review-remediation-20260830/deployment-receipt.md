# OBS review-remediation deployment receipt

## Outcome

The verified `32.2.2-perf` build was overlaid onto `C:\Program Files\obs-studio` after the owner reset the deployment retry boundary and approved Windows elevation. No OBS process was running during backup or deployment.

## Rollback

- Full predeploy backup: `L:\Coding\_obs_install_backup\obs-studio-pre-review-remediation-20260830-194350`
- Files: 12,101
- Predeploy `obs64.exe` SHA-256: `DFFF8502C0B1996818D09EE417BE67F7DE345FB68A384BE464D8EB63D53E79F8`
- Hash-guarded installer: `L:\Coding\_turnover\obs-deploy-review-remediation.ps1`
- Installer SHA-256: `998280E5AC0448C1B10F0790A5D258CB422F174F22A70C7CEA3EA1C4E5606CED`

## Installed critical artifacts

- `bin\64bit\obs64.exe`: `B7B965E13F03E2BFA5D7CE9DFB539F9A9F93239304214A56E48E4FFCC2FB112C`
- `bin\64bit\obs-vst2-scanner.exe`: `36C3D300A2F7A0A1BA696C75E83E431759E1E257A01C158A01C0BCB93565B7EB`
- `bin\64bit\obs-vst3-scanner.exe`: `78D51FA1FA9413A3D3DD641597AB97D55F0C47E79CF1C2C39CFEA418DF3DEE0C`
- `obs-plugins\64bit\obs-vst.dll`: `2FBFBD6A6903A8B73CA782661364F0F73FDF2DDD6907997481AC2EA81809859D`
- `obs-plugins\64bit\obs-vst3.dll`: `DD17708378C6FD24AB5A5E04B58D623F811CE4D22C3E882C0EB271A369F17F1F`

Each installed hash exactly matched `build_x64\rundir\RelWithDebInfo` after deployment.

## Runtime acceptance

- Process: installed `obs64.exe`, PID 77844, responsive at verification.
- Log: `C:\Users\Owner\AppData\Roaming\obs-studio\logs\2026-08-30 20-14-57.txt`
- Runtime version: `OBS 32.2.2-perf (64-bit, windows)`.
- `obs-vst.dll` and `obs-vst3.dll` loaded; `OBS-VST3 filter loaded successfully` was recorded.
- `Startup complete` was recorded.
- Streaming started: no.
- Recording started: no.
- Crash-report count before/after: 11/11.

The VST3 cache schema intentionally changed. The first startup correctly reported that no compatible cache was available; use **Tools > Plugin Manager > Audio Plug-ins** to scan before selecting VST3 effects.

OBS was deliberately left open. Do not automate its close; the owner should close it normally and then confirm the shutdown log and unchanged crash-report count.
