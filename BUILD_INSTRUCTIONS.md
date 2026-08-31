# Building the OBS performance fork on Windows

This fork builds OBS 32.2.2 as `32.2.2-perf`. The suffix identifies the fork without changing the OBS major version used by third-party plug-ins.

## Supported build

- Windows x64
- Visual Studio 18 with the Desktop C++ workload
- CMake 3.28 or newer
- Dependencies pinned by `CMakePresets.json` (`2026-07-15` at this revision)

Run any of these equivalent wrappers from any working directory:

```batch
configure_and_build.bat
build_optimized.bat
run_build.bat
```

They all call `scripts\build-windows-x64.bat`, configure the `windows-x64` preset, build `RelWithDebInfo`, and run CTest. A failing configure, compile, or test returns a nonzero exit code.

To build another multi-config configuration explicitly:

```batch
scripts\build-windows-x64.bat Release
```

Build output is under:

```text
build_x64\rundir\RelWithDebInfo\bin\64bit\obs64.exe
build_x64\rundir\Release\bin\64bit\obs64.exe
```

## Manual equivalent

Use the CMake bundled with Visual Studio if `cmake.exe` is not on `PATH`:

```batch
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo --parallel
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

Do not point CMake at older dependency folders manually. The preset owns dependency selection and hashes.

## Runtime smoke test

Before replacing an installed OBS build, confirm OBS is not streaming, recording, or running. Launch the staged `obs64.exe`, check the current log for the `32.2.2-perf` version, and exercise the changed audio/VST paths before deployment.
