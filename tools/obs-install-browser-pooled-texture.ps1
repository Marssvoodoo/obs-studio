param([switch] $VerifyOnly, [switch] $Rollback)
$ErrorActionPreference = 'Stop'

# Run elevated after OBS is closed. This script never stops or launches OBS.
$releaseRoot = 'L:\Coding\_obs_releases\20260930-browser-pooled-texture'
$manifestPath = Join-Path $releaseRoot 'manifest.json'
$manifestHash = '363C5233471978A233CE24E542659D4AC3F96865DD45AA0030D3A88A603AA1E7'
$installRoot = 'C:\Program Files\obs-studio\obs-plugins\64bit'
$payloadRoot = Join-Path $releaseRoot 'payload'
$backupRoot = 'L:\Coding\_obs_install_backup\obs-browser-pre-pooled-texture-20260930'

function Assert-RegularItem([string] $path) {
    $item = Get-Item -LiteralPath $path
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Refusing a redirected installation or inventory path: $path"
    }
    return $item
}

function Assert-File([string] $path, [string] $hash, [long] $length) {
    $item = Assert-RegularItem $path
    if ($item.PSIsContainer -or $item.Length -ne $length -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hash) {
        throw "Inventory changed: $path"
    }
}

if (-not $VerifyOnly -and (Get-Process -Name obs64 -ErrorAction SilentlyContinue)) {
    Write-Output 'Deployment deferred: OBS is open. No installation files were changed.'
    exit 20
}

Assert-File $manifestPath $manifestHash (Get-Item -LiteralPath $manifestPath).Length
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.install_root -ne $installRoot -or $manifest.payload_root -ne $payloadRoot -or
    $manifest.backup_root -ne $backupRoot -or
    $manifest.browser_commit -ne '295e9816efcec0c4e655a2c5dede0a548e92e3e6' -or
    $manifest.files.Count -ne 2 -or
    (($manifest.files.name | Sort-Object) -join ',') -ne 'obs-browser.dll,obs-browser.pdb') {
    throw 'Unexpected release manifest.'
}
foreach ($root in @($releaseRoot, $payloadRoot, $backupRoot, $installRoot,
                    'C:\Program Files\obs-studio', 'C:\Program Files\obs-studio\obs-plugins')) {
    Assert-RegularItem $root | Out-Null
}
foreach ($entry in $manifest.files) {
    Assert-File (Join-Path $payloadRoot $entry.name) $entry.sha256 $entry.length
    Assert-File (Join-Path $backupRoot $entry.name) $entry.previous_sha256 $entry.previous_length
    $installed = Join-Path $installRoot $entry.name
    $item = Assert-RegularItem $installed
    $hash = (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash
    if (-not (($hash -eq $entry.previous_sha256 -and $item.Length -eq $entry.previous_length) -or
              ($hash -eq $entry.sha256 -and $item.Length -eq $entry.length))) {
        throw "Installed file changed since backup: $($entry.name)"
    }
}
if ($VerifyOnly) {
    [pscustomobject]@{
        browser_commit = $manifest.browser_commit
        manifest_sha256 = $manifestHash
        payload_and_backup_verified = $true
        files = $manifest.files.Count
        obs_open = [bool](Get-Process -Name obs64 -ErrorAction SilentlyContinue)
        installation_changed = $false
    } | ConvertTo-Json
    exit 0
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run elevated PowerShell after OBS is closed.'
}
if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
    throw 'OBS started during preflight; no files copied.'
}
foreach ($entry in $manifest.files) {
    if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
        throw 'OBS started during deployment; stop and inspect before continuing.'
    }
    $sourceRoot = if ($Rollback) { $backupRoot } else { $payloadRoot }
    $expectedHash = if ($Rollback) { $entry.previous_sha256 } else { $entry.sha256 }
    $expectedLength = if ($Rollback) { $entry.previous_length } else { $entry.length }
    $source = Join-Path $sourceRoot $entry.name
    $destination = Join-Path $installRoot $entry.name
    Assert-File $source $expectedHash $expectedLength
    Copy-Item -LiteralPath $source -Destination $destination -Force
    Assert-File $destination $expectedHash $expectedLength
}
if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
    throw 'OBS opened during deployment. Inspect the loaded module before claiming activation.'
}
[pscustomobject]@{
    installed_utc = [DateTime]::UtcNow.ToString('o')
    browser_commit = $manifest.browser_commit
    parent_build_baseline = $manifest.parent_baseline
    manifest_sha256 = $manifestHash
    verified_files = $manifest.files.Count
    rollback = [bool]$Rollback
    obs_open = $false
    runtime_qa = 'Pending: open OBS and inspect an animated accelerated browser source.'
} | ConvertTo-Json | Tee-Object -FilePath (Join-Path $releaseRoot ('deployment-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '.json'))
