<#
.SYNOPSIS
    OBS Phase 3 - Automated test-scenario runner.

.DESCRIPTION
    Guides the tester through each of the four benchmark scenarios defined in
    NEXT_STEPS.md / PERFORMANCE_TEST_RESULTS.md, automatically launching
    obs-perf-monitor.ps1 for each one and saving per-scenario CSVs.

    Run AFTER building and launching OBS.  The script waits for OBS to be
    running before starting each scenario, prompts you to configure the scene,
    monitors for the specified duration, then prompts for the next scenario.

.PARAMETER OBSExe
    Full path to obs64.exe.  Leave blank to skip auto-launch.

.PARAMETER OutputDir
    Where to store CSV files (default: tools\results\ next to this script).

.EXAMPLE
    # Let the script auto-launch OBS and run all four scenarios
    .\obs-run-scenarios.ps1 -OBSExe "C:\Program Files\OBS Studio\bin\64bit\obs64.exe"

    # OBS is already running; just collect data
    .\obs-run-scenarios.ps1
#>

param(
    [string] $OBSExe   = "",
    [string] $OutputDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

$scriptDir = $PSScriptRoot
if (-not $OutputDir) { $OutputDir = Join-Path $scriptDir "results" }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$monitorScript = Join-Path $scriptDir "obs-perf-monitor.ps1"
if (-not (Test-Path $monitorScript)) {
    Write-Error "obs-perf-monitor.ps1 not found at: $monitorScript"
    exit 1
}

$appStatsCsv = Join-Path $OutputDir "OBS_RUNTIME_STATS.csv"

# -- Scenario definitions -----------------------------------------------------
$scenarios = @(
    @{
        Id          = 1
        Name        = "Test1_SingleSource_Baseline"
        Duration    = 600
        Description = @"
TEST 1 - SINGLE SOURCE (BASELINE)
------------------------------------------------------------------------
Configure OBS as follows BEFORE pressing Enter:
  * Resolution : 1920x1080 @ 30 fps
  * Sources    : 1 video source (webcam or video capture device)
                 1 audio source (microphone or desktop audio)
  * Recording  : Start recording (do NOT stream)
  * Encoder    : Software (x264) - quality preset: veryfast
  * No filters on any source

This is the BASELINE.  All other tests are compared to this one.
------------------------------------------------------------------------
"@
    },
    @{
        Id          = 2
        Name        = "Test2_MultipleSources"
        Duration    = 600
        Description = @"
TEST 2 - MULTIPLE SOURCES
------------------------------------------------------------------------
Configure OBS as follows BEFORE pressing Enter:
  * Resolution : 1920x1080 @ 30 fps
  * Sources    : 4 video sources (can be window captures or image sources)
                 4 audio sources (can be virtual audio cables or file loops)
  * Recording  : Start recording
  * Encoder    : Same as Test 1 (x264 veryfast)
  * No filters

Expected: CPU usage increases; frame time should remain < 33 ms (30 fps).
------------------------------------------------------------------------
"@
    },
    @{
        Id          = 3
        Name        = "Test3_HighResolution"
        Duration    = 300
        Description = @"
TEST 3 - HIGH RESOLUTION (4K60)
------------------------------------------------------------------------
Configure OBS as follows BEFORE pressing Enter:
  * Resolution : 3840x2160 @ 60 fps  (or highest your GPU supports)
  * Sources    : 1 video source (screen/display capture at native res)
                 2 audio sources
  * Recording  : Start recording
  * Encoder    : NVENC / AMF / QuickSync if available; else x264 ultrafast
  * Watch for dropped frames in Stats panel (View > Stats)

Duration: 5 minutes.
------------------------------------------------------------------------
"@
    },
    @{
        Id          = 4
        Name        = "Test4_ComplexScene"
        Duration    = 600
        Description = @"
TEST 4 - COMPLEX SCENE
------------------------------------------------------------------------
Configure OBS as follows BEFORE pressing Enter:
  * Resolution : 1920x1080 @ 60 fps
  * Sources    : 8+ sources (mix of video captures, images, browser sources)
  * Audio      : 4+ audio sources
  * Filters    : At least 2 sources should have filters (e.g. colour correction,
                 noise suppression)
  * Transitions: Set a transition (e.g. Fade 300 ms) and switch scenes roughly
                 once per minute during the test
  * Streaming  : Start streaming (RTMP, any ingest) AND recording simultaneously

This is the worst-case stress test.
------------------------------------------------------------------------
"@
    }
)

# -- Helper: wait for OBS -----------------------------------------------------
function Wait-ForOBS {
    Write-Host "Waiting for OBS to be running..." -ForegroundColor Yellow
    while ($true) {
        $p = Get-Process -Name obs64 -ErrorAction SilentlyContinue
        if (-not $p) { $p = Get-Process -Name obs32 -ErrorAction SilentlyContinue }
        if ($p) { Write-Host "OBS detected (PID $($p.Id))." -ForegroundColor Green; return }
        Start-Sleep -Seconds 2
    }
}

# -- Optional: auto-launch OBS ------------------------------------------------
if ($OBSExe -and (Test-Path $OBSExe)) {
    $alreadyRunning = (Get-Process -Name obs64 -ErrorAction SilentlyContinue) -or
                      (Get-Process -Name obs32 -ErrorAction SilentlyContinue)
    if (-not $alreadyRunning) {
        if (Test-Path $appStatsCsv) {
            Remove-Item $appStatsCsv -Force
        }

        $prevPerfCsv = $env:OBS_PERF_EXPORT_CSV
        $prevPerfInterval = $env:OBS_PERF_EXPORT_INTERVAL_MS
        $env:OBS_PERF_EXPORT_CSV = $appStatsCsv
        $env:OBS_PERF_EXPORT_INTERVAL_MS = "1000"
        Write-Host "Launching OBS: $OBSExe" -ForegroundColor Cyan
        Start-Process -FilePath $OBSExe
        if ($null -eq $prevPerfCsv) {
            Remove-Item Env:OBS_PERF_EXPORT_CSV -ErrorAction SilentlyContinue
        } else {
            $env:OBS_PERF_EXPORT_CSV = $prevPerfCsv
        }
        if ($null -eq $prevPerfInterval) {
            Remove-Item Env:OBS_PERF_EXPORT_INTERVAL_MS -ErrorAction SilentlyContinue
        } else {
            $env:OBS_PERF_EXPORT_INTERVAL_MS = $prevPerfInterval
        }
        Start-Sleep -Seconds 5   # give OBS time to start
    } elseif (-not (Test-Path $appStatsCsv)) {
        Write-Host "OBS is already running; app-level stats export is not enabled for this session." -ForegroundColor Yellow
    }
}

Wait-ForOBS

# -- Banner --------------------------------------------------------------------
Write-Host ""
Write-Host "====================================================================" -ForegroundColor Cyan
Write-Host "  OBS Optimization - Phase 3 Performance Benchmark Suite            " -ForegroundColor Cyan
Write-Host "  Results will be saved to:                                         " -ForegroundColor Cyan
Write-Host "  $OutputDir" -ForegroundColor Cyan
Write-Host "====================================================================" -ForegroundColor Cyan
Write-Host ""

$summaryRows = [System.Collections.Generic.List[hashtable]]::new()

# -- Run each scenario ---------------------------------------------------------
foreach ($sc in $scenarios) {

    Write-Host ""
    Write-Host ("-" * 72) -ForegroundColor DarkGray
    Write-Host $sc.Description -ForegroundColor White
    $mins = [int]($sc.Duration / 60)
    $secs = $sc.Duration % 60
    Write-Host "Duration: $($sc.Duration) seconds  ($mins min $secs sec)" -ForegroundColor DarkCyan

    $resp = Read-Host "Press Enter to START monitoring, or type 'skip' to skip this test"
    if ($resp -ieq "skip") {
        Write-Host "Skipped." -ForegroundColor DarkYellow
        continue
    }

    # Run monitor as a sub-process so Ctrl+C in the child doesn't kill us
    $args_ = @(
        "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-File", $monitorScript,
        "-ScenarioName",          $sc.Name,
        "-DurationSeconds",       $sc.Duration,
        "-SampleIntervalSeconds", "1",
        "-OutputDir",             $OutputDir,
        "-AppStatsCsv",           $appStatsCsv
    )

    Write-Host "> Monitoring started for '$($sc.Name)' ..." -ForegroundColor Green
    $job = Start-Process -FilePath "powershell.exe" -ArgumentList $args_ -PassThru -NoNewWindow -Wait

    # Collect the latest summary CSV for this scenario and summarise
    $summaryFile = Get-ChildItem -Path $OutputDir -Filter "$($sc.Name)_*_SUMMARY.csv" |
                   Sort-Object LastWriteTime | Select-Object -Last 1

    if ($summaryFile) {
        $summary = Import-Csv $summaryFile.FullName | Select-Object -First 1
        $summaryRows.Add(@{
            Test      = "Test $($sc.Id)"
            AvgCPU    = $summary.ProcessAvgCPU_Pct
            AvgFrame  = $summary.AvgFrameTime_ms
            AudioCB   = $summary.AvgAudioCallback_ms
            Lagged    = $summary.LaggedFrames
            Samples   = $summary.Samples
            SummaryCSV = $summaryFile.Name
        })

        Write-Host "  Avg CPU: $($summary.ProcessAvgCPU_Pct)%   Avg Frame: $($summary.AvgFrameTime_ms) ms   Audio CB: $($summary.AvgAudioCallback_ms) ms   Lagged: $($summary.LaggedFrames)" -ForegroundColor Cyan
    } else {
        $csvFiles = Get-ChildItem -Path $OutputDir -Filter "$($sc.Name)_*.csv" |
                    Sort-Object LastWriteTime | Select-Object -Last 1

        if ($csvFiles) {
            $data = Import-Csv $csvFiles.FullName
            $cpuArr = $data | Where-Object { $_.CPU_Pct -match '^\d' } | ForEach-Object { [double]$_.CPU_Pct }
            $avgCpu = if ($cpuArr) { [math]::Round(($cpuArr | Measure-Object -Average).Average, 1) } else { "N/A" }

            $summaryRows.Add(@{
                Test      = "Test $($sc.Id)"
                AvgCPU    = $avgCpu
                AvgFrame  = "N/A"
                AudioCB   = "N/A"
                Lagged    = "N/A"
                Samples   = $data.Count
                SummaryCSV = $csvFiles.Name
            })

            Write-Host "  Avg CPU: $avgCpu%   OBS runtime stats unavailable" -ForegroundColor DarkYellow
        }
    }

    Write-Host ""
    Read-Host "Configure OBS for the NEXT test, then press Enter to continue"
}

# -- Final summary table ------------------------------------------------------
if ($summaryRows.Count -gt 0) {
    Write-Host ""
    Write-Host "=== PHASE 3 BENCHMARK SUMMARY =========================================" -ForegroundColor Cyan
    Write-Host ("{0,-10} {1,8} {2,10} {3,10} {4,8} {5,8}" -f "Test","AvgCPU%","AvgFrame","AudioCB","Lagged","Samples") -ForegroundColor White
    Write-Host ("{0,-10} {1,8} {2,10} {3,10} {4,8} {5,8}" -f "----------","--------","----------","----------","--------","--------") -ForegroundColor DarkGray
    foreach ($row in $summaryRows) {
        Write-Host ("{0,-10} {1,8} {2,10} {3,10} {4,8} {5,8}" -f $row.Test, $row.AvgCPU, $row.AvgFrame, $row.AudioCB, $row.Lagged, $row.Samples)
    }

    # Write machine-readable summary CSV
    $summaryPath = Join-Path $OutputDir "SUMMARY_$(Get-Date -Format 'yyyyMMdd_HHmmss').csv"
    "Test,AvgCPU_Pct,AvgFrameTime_ms,AvgAudioCallback_ms,LaggedFrames,Samples,SummaryCSV" | Out-File $summaryPath -Encoding UTF8
    foreach ($row in $summaryRows) {
        "$($row.Test),$($row.AvgCPU),$($row.AvgFrame),$($row.AudioCB),$($row.Lagged),$($row.Samples),$($row.SummaryCSV)" |
            Out-File $summaryPath -Append -Encoding UTF8
    }

    Write-Host ""
    Write-Host "Summary CSV: $summaryPath" -ForegroundColor Green
    Write-Host "Copy the table above into PERFORMANCE_TEST_RESULTS.md." -ForegroundColor Yellow
}
