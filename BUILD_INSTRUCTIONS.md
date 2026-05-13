# Building OBS with Optimizations

## About the version override

Both `configure_and_build.bat` and `build_optimized.bat` pass
`-DOBS_VERSION_OVERRIDE="32.1.2-perf"`. The override exists for plugin
compatibility — many third-party plugins gate themselves on the OBS major
version string and refuse to load against an unrecognized build. The `-perf`
suffix lets bug reports be triaged correctly so we can tell the OpenClaw
performance fork apart from official OBS 32.1.2.

## ✅ Build System Updated
The optimization file `obs-audio-optimized.c` has been added to `libobs/CMakeLists.txt`

## 🚀 Quick Build (Windows)

### Option 1: Automated Build Script (Easiest)
```batch
# Run the build script
build_optimized.bat
```

### Option 2: Manual Build
```batch
# 1. Open "Developer Command Prompt for VS 2022" (or your VS version)

# 2. Navigate to OBS project
cd "c:\obs project"

# 3. Create and enter build directory
mkdir build
cd build

# 4. Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 5. Build (use -j for parallel compilation)
cmake --build . --config Release -j
```

## Prerequisites

### Windows
- **Visual Studio 2022** (or 2019) with C++ tools
- **CMake 3.28+**
- **Git**
- All OBS dependencies (should already be set up)

### Check if you have the tools:
```batch
cmake --version
git --version
cl.exe
```

## Build Time
- **First build**: 15-30 minutes (depending on CPU)
- **Incremental builds**: 2-5 minutes

## Build Output Location
```
c:\obs project\build\rundir\Release\bin\obs64.exe
```

## Testing the Build

### 1. Quick Smoke Test
```batch
cd build\rundir\Release\bin
obs64.exe
```

**Check for:**
- OBS launches successfully
- No immediate crashes
- Interface loads properly

### 2. Functional Test
1. Add a video source (Display Capture or Video Capture Device)
2. Add an audio source (Audio Input Capture)
3. Start recording for 2 minutes
4. Stop and verify the recording plays correctly
5. Check for any audio/video artifacts

### 3. Performance Comparison

**Before** (original build):
- Open Task Manager → Performance tab
- Note CPU usage while recording

**After** (optimized build):
- Same test with optimized build
- Compare CPU usage - should be 15-30% lower

## Troubleshooting

### Issue: "cmake: command not found"
**Solution**: Install CMake and add to PATH
```batch
# Download from: https://cmake.org/download/
# Or use chocolatey:
choco install cmake
```

### Issue: "Cannot find Visual Studio"
**Solution**: Run from Developer Command Prompt
```batch
# Start Menu → Visual Studio 2022 → Developer Command Prompt for VS 2022
```

### Issue: "Missing dependencies"
**Solution**: OBS requires many dependencies. If this is a fresh clone:
```batch
# Follow OBS build instructions first:
# https://github.com/obsproject/obs-studio/wiki/Install-Instructions#windows-build-directions
```

### Issue: Build errors with obs-audio-optimized.c
**Solution**: Check compiler supports SIMD intrinsics
```batch
# Add this flag to CMakeLists.txt if needed:
# set_source_files_properties(obs-audio-optimized.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
```

### Issue: Compilation takes forever
**Solution**: Use parallel compilation
```batch
# Use -j flag with number of CPU cores
cmake --build . --config Release -j8
```

## Verify Optimizations Are Active

Add this temporary debug code to `obs-audio.c`:

```c
// At the top of file
extern bool cpu_supports_avx2(void);

// In audio_callback() function
static bool printed = false;
if (!printed) {
    blog(LOG_INFO, "=== OBS OPTIMIZATIONS ACTIVE ===");
    blog(LOG_INFO, "AVX2 Support: %s", cpu_supports_avx2() ? "YES" : "NO");
    printed = true;
}
```

Then check OBS logs (Help → Log Files → View Current Log) for the message.

## Clean Build (If Needed)

```batch
# Remove build directory and start fresh
cd "c:\obs project"
rmdir /s /q build
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j
```

## Next Steps After Successful Build

1. ✅ **Build completes** → Continue to Phase 2 integration
2. ⚠️ **Build fails** → Check troubleshooting section
3. ✅ **Build works** → Run performance tests from NEXT_STEPS.md

## Performance Testing Script

Create this file as `test_performance.bat`:

```batch
@echo off
echo Starting OBS Performance Test...
echo.
echo Press Ctrl+C to stop monitoring
echo.

:loop
for /f "tokens=2 delims=," %%a in ('typeperf "\Processor(_Total)\%% Processor Time" -sc 1 ^| find ","') do (
    echo CPU: %%a%%
)
timeout /t 1 /nobreak >nul
goto loop
```

Run while OBS is recording to monitor CPU usage.

## Build Complete Checklist

- [ ] CMakeLists.txt updated with obs-audio-optimized.c
- [ ] Build directory created
- [ ] CMake configuration successful
- [ ] Compilation completed without errors
- [ ] obs64.exe launches successfully
- [ ] No crashes during basic usage
- [ ] Ready for Phase 2 (integration testing)

---

**Current Status**: ✅ Build system configured  
**Next Action**: Run `build_optimized.bat` or manual build commands
