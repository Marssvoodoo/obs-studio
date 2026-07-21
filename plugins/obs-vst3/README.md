# OBS VST3 Filter for Windows

`obs-vst3` is an experimental native 64-bit VST3 effect host for the Windows
build of Voodoo OBS Studio. It adds a **VST3 Plugin** audio filter, supervised
plug-in discovery, persistent state, native editor windows, latency reporting,
restart handling, parameter changes, and an optional OBS-source sidechain.

## Support status

| Capability | Status |
| --- | --- |
| Windows x64 host and scanner | Supported |
| Windows arm64 build | Built by release CI; plug-in compatibility depends on native arm64 VST3 modules |
| Effect plug-ins | Supported |
| Instrument and MIDI plug-ins | Not supported |
| Component and controller state | Supported with bounded state chunks |
| Native editor window | Supported |
| Latency reporting | Supported |
| OBS-source sidechain | Supported when the plug-in exposes a compatible auxiliary input |
| Scanner process isolation | Supported |
| Live effect process isolation | Not implemented; selected effects run inside OBS |
| macOS and Linux hosting | Not implemented |

The module is suitable for controlled Windows production setups using trusted
effects. It is not yet a cross-platform or crash-proof VST3 host.

## SDK and build

The accepted build uses Steinberg VST 3 SDK tag `v3.8.0_build_66` at commit
`9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`.

Clone the SDK with submodules into `.deps/vst3sdk`, or pass its root explicitly:

```powershell
git clone --recursive --branch v3.8.0_build_66 `
  https://github.com/steinbergmedia/vst3sdk.git .deps/vst3sdk
cmake -S . -B build -DENABLE_VST3=ON -DVST3SDK_PATH="$PWD/.deps/vst3sdk"
cmake --build build --config Release --target obs-vst3 obs-vst3-scanner
```

The module disables itself at configure time if any required SDK source is
missing. The SDK is not copied into this repository. Windows release CI checks
out the exact accepted SDK commit and verifies the revision before configuring
OBS, preventing a release from silently omitting the VST3 module.

## Install layout

The module and its data directory use the normal OBS plug-in layout:

```text
obs-plugins/64bit/obs-vst3.dll
bin/64bit/obs-vst3-scanner.exe
data/obs-plugins/obs-vst3/locale/en-US.ini
```

The accepted local installation uses the supported shared package root:

```text
C:\ProgramData\obs-studio\plugins\obs-vst3\
```

Do not keep duplicate copies under both Program Files and ProgramData. OBS can
load the wrong module or report a duplicate when two package roots contain the
same plug-in.

## Scan and manage effects

Open **Tools > Plugin Manager > Audio Plug-ins**, select **VST3**, and choose:

- **Scan** to reuse valid prior results while checking new or changed modules;
- **Rescan All** to inspect every candidate again;
- **Stop** to end the current scan after preserving completed results;
- **Skip previously failed plug-ins** to avoid known crashes, hangs, or invalid
  modules on later scans.

The manager shows the active search location, current candidate, progress, and
the last passed, failed, or skipped result for every inspected module. Results
stay in fixed columns so progress updates do not resize the window.

### Discovery locations

Only modules below the standard Windows VST3 roots are accepted:

- `%ProgramFiles%\Common Files\VST3`
- `%LOCALAPPDATA%\Programs\Common\VST3`

Paths are canonicalized before comparison. The scanner rejects links that
escape an allowed root and compares path components case-insensitively.

### Worker containment

Each candidate is inspected by its own helper process with:

- a 15-second timeout;
- a 768 MiB process-memory limit;
- a kill-on-close Windows job;
- validated output before it can enter the cache.

A crash, hang, invalid response, or resource-limit breach fails that candidate
without ending the remaining scan. The aggregate cache is written after each
successful worker so Stop leaves usable partial results. A failed refresh keeps
the last valid cache.

Cache input is limited to 16 MiB and 10,000 entries. Every class ID, module
path, display string, category, and schema field is validated before use.

## Add and configure an effect

1. Open a source's **Filters** window.
2. Add **VST3 Plugin** under Audio/Video Filters.
3. Select an effect from the validated list.
4. Use **Open Plug-in Interface** when the effect provides a native editor.
5. Select a sidechain source only when the effect exposes a compatible
   auxiliary input.
6. Save and reopen the scene collection to confirm the plug-in restores its
   state before using it live.

The filter stores the canonical module path and the 32-character VST3 class ID,
so two effects in the same module remain distinct.

## Runtime boundaries

The scanner protects discovery only. A selected VST3 effect processes audio
inside the OBS process and must be treated as trusted native code. An unstable
effect can still crash OBS while streaming, recording, saving state, or opening
its editor.

The host applies these limits before or after invoking an effect:

- exact 32-hex-character class IDs and standard-folder module paths;
- bounded audio-bus and channel arrangements;
- bounded component and controller state with complete read/write checks;
- bounded latency, editor dimensions, restart requests, and parameter queues;
- no Qt allocation, UI operation, or logging from the real-time callback;
- invalid output samples are cleared and the filter bypasses after processing
  failure;
- sidechain delivery uses a non-blocking producer path and drops a block instead
  of waiting on the audio thread.

## Troubleshooting

### An effect is not listed

- Confirm it is a 64-bit VST3 effect installed under a standard VST3 root.
- Open the Audio Plug-ins manager and inspect its most recent status and reason.
- Clear **Skip previously failed plug-ins**, then use **Rescan All** once.
- Confirm `obs-vst3-scanner.exe` is installed beside the other OBS helper
  executables.
- Check the OBS log for the cached plug-in count and scanner exit status.

### A scan stops on one module

The worker should be terminated after 15 seconds. If the manager remains active,
press **Stop**; completed results remain durable. On the next scan, enable
**Skip previously failed plug-ins**.

### OBS crashes after selecting an effect

Start OBS in safe mode, remove the filter, and leave that effect disabled. A
clean scanner result does not prove the effect is safe during live processing.
Retest with a new scene collection before returning it to production.

### The editor opens but no audio passes

Close the editor, bypass the filter, and check whether the effect supports the
current OBS sample rate and channel arrangement. Re-enable it only after audio
passes in a controlled recording test.

## Acceptance checklist

1. Build `obs-vst3-scanner.exe` and `obs-vst3.dll` in Release.
2. Run a managed scan and verify the JSON schema, canonical module paths, and
   32-character class IDs.
3. Stop a scan mid-library and confirm completed results remain after reopening
   the manager.
4. Enable skip-failed behavior and confirm a recorded failure is not launched
   again.
5. Add one trusted effect, open and close its editor, and confirm audio continues.
6. Save, close, and reopen the scene collection; confirm effect state returns.
7. Exercise a compatible sidechain and then clear the selection.
8. Close OBS normally and confirm the log reaches the complete shutdown footer
   without a new crash report.

Skipped workers are expected when installed modules are incompatible, crash
during discovery, or exceed their limits. Acceptance requires responsive effects
to remain available and one bad module to be unable to abort the full scan.

## Upstream scope gap

The OBS Project's published VST3 request calls for Windows, macOS, and Linux
support and live plug-in execution in an external process. This module currently
isolates discovery only and hosts live effects in-process on Windows. Those are
material architecture gaps, so this implementation should remain a fork feature
until a cross-platform process-host design and its real-time IPC path are built,
tested, and reviewed.
