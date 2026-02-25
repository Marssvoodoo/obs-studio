<#
.SYNOPSIS
    OBS Studio Performance Monitor — Phase 3 benchmark data collector.

.DESCRIPTION
    Continuously samples the obs64 (or obs32) process and writes CPU,
    working-set, private bytes, thread count and handle count to a
    timestamped CSV file.  Run this script while running each test
    scenario documented in PERFORMANCE_TEST_RESULTS.md.

.PARAMETER ScenarioName
    Label that is embedded in the CSV filename, e.g. "Test1_SingleSource".

.PARAMETER DurationSeconds
    How long to monitor (default 600 = 10 minutes).

.PARAMETER SampleIntervalSeconds
    Seconds between samples (default 1).

.PARAMETER OutputDir
    Directory to write CSV files to (default: script directory).

.EXAMPLE
    .\obs-perf-monitor.ps1 -ScenarioName "Test1_Baseline" -DurationSeconds 600
    .\obs-perf-monitor.ps1 -ScenarioName "Test4_ComplexScene" -DurationSeconds 600
#>

param(
    [string]  $ScenarioName          = "Test",
    [int]     $DurationSeconds       = 600,
    [int]     $SampleIntervalSeconds = 1,
    [string]  $OutputDir             = $PSScriptRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Locate OBS process ────────────────────────────────────────────────────────
$processName = "obs64"
$proc = Get-Process -Name $processName -ErrorAction SilentlyContinue
if (-not $proc) {
    $processName = "obs32"
    $proc = Get-Process -Name $processName -ErrorAction SilentlyContinue
}
if (-not $proc) {
    Write-Error "OBS process (obs64 / obs32) not found.  Launch OBS first, then run this script."
    exit 1
}

$pid_ = $proc.Id
Write-Host "Monitoring $processName (PID $pid_) …" -ForegroundColor Cyan

# ── Prepare output CSV ────────────────────────────────────────────────────────
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$csvName   = "${ScenarioName}_${timestamp}.csv"
$csvPath   = Join-Path $OutputDir $csvName

$header = "Timestamp,ElapsedSec,CPU_Pct,WorkingSet_MB,PrivateBytes_MB,Threads,Handles,PagedPool_KB,NonPagedPool_KB"
$header | Out-File -FilePath $csvPath -Encoding UTF8

Write-Host "Writing to: $csvPath"
Write-Host "Duration  : $DurationSeconds s   (Ctrl+C to stop early)"
Write-Host ("-" * 72)
Write-Host ("{0,-24} {1,7} {2,8} {3,12} {4,10}" -f "Time","CPU%","WS(MB)","Private(MB)","Threads")
Write-Host ("-" * 72)

# ── CPU counter helper ────────────────────────────────────────────────────────
$cpuCounter  = New-Object System.Diagnostics.PerformanceCounter("Process", "% Processor Time", $processName, $true)
$null = $cpuCounter.NextValue()   # first call always returns 0; discard it
Start-Sleep -Milliseconds 500

$logicalCores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$startTime    = Get-Date
$elapsed      = 0

# ── Sampling loop ─────────────────────────────────────────────────────────────
try {
    while ($elapsed -lt $DurationSeconds) {
        Start-Sleep -Seconds $SampleIntervalSeconds

        $proc = Get-Process -Id $pid_ -ErrorAction SilentlyContinue
        if (-not $proc) {
            Write-Warning "OBS process ended — stopping monitor."
            break
        }

        $now      = Get-Date
        $elapsed  = [int]($now - $startTime).TotalSeconds

        # CPU % normalised to one core (divide raw value by logical-core count)
        $rawCpu   = $cpuCounter.NextValue()
        $cpuPct   = [math]::Round($rawCpu / $logicalCores, 1)

        $wsMB     = [math]::Round($proc.WorkingSet64   / 1MB, 1)
        $privMB   = [math]::Round($proc.PrivateMemorySize64 / 1MB, 1)
        $threads  = $proc.Threads.Count
        $handles  = $proc.HandleCount

        # Paged / Non-paged pool via WMI (slower; sampled every 5 s to reduce overhead)
        $pagedKB    = "N/A"
        $nonPagedKB = "N/A"
        if ($elapsed % 5 -eq 0) {
            try {
                $wmi = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId=$pid_" -Property PageFaults,QuotaPagedPoolUsage,QuotaNonPagedPoolUsage
                $pagedKB    = [math]::Round($wmi.QuotaPagedPoolUsage    / 1KB, 0)
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

Write-Host ""
Write-Host "Monitor complete.  CSV saved to:" -ForegroundColor Green
Write-Host "  $csvPath" -ForegroundColor Green

# ── Summary statistics ────────────────────────────────────────────────────────
$data = Import-Csv $csvPath

$cpuValues  = $data | Where-Object { $_.CPU_Pct -match '^\d' } | ForEach-Object { [double]$_.CPU_Pct }
$wsMBValues = $data | ForEach-Object { [double]$_.WorkingSet_MB }

function Get-Stats($arr) {
    if (-not $arr) { return @{ Min=0; Max=0; Avg=0; P95=0 } }
    $sorted = $arr | Sort-Object
    $p95idx = [int][math]::Ceiling($sorted.Count * 0.95) - 1
    @{
        Min = [math]::Round(($sorted | Measure-Object -Minimum).Minimum, 2)
        Max = [math]::Round(($sorted | Measure-Object -Maximum).Maximum, 2)
        Avg = [math]::Round(($sorted | Measure-Object -Average).Average, 2)
        P95 = [math]::Round($sorted[$p95idx], 2)
    }
}

$cpuStats = Get-Stats $cpuValues
$wsStats  = Get-Stats $wsMBValues

Write-Host ""
Write-Host "═══ Summary: $ScenarioName ══════════════════════════════════════" -ForegroundColor Cyan
Write-Host ("CPU %        — min: {0,7}  avg: {1,7}  p95: {2,7}  max: {3,7}" -f $cpuStats.Min, $cpuStats.Avg, $cpuStats.P95, $cpuStats.Max)
Write-Host ("WorkingSet MB— min: {0,7}  avg: {1,7}  p95: {2,7}  max: {3,7}" -f $wsStats.Min, $wsStats.Avg, $wsStats.P95, $wsStats.Max)
Write-Host "Samples     : $($data.Count)"
Write-Host ""
Write-Host "Copy the summary above into PERFORMANCE_TEST_RESULTS.md." -ForegroundColor Yellow
