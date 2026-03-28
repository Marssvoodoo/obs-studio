<#
.SYNOPSIS
    OBS Studio Performance Monitor - Phase 3 benchmark data collector.

.DESCRIPTION
    Continuously samples the obs64 (or obs32) process and writes CPU,
    working-set, private bytes, thread count and handle count to a
    timestamped CSV file. When an OBS runtime stats CSV is available, the
    script also summarises frame timing, dropped/skipped frames, and audio
    callback timing for the same scenario window.

.PARAMETER ScenarioName
    Label that is embedded in the CSV filename, e.g. "Test1_SingleSource".

.PARAMETER DurationSeconds
    How long to monitor (default 600 = 10 minutes).

.PARAMETER SampleIntervalSeconds
    Seconds between samples (default 1).

.PARAMETER OutputDir
    Directory to write CSV files to (default: script directory).

.PARAMETER AppStatsCsv
    Optional OBS runtime stats CSV written by the frontend exporter. If
    omitted, the script will look for OBS_RUNTIME_STATS.csv inside OutputDir.

.EXAMPLE
    .\obs-perf-monitor.ps1 -ScenarioName "Test1_Baseline" -DurationSeconds 600
    .\obs-perf-monitor.ps1 -ScenarioName "Test4_ComplexScene" -DurationSeconds 600
#>

param(
    [string]  $ScenarioName          = "Test",
    [int]     $DurationSeconds       = 600,
    [int]     $SampleIntervalSeconds = 1,
    [string]  $OutputDir             = $PSScriptRoot,
    [string]  $AppStatsCsv           = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Stats($arr) {
    if (-not $arr -or $arr.Count -eq 0) {
        return @{ Min = 0; Max = 0; Avg = 0; P95 = 0 }
    }

    $sorted = $arr | Sort-Object
    $p95idx = [int][math]::Ceiling($sorted.Count * 0.95) - 1
    @{
        Min = [math]::Round(($sorted | Measure-Object -Minimum).Minimum, 2)
        Max = [math]::Round(($sorted | Measure-Object -Maximum).Maximum, 2)
        Avg = [math]::Round(($sorted | Measure-Object -Average).Average, 2)
        P95 = [math]::Round($sorted[$p95idx], 2)
    }
}

function Get-Delta($rows, [string]$columnName) {
    if (-not $rows -or $rows.Count -eq 0) {
        return 0
    }

    $values = @(
        $rows |
            Where-Object { $_.$columnName -match '^-?\d+(\.\d+)?$' } |
            ForEach-Object { [double]($_.$columnName) }
    )
    if ($values.Count -lt 2) {
        return 0
    }

    return [math]::Round($values[-1] - $values[0], 2)
}

function Get-AppRowsInWindow($csvPath, [datetime]$startUtc, [datetime]$endUtc) {
    if (-not $csvPath -or -not (Test-Path $csvPath)) {
        return @()
    }

    $upperBound = $endUtc.AddSeconds(1)
    $rows = [System.Collections.Generic.List[object]]::new()

    foreach ($row in (Import-Csv $csvPath)) {
        try {
            $ts = [DateTimeOffset]::Parse($row.TimestampUtc).UtcDateTime
        } catch {
            continue
        }

        if ($ts -ge $startUtc -and $ts -le $upperBound) {
            [void]$rows.Add($row)
        }
    }

    return $rows
}

# -- Locate OBS process -------------------------------------------------------
$processName = "obs64"
$proc = Get-Process -Name $processName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    $processName = "obs32"
    $proc = Get-Process -Name $processName -ErrorAction SilentlyContinue | Select-Object -First 1
}
if (-not $proc) {
    Write-Error "OBS process (obs64 / obs32) not found. Launch OBS first, then run this script."
    exit 1
}

$pid_ = $proc.Id

# Resolve the correct PerformanceCounter instance name for this PID.
# When multiple processes share the same name, Windows appends #1, #2, etc.
$counterInstanceName = $processName
$category = New-Object System.Diagnostics.PerformanceCounterCategory("Process")
$instances = $category.GetInstanceNames() | Where-Object { $_ -like "$processName*" }
foreach ($inst in $instances) {
    try {
        $pidCounter = New-Object System.Diagnostics.PerformanceCounter("Process", "ID Process", $inst, $true)
        if ($pidCounter.NextValue() -eq $pid_) {
            $counterInstanceName = $inst
            break
        }
    } catch { }
}

Write-Host "Monitoring $processName (PID $pid_, counter=$counterInstanceName) ..." -ForegroundColor Cyan

# -- Prepare output CSV -------------------------------------------------------
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$csvName   = "${ScenarioName}_${timestamp}.csv"
$csvPath   = Join-Path $OutputDir $csvName
$summaryPath = Join-Path $OutputDir "${ScenarioName}_${timestamp}_SUMMARY.csv"

