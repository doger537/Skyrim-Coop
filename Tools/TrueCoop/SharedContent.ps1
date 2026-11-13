[CmdletBinding()]
param(
    [ValidateSet('Plan','Enable','Restore')][string]$Action = 'Plan',
    [string]$GamePath = '',
    [switch]$Launch
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($GamePath)) {
    # Windows PowerShell -File can evaluate parameter defaults before PSScriptRoot is set.
    # Resolve only after parameter binding; never depend on the caller's working directory.
    $scriptDirectory = $PSScriptRoot
    if ([string]::IsNullOrWhiteSpace($scriptDirectory)) {
        $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    }
    if ([string]::IsNullOrWhiteSpace($scriptDirectory)) { throw 'Cannot locate SharedContent.ps1. Specify -GamePath explicitly.' }
    $GamePath = Join-Path $scriptDirectory '../..'
}
$game = (Resolve-Path -LiteralPath $GamePath).Path.TrimEnd('\')
$data = Join-Path $game 'Data'
if (!(Test-Path -LiteralPath (Join-Path $game 'SkyrimSE.exe')) -or !(Test-Path -LiteralPath $data)) { throw 'Select the Skyrim Special Edition game directory.' }
foreach ($name in @('Skyrim.esm','Update.esm','Dawnguard.esm','HearthFires.esm','Dragonborn.esm')) {
    if (!(Test-Path -LiteralPath (Join-Path $data $name))) { throw "Missing common SE master: $name" }
}
$backup = Join-Path $game 'TrueCoop-CommonSE-Backup'
$journal = Join-Path $backup 'manifest.json'
function Test-CreationName([string]$Name) { return $Name -match '^cc[a-zA-Z0-9_-]+\.(esm|esl|esp)$' }
function Get-Masters([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'TES4') { throw "Not a TES4 plugin: $Path" }
        $length = $reader.ReadUInt32()
        if ($length -gt 16MB -or $stream.Length -lt (24 + $length)) { throw "Invalid plugin header: $Path" }
        $stream.Position = 24
        $end = 24 + $length
        while ($stream.Position -lt $end) {
            if (($end - $stream.Position) -lt 6) { throw "Truncated subrecord: $Path" }
            $type = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
            $size = $reader.ReadUInt16()
            if ($type -eq 'XXXX' -or ($stream.Position + $size) -gt $end) { throw "Unsupported or invalid header: $Path" }
            $bytes = $reader.ReadBytes($size)
            if ($type -eq 'MAST') { [Text.Encoding]::ASCII.GetString($bytes).TrimEnd([char]0) }
        }
    } finally { $reader.Dispose(); $stream.Dispose() }
}
function Assert-Stopped {
    if (Get-Process SkyrimSE,SkyrimTogether,SkyrimSELauncher -ErrorAction SilentlyContinue) { throw 'Close Skyrim and its launchers first.' }
}
if ($Action -eq 'Restore') {
    Assert-Stopped
    if (!(Test-Path -LiteralPath $journal)) { throw 'No CommonSE backup to restore.' }
    $state = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
    if ($state.GamePath -ne $game -or $state.Version -ne 1) { throw 'Backup belongs to another game directory or format.' }
    # Validate the entire restore before moving any file. Never replace a file changed by Steam/Vortex.
    foreach ($item in $state.Files) {
        if ($item.Name -notmatch '^cc[a-zA-Z0-9_ -]+\.(esm|esp|esl|bsa)$') { throw 'Unsafe backup filename.' }
        $source = Join-Path $backup $item.Name
        $destination = Join-Path $data $item.Name
        if (Test-Path -LiteralPath $source) {
            if ((Get-FileHash -LiteralPath $source).Hash -ne $item.SHA256 -or (Test-Path -LiteralPath $destination)) { throw "Restore conflict: $($item.Name). Resolve the Steam/Vortex change first; backup is preserved." }
        } elseif (!(Test-Path -LiteralPath $destination) -or (Get-FileHash -LiteralPath $destination).Hash -ne $item.SHA256) { throw "Missing or changed restore file: $($item.Name)" }
    }
    foreach ($item in $state.Files) {
        $source = Join-Path $backup $item.Name
        if (Test-Path -LiteralPath $source) { Move-Item -LiteralPath $source -Destination (Join-Path $data $item.Name) }
    }
    # Keep the journal as evidence; next enable verifies that every old file has returned.
    Write-Output 'Creation files restored. Re-select your original Vortex profile before solo play.'
    exit
}
$plugins = @(Get-ChildItem -LiteralPath $data -File | Where-Object { $_.Extension -in @('.esm','.esl','.esp') })
$creations = @($plugins | Where-Object { Test-CreationName $_.Name })
$files = @($creations)
foreach ($creation in $creations) {
    $stem = [IO.Path]::GetFileNameWithoutExtension($creation.Name)
    $files += @(Get-ChildItem -LiteralPath $data -File | Where-Object { $_.Extension -eq '.bsa' -and ($_.BaseName -eq $stem -or $_.BaseName.StartsWith($stem + ' - ', [StringComparison]::OrdinalIgnoreCase)) })
}
$files = @($files | Sort-Object FullName -Unique)
$blockers = @()
foreach ($plugin in $plugins) {
    if (Test-CreationName $plugin.Name) { continue }
    foreach ($master in @(Get-Masters $plugin.FullName)) {
        if (Test-CreationName $master) { $blockers += "$($plugin.Name) requires $master" }
    }
}
[pscustomobject]@{ Mode='CommonSE'; GamePath=$game; CreationPlugins=$creations.Count; FilesToPreserve=$files.Count; BlockingDependencies=$blockers; Note='All cc* plugins are excluded, including the four free Creations. Use a NEW Vortex profile and NEW character. Other mods still must match.' } | Format-List
if ($blockers.Count) { throw ('Disable dependent mods in the separate Vortex co-op profile first: ' + ($blockers -join '; ')) }
if ($Action -eq 'Enable') {
    Assert-Stopped
    if ($Launch -and !(Test-Path -LiteralPath (Join-Path $data 'SkyrimTogetherReborn\SkyrimTogether.exe'))) { throw 'Install the same TrueCoop build on both PCs first; no files have been moved.' }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $game 'SkyrimSE.exe')).FileVersion
    if ($version -notmatch '^1\.7\.104(\.0)?$') { throw "This package targets Steam 1.7.104.0, found $version. Both players need the same supported runtime." }
    if ($files.Count) {
        if (Test-Path -LiteralPath $journal) {
            $previous = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
            if ($previous.GamePath -ne $game) { throw 'Backup path conflict.' }
            foreach ($item in $previous.Files) {
                if ($item.Name -notmatch '^cc[a-zA-Z0-9_ -]+\.(esm|esp|esl|bsa)$' -or (Test-Path -LiteralPath (Join-Path $backup $item.Name))) { throw 'A backup is still active. Restore it before Steam/Vortex deployment or another enable.' }
                $restored = Join-Path $data $item.Name
                if (!(Test-Path -LiteralPath $restored) -or (Get-FileHash -LiteralPath $restored).Hash -ne $item.SHA256) { throw 'Restored files changed. Preserve the previous backup journal before creating a new profile.' }
            }
        }
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        $entries = @(foreach ($file in $files) {
            if (Test-Path -LiteralPath (Join-Path $backup $file.Name)) { throw "Backup already contains $($file.Name)" }
            [pscustomobject]@{Name=$file.Name;SHA256=(Get-FileHash -LiteralPath $file.FullName).Hash}
        })
        # Persist the full plan before the first move; interrupted runs can use Restore.
        [pscustomobject]@{Version=1;GamePath=$game;Files=$entries} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $journal -Encoding UTF8
        foreach ($file in $files) { Move-Item -LiteralPath $file.FullName -Destination (Join-Path $backup $file.Name) }
    }
    Write-Output 'CommonSE enabled. Creations are preserved in TrueCoop-CommonSE-Backup. Do not load a save that used them.'
}
if ($Launch) {
    if ($Action -ne 'Enable') { throw 'Launch requires -Action Enable.' }
    $launcher = Join-Path $data 'SkyrimTogetherReborn\SkyrimTogether.exe'
    if (!(Test-Path -LiteralPath $launcher)) { throw 'Install the same TrueCoop build on both PCs first.' }
    Start-Process -FilePath $launcher -WorkingDirectory $game
}
