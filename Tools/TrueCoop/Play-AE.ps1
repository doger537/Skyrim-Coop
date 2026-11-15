[CmdletBinding()]
param(
    [string]$GamePath = '',
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($GamePath)) {
    # Resolve after parameter binding: Windows PowerShell 5.1 -File can leave
    # PSScriptRoot empty while evaluating parameter defaults.
    $scriptDirectory = $PSScriptRoot
    if ([string]::IsNullOrWhiteSpace($scriptDirectory)) { $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path }
    if ([string]::IsNullOrWhiteSpace($scriptDirectory)) { throw 'Cannot locate Play-AE.ps1. Specify -GamePath explicitly.' }
    $GamePath = Join-Path $scriptDirectory '../..'
}
$game = (Resolve-Path -LiteralPath $GamePath).Path.TrimEnd('\')
$data = Join-Path $game 'Data'
$gameExe = Join-Path $game 'SkyrimSE.exe'
$launcher = Join-Path $data 'SkyrimTogetherReborn\SkyrimTogether.exe'
if (!(Test-Path -LiteralPath $gameExe -PathType Leaf) -or !(Test-Path -LiteralPath $data -PathType Container)) { throw 'Select the Skyrim Special Edition game directory.' }
if (!(Test-Path -LiteralPath $launcher -PathType Leaf)) { throw 'TrueCoop launcher is missing. Install and deploy the full AE package first.' }
$version = [Diagnostics.FileVersionInfo]::GetVersionInfo($gameExe).FileVersion
if ($version -notmatch '^1\.7\.104(\.0)?$') { throw "This AE package targets SkyrimSE.exe 1.7.104.0; found $version. Use the matching client and runtime on both PCs." }
if (!$CheckOnly -and (Get-Process SkyrimSE,SkyrimTogether,SkyrimSELauncher -ErrorAction SilentlyContinue)) { throw 'Close Skyrim and its launchers before starting AE.' }

$backup = Join-Path $game 'TrueCoop-CommonSE-Backup'
$journal = Join-Path $backup 'manifest.json'
if (Test-Path -LiteralPath $journal) {
    if (!(Test-Path -LiteralPath $journal -PathType Leaf)) { throw 'CommonSE backup journal is not a file. Preserve the backup and resolve it before launch.' }
    $state = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
    if ($state.Version -ne 1 -or $state.GamePath -ne $game) { throw 'CommonSE backup belongs to another game directory or format. Preserve it; do not launch or restore into this installation.' }
    $seen = @{}
    foreach ($item in $state.Files) {
        if ($item.Name -notmatch '^cc[a-zA-Z0-9_ -]+\.(esm|esp|esl|bsa)$' -or $item.SHA256 -notmatch '^[a-fA-F0-9]{64}$' -or $seen.ContainsKey($item.Name)) { throw 'Unsafe or duplicate CommonSE backup entry. Preserve the journal and resolve it before launch.' }
        $seen[$item.Name] = $true
        $original = Join-Path $data $item.Name
        $saved = Join-Path $backup $item.Name
        if (!(Test-Path -LiteralPath $original -PathType Leaf) -and !(Test-Path -LiteralPath $saved -PathType Leaf)) { throw "CommonSE file is missing from both Data and its backup: $($item.Name). Preserve the backup and recover this file before launch." }
    }
}
# Only files directly in the backup root are pending. History holds deliberately
# preserved duplicates and must not block a successfully restored AE installation.
$pending = @()
if (Test-Path -LiteralPath $backup -PathType Container) {
    $pending = @(Get-ChildItem -LiteralPath $backup -File | Where-Object { $_.Name -match '^cc.*\.(esm|esl|esp|bsa)$' })
}
if ($pending.Count) { throw "CommonSE backup still contains $($pending.Count) Creation files. Close Skyrim and run Restore-AE-Creations.cmd first. This launcher has moved nothing." }
$creations = @(Get-ChildItem -LiteralPath $data -File | Where-Object { $_.Name -match '^cc[a-zA-Z0-9_-]+\.(esm|esl|esp)$' })
if (!$creations.Count) { throw 'No active Creation plugins found in Data. This is the full AE preset: restore your legally installed Creations before using Play-AE.cmd.' }
[pscustomobject]@{
    Mode = 'AE experimental'
    GamePath = $game
    Runtime = $version
    CreationPluginsPresent = $creations.Count
    CheckOnly = [bool]$CheckOnly
    Note = 'Creations are kept in Data. Both clients and server must use the same AE build and matching active content.'
} | Format-List
if ($CheckOnly) {
    Write-Output 'AE checks passed. No game launched and no files changed.'
    return
}
Write-Output 'Starting Skyrim TrueCoop AE; Creation files have not been moved or disabled.'
Start-Process -FilePath $launcher -WorkingDirectory $game