if (-not $AppStatsCsv) {
    $candidate = Join-Path $OutputDir "OBS_RUNTIME_STATS.csv"
    if (Test-Path $candidate) {
        $AppStatsCsv = $candidate
    }
}

$header = "Timestamp,ElapsedSec,CPU_Pct,WorkingSet_MB,PrivateBytes_MB,Threads,Handles,PagedPool_KB,NonPagedPool_KB"
$header | Out-File -FilePath $csvPath -Encoding UTF8

Write-Host "Writing to: $csvPath"
if ($AppStatsCsv) {
    Write-Host "App stats : $AppStatsCsv"
}
Write-Host "Duration  : $DurationSeconds s   (Ctrl+C to stop early)"
Write-Host ("-" * 72)
Write-Host ("{0,-24} {1,7} {2,8} {3,12} {4,10}" -f "Time","CPU%","WS(MB)","Private(MB)","Threads")
Write-Host ("-" * 72)

# -- CPU counter helper -------------------------------------------------------
$cpuCounter  = New-Object System.Diagnostics.PerformanceCounter("Process", "% Processor Time", $counterInstanceName, $true)
$null = $cpuCounter.NextValue()
Start-Sleep -Milliseconds 500

$logicalCores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$startTime    = Get-Date
$scenarioStartUtc = [DateTime]::UtcNow
$elapsed      = 0

# -- Sampling loop ------------------------------------------------------------
try {
    while ($elapsed -lt $DurationSeconds) {
        Start-Sleep -Seconds $SampleIntervalSeconds

        $proc = Get-Process -Id $pid_ -ErrorAction SilentlyContinue
        if (-not $proc) {
            Write-Warning "OBS process ended - stopping monitor."
            break
        }

        $now      = Get-Date
        $elapsed  = [int]($now - $startTime).TotalSeconds

        $rawCpu   = $cpuCounter.NextValue()
        $cpuPct   = [math]::Round($rawCpu / $logicalCores, 1)
        $wsMB     = [math]::Round($proc.WorkingSet64 / 1MB, 1)
        $privMB   = [math]::Round($proc.PrivateMemorySize64 / 1MB, 1)
        $threads  = $proc.Threads.Count
        $handles  = $proc.HandleCount

        $pagedKB    = "N/A"
        $nonPagedKB = "N/A"
        if ($elapsed % 5 -eq 0) {
            try {
                $wmi = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId=$pid_" -Property PageFaults,QuotaPagedPoolUsage,QuotaNonPagedPoolUsage
                $pagedKB    = [math]::Round($wmi.QuotaPagedPoolUsage / 1KB, 0)
                $nonPagedKB = [math]::Round($wmi.QuotaNonPagedPoolUsage / 1KB, 0)
            } catch { }
        }

        $line = "$($now.ToString('HH:mm:ss.fff')),$elapsed,$cpuPct,$wsMB,$privMB,$threads,$handles,$pagedKB,$nonPagedKB"
        $line | Out-File -FilePath $csvPath -Append -Encoding UTF8

        Write-Host ("{0,-24} {1,6}% {2,8} {3,12} {4,10}" -f $now.ToString('HH:mm:ss'), $cpuPct, $wsMB, $privMB, $threads)
    }
} finally {
    $cpuCounter.Dispose()
}

$scenarioEndUtc = [DateTime]::UtcNow

Write-Host ""
Write-Host "Monitor complete. CSV saved to:" -ForegroundColor Green
Write-Host "  $csvPath" -ForegroundColor Green

# -- Summary statistics -------------------------------------------------------
$data = Import-Csv $csvPath
$cpuValues  = @($data | Where-Object { $_.CPU_Pct -match '^\d' } | ForEach-Object { [double]$_.CPU_Pct })
$wsMBValues = @($data | ForEach-Object { [double]$_.WorkingSet_MB })
$cpuStats = Get-Stats $cpuValues
$wsStats  = Get-Stats $wsMBValues

$appRows = Get-AppRowsInWindow $AppStatsCsv $scenarioStartUtc $scenarioEndUtc
$appCpuStats = Get-Stats @($appRows | ForEach-Object { [double]$_.OBS_CPU_Pct })
$fpsStats = Get-Stats @($appRows | ForEach-Object { [double]$_.ActiveFPS })
$frameStats = Get-Stats @($appRows | ForEach-Object { [double]$_.AvgFrameTime_ms })
$audioAvgStats = Get-Stats @($appRows | ForEach-Object { [double]$_.AudioCallbackAvg_ms })
$audioLastStats = Get-Stats @($appRows | ForEach-Object { [double]$_.AudioCallbackLast_ms })

