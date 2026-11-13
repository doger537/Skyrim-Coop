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
function Read-BackupState {
    $state = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
    if ($state.GamePath -ne $game -or $state.Version -ne 1) { throw 'Backup belongs to another game directory or format.' }
    $seen = @{}
    foreach ($item in $state.Files) {
        if ($item.Name -notmatch '^cc[a-zA-Z0-9_ -]+\.(esm|esp|esl|bsa)$' -or $item.SHA256 -notmatch '^[a-fA-F0-9]{64}$' -or $seen.ContainsKey($item.Name)) { throw 'Unsafe or duplicate backup entry.' }
        $seen[$item.Name] = $true
        $original = Join-Path $data $item.Name
        $saved = Join-Path $backup $item.Name
        if (!(Test-Path -LiteralPath $original) -and !(Test-Path -LiteralPath $saved)) { throw "Missing original and backup: $($item.Name)" }
        foreach ($path in @($original,$saved)) {
            if (Test-Path -LiteralPath $path) {
                Write-Progress -Activity 'Checking CommonSE backup' -Status $item.Name
                if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path).Hash -ne $item.SHA256) { throw "Changed file: $path. Nothing will be overwritten; keep the backup." }
            }
        }
    }
    Write-Progress -Activity 'Checking CommonSE backup' -Completed
    return $state
}
function Preserve-Duplicate([string]$Path) {
    # This is a move inside the same backup root, never deletion or replacement.
    $history = Join-Path $backup ('History\' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $history -Force | Out-Null
    Move-Item -LiteralPath $Path -Destination (Join-Path $history ([IO.Path]::GetFileName($Path)))
}
if ($Action -eq 'Restore') {
    Assert-Stopped
    if (!(Test-Path -LiteralPath $journal)) { throw 'No CommonSE backup to restore.' }
    # Validate BOTH copies of every file before moving anything.
    $state = Read-BackupState
    foreach ($item in $state.Files) {
        $source = Join-Path $backup $item.Name
        if (Test-Path -LiteralPath $source) {
            $destination = Join-Path $data $item.Name
            if (Test-Path -LiteralPath $destination) { Preserve-Duplicate $source }
            else { Move-Item -LiteralPath $source -Destination $destination }
        }
    }
    foreach ($item in $state.Files) {
        Write-Progress -Activity 'Verifying restored files' -Status $item.Name
        if ((Get-FileHash -LiteralPath (Join-Path $data $item.Name)).Hash -ne $item.SHA256) { throw "File changed during restore: $($item.Name). Keep the backup and stop other file operations." }
    }
    Write-Progress -Activity 'Verifying restored files' -Completed
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
[pscustomobject]@{ Mode='CommonSE'; ToolsVersion='fix2'; GamePath=$game; CreationPlugins=$creations.Count; FilesToPreserve=$files.Count; BlockingDependencies=$blockers; Note='This is a plan before changes. Wait for CommonSE enabled. Use a new co-op save; other mods must match.' } | Format-List
if ($blockers.Count) { throw ('Disable dependent mods in the separate Vortex co-op profile first: ' + ($blockers -join '; ')) }
if ($Action -eq 'Enable') {
    Assert-Stopped
    if ($Launch -and !(Test-Path -LiteralPath (Join-Path $data 'SkyrimTogetherReborn\SkyrimTogether.exe'))) { throw 'Install the same TrueCoop build on both PCs first; no files have been moved.' }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $game 'SkyrimSE.exe')).FileVersion
    if ($version -notmatch '^1\.7\.104(\.0)?$') { throw "This package targets Steam 1.7.104.0, found $version. Both players need the same supported runtime." }
    $known = @{}
    if (Test-Path -LiteralPath $journal) {
        $previous = Read-BackupState
        foreach ($item in $previous.Files) { $known[$item.Name] = $item }
    }
    foreach ($file in $files) {
        if (!$known.ContainsKey($file.Name)) {
            if ($file.Name -notmatch '^cc[a-zA-Z0-9_ -]+\.(esm|esp|esl|bsa)$') { throw "Unsupported Creation filename: $($file.Name). No files have been moved." }
            if (Test-Path -LiteralPath (Join-Path $backup $file.Name)) { throw "Untracked backup file: $($file.Name). Keep the backup and resolve this before continuing." }
            Write-Progress -Activity 'Preparing CommonSE' -Status $file.Name
            $known[$file.Name] = [pscustomobject]@{Name=$file.Name;SHA256=(Get-FileHash -LiteralPath $file.FullName).Hash}
        }
    }
    Write-Progress -Activity 'Preparing CommonSE' -Completed
    if ($known.Count) {
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        $entries = @($known.Values | Sort-Object Name)
        # Commit the merged journal before any move, retaining its previous version.
        $next = Join-Path $backup ('manifest.next-' + [guid]::NewGuid().ToString('N') + '.json')
        [pscustomobject]@{Version=1;GamePath=$game;Files=$entries} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $next -Encoding UTF8
        if (Test-Path -LiteralPath $journal) {
            $old = Join-Path $backup ('manifest.previous-' + [guid]::NewGuid().ToString('N') + '.json')
            [IO.File]::Replace($next,$journal,$old)
        } else { [IO.File]::Move($next,$journal) }
        foreach ($item in $entries) {
            $source = Join-Path $data $item.Name
            $saved = Join-Path $backup $item.Name
            if (Test-Path -LiteralPath $source) {
                # Recheck immediately before the move as well as during whole-plan validation.
                if ((Get-FileHash -LiteralPath $source).Hash -ne $item.SHA256) { throw "File changed during preparation: $($item.Name). Backup preserved; do not launch until resolved." }
                if (Test-Path -LiteralPath $saved) { Preserve-Duplicate $saved }
                Move-Item -LiteralPath $source -Destination $saved
            }
        }
    }
    $remaining = @(Get-ChildItem -LiteralPath $data -File | Where-Object { Test-CreationName $_.Name })
    if ($remaining.Count) { throw 'Creation plugins reappeared during preparation. Stop Steam/Vortex file operations and retry; game not launched.' }
    Write-Output 'CommonSE enabled. Creations are preserved in TrueCoop-CommonSE-Backup. Do not load a save that used them.'
    Write-Output 'Active Creation plugins remaining: 0'
}
if ($Launch) {
    if ($Action -ne 'Enable') { throw 'Launch requires -Action Enable.' }
    $launcher = Join-Path $data 'SkyrimTogetherReborn\SkyrimTogether.exe'
    if (!(Test-Path -LiteralPath $launcher)) { throw 'Install the same TrueCoop build on both PCs first.' }
    Start-Process -FilePath $launcher -WorkingDirectory $game
}
