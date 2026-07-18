# OBS VST3 Filter for Windows

This experimental module adds a native 64-bit VST3 audio filter to the Windows OBS build. It supports audio effects, saved component and controller state, native editor windows, latency reporting, component restart requests, and an optional OBS-source sidechain.

## SDK

The accepted build uses Steinberg VST 3 SDK tag `v3.8.0_build_66` at commit `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`.

Clone the SDK with submodules into `.deps/vst3sdk`, or pass its root explicitly:

```powershell
cmake -S . -B build -DENABLE_VST3=ON -DVST3SDK_PATH="L:/path/to/vst3sdk"
cmake --build build --config Release --target obs-vst3
```

The module is disabled at configure time when the required SDK sources are incomplete. The SDK itself is not copied into this repository.

## Discovery

Only modules below the standard Windows VST3 locations are accepted:

- `%ProgramFiles%\Common Files\VST3`
- `%LOCALAPPDATA%\Programs\Common\VST3`

Paths are canonicalized before comparison, including case-insensitive component checks and rejection of links that escape an allowed root. Each module is inspected by its own scanner worker. A worker has a 15-second timeout, a 768 MiB process-memory limit, and a kill-on-close job. A crash or hang skips that module and does not stop the remaining scan.

The scan result is written after every successful worker. OBS allows up to ten minutes for a full installed-library scan and can publish a growing partial list during the first run. If a refresh fails, the last valid cache remains available. Cache input is capped at 16 MiB and 10,000 entries, and every class ID, path, display string, and schema field is validated before use.

## Runtime boundaries

The scanner protects discovery only. A selected VST3 effect processes audio inside the OBS process and must therefore be treated as trusted native code. An unstable effect can still crash OBS.

The host applies the following limits before or after calling an effect:

- exact 32-hex-character class IDs and standard-folder module paths;
- bounded audio-bus and channel arrangements;
- bounded state chunks with complete read and write checks;
- bounded latency, editor dimensions, restart requests, and parameter queues;
- no Qt allocation, UI call, or log operation from the real-time process callback;
- invalid output samples are cleared and the filter is bypassed after processing failure;
- sidechain delivery uses a non-blocking producer path and drops a block instead of waiting on the audio thread.

## Acceptance checks

1. Build both `obs-vst3-scanner.exe` and `obs-vst3.dll` in Release.
2. Run the scanner against the installed library and validate the JSON version, canonical paths, and 32-character class IDs.
3. Add one trusted effect, open and close its editor, and verify audio continues when the editor is closed.
4. Save, close, and reopen the scene collection; confirm the effect and its state return.
5. Exercise a compatible sidechain input and then clear the selection.
6. Close OBS normally and confirm the log reaches the complete shutdown footer without a crash report.

Skipped scanner workers are expected when an installed module is incompatible, crashes during discovery, or exceeds its time limit. The acceptance condition is that responsive effects remain available and one bad module cannot abort the scan.