$laggedFrames = Get-Delta $appRows "LaggedFrames"
$skippedFrames = Get-Delta $appRows "SkippedFrames"
$streamDropped = Get-Delta $appRows "StreamDroppedFrames"
$recordDropped = Get-Delta $appRows "RecordDroppedFrames"
$graphRebuilds = Get-Delta $appRows "AudioGraphRebuilds"
$parallelTicks = Get-Delta $appRows "AudioParallelTicks"
$serialTicks = Get-Delta $appRows "AudioSerialTicks"
$audioThreads = if ($appRows.Count -gt 0) { [int]$appRows[-1].AudioRenderThreads } else { 0 }
$audioPeakJobs = if ($appRows.Count -gt 0) { [int]$appRows[-1].AudioPeakParallelJobs } else { 0 }

Write-Host ""
Write-Host "=== Summary: $ScenarioName ======================================" -ForegroundColor Cyan
Write-Host ("CPU %        - min: {0,7}  avg: {1,7}  p95: {2,7}  max: {3,7}" -f $cpuStats.Min, $cpuStats.Avg, $cpuStats.P95, $cpuStats.Max)
Write-Host ("WorkingSet MB- min: {0,7}  avg: {1,7}  p95: {2,7}  max: {3,7}" -f $wsStats.Min, $wsStats.Avg, $wsStats.P95, $wsStats.Max)
if ($appRows.Count -gt 0) {
    Write-Host ("OBS CPU %    - min: {0,7}  avg: {1,7}  p95: {2,7}  max: {3,7}" -f $appCpuStats.Min, $appCpuStats.Avg, $appCpuStats.P95, $appCpuStats.Max)
    Write-Host ("Active FPS   - min: {0,7}  avg: {1,7}  p95: {2,7}  max: {3,7}" -f $fpsStats.Min, $fpsStats.Avg, $fpsStats.P95, $fpsStats.Max)
    Write-Host ("Frame Time ms- min: {0,7}  avg: {1,7}  p95: {2,7}  max: {3,7}" -f $frameStats.Min, $frameStats.Avg, $frameStats.P95, $frameStats.Max)
    Write-Host ("Audio CB ms  - avg: {0,7}  p95: {1,7}  max(last): {2,7}" -f $audioAvgStats.Avg, $audioAvgStats.P95, $audioLastStats.Max)
    Write-Host ("Drops/Lag    - lagged: {0}  skipped: {1}  stream dropped: {2}  record dropped: {3}" -f $laggedFrames, $skippedFrames, $streamDropped, $recordDropped)
    Write-Host ("Audio pool   - threads: {0}  graph rebuilds: {1}  parallel ticks: {2}  serial ticks: {3}  peak jobs: {4}" -f $audioThreads, $graphRebuilds, $parallelTicks, $serialTicks, $audioPeakJobs)
} else {
    Write-Host "OBS runtime stats were not available for this scenario." -ForegroundColor Yellow
}
Write-Host "Samples      : $($data.Count)"

$summaryObject = [pscustomobject][ordered]@{
    Scenario                = $ScenarioName
    DurationSeconds         = $DurationSeconds
    Samples                 = $data.Count
    ProcessCSV              = $csvName
    AppStatsCSV             = if ($AppStatsCsv) { Split-Path $AppStatsCsv -Leaf } else { "" }
    ProcessAvgCPU_Pct       = $cpuStats.Avg
    ProcessP95CPU_Pct       = $cpuStats.P95
    ProcessMaxCPU_Pct       = $cpuStats.Max
    ProcessAvgWorkingSet_MB = $wsStats.Avg
    ProcessP95WorkingSet_MB = $wsStats.P95
    ProcessMaxWorkingSet_MB = $wsStats.Max
    AppSamples              = $appRows.Count
    OBSAvgCPU_Pct           = $appCpuStats.Avg
    OBSP95CPU_Pct           = $appCpuStats.P95
    OBSMaxCPU_Pct           = $appCpuStats.Max
    AvgActiveFPS            = $fpsStats.Avg
    P95ActiveFPS            = $fpsStats.P95
    AvgFrameTime_ms         = $frameStats.Avg
    P95FrameTime_ms         = $frameStats.P95
    MaxFrameTime_ms         = $frameStats.Max
    LaggedFrames            = $laggedFrames
    SkippedFrames           = $skippedFrames
    StreamDroppedFrames     = $streamDropped
    RecordDroppedFrames     = $recordDropped
    AvgAudioCallback_ms     = $audioAvgStats.Avg
    P95AudioCallback_ms     = $audioAvgStats.P95
    MaxAudioCallback_ms     = $audioLastStats.Max
    AudioRenderThreads      = $audioThreads
    AudioGraphRebuilds      = $graphRebuilds
    AudioParallelTicks      = $parallelTicks
    AudioSerialTicks        = $serialTicks
    AudioPeakParallelJobs   = $audioPeakJobs
}

$summaryObject | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

Write-Host ""
Write-Host "Scenario summary CSV:" -ForegroundColor Green
Write-Host "  $summaryPath" -ForegroundColor Green
Write-Host ""
Write-Host "Copy the summary above into PERFORMANCE_TEST_RESULTS.md." -ForegroundColor Yellow
