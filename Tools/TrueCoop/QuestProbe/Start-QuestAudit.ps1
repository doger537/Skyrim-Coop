[CmdletBinding()]
param([string]$GamePath = '', [switch]$Restore, [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'

function Set-AuditIniValue([string]$Text, [string]$Section, [string]$Key, [string]$Value) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $inside = $false; $inserted = $false
    foreach ($line in [regex]::Split($Text, '\r?\n')) {
        if ($line -match '^\s*\[([^\]]+)\]\s*$') {
            if ($inside -and !$inserted) { $lines.Add("$Key=$Value"); $inserted = $true }
            $inside = $Matches[1] -ieq $Section
        }
        if ($inside -and $line -match ('^\s*' + [regex]::Escape($Key) + '\s*=')) { continue }
        $lines.Add($line)
    }
    if (!$inserted) {
        if (!$inside) { $lines.Add("[$Section]") }
        $lines.Add("$Key=$Value")
    }
    return [string]::Join("`r`n", $lines)
}

function Write-AuditFile([string]$Path, [byte[]]$Bytes) {
    # Replace the directory entry, never overwrite a Vortex hardlink's inode.
    $temporary = $Path + '.truecoop-' + [guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllBytes($temporary, $Bytes)
    try {
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
        else { [IO.File]::Move($temporary, $Path) }
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}

$runtime = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($GamePath)) { $GamePath = Join-Path $runtime '../..' }
$game = (Resolve-Path -LiteralPath $GamePath).Path.TrimEnd('\')
$documents = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
$profile = Join-Path $documents 'My Games/Skyrim Special Edition'
$customIni = Join-Path $profile 'SkyrimCustom.ini'
$auditRoot = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)) 'SkyrimTrueCoop/QuestAudit'
$marker = Join-Path $auditRoot 'active.json'

function Restore-AuditProfile {
    if (!(Test-Path -LiteralPath $marker)) { return }
    if (Get-Process SkyrimTogether,SkyrimSE,SkyrimSELauncher -ErrorAction SilentlyContinue) { throw 'Close Skyrim before restoring audit settings.' }
    $state = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
    $run = [IO.Path]::GetFullPath([string]$state.Run)
    $allowed = [IO.Path]::GetFullPath($auditRoot).TrimEnd('\') + '\'
    if ($state.Version -ne 1 -or $state.Game -ne $game -or !$run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($run) -notmatch '^run-[a-f0-9]{32}$') { throw 'Audit recovery journal belongs to another installation or has an invalid path. Preserve it.' }
    if (Test-Path -LiteralPath $customIni) {
        $currentHash = (Get-FileHash -LiteralPath $customIni -Algorithm SHA256).Hash
        if ($currentHash -ne $state.AuditSHA256 -and (!$state.OriginalExists -or $currentHash -ne $state.OriginalSHA256)) {
            throw "SkyrimCustom.ini changed after the audit started. Preserve the journal and backup: $run"
        }
    } elseif ($state.OriginalExists) { throw "SkyrimCustom.ini is missing. Preserve the original backup: $run" }
    $backup = Join-Path $run 'SkyrimCustom.ini.original'
    if ($state.OriginalExists) {
        if (!(Test-Path -LiteralPath $backup) -or (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $state.OriginalSHA256) {
            throw 'Original audit settings backup is missing or damaged. Preserve the journal.'
        }
        Write-AuditFile $customIni ([IO.File]::ReadAllBytes($backup))
    } elseif (Test-Path -LiteralPath $customIni) { Remove-Item -LiteralPath $customIni }
    Move-Item -LiteralPath $marker -Destination (Join-Path $run 'restored.json') -Force
    Write-Output "Original SkyrimCustom.ini restored. Diagnostic evidence: $run"
}

if ($Restore) { Restore-AuditProfile; return }
if (Test-Path -LiteralPath $marker) { throw 'An audit backup is still active. Close Skyrim and run Restore-QuestAudit.cmd first.' }
if (Get-Process SkyrimTogether,SkyrimSE,SkyrimSELauncher -ErrorAction SilentlyContinue) { throw 'Close Skyrim before starting the diagnostic.' }
$gameExe = Join-Path $game 'SkyrimSE.exe'
if (!(Test-Path -LiteralPath $gameExe) -or [Diagnostics.FileVersionInfo]::GetVersionInfo($gameExe).FileVersion -notmatch '^1\.7\.104(\.0)?$') {
    throw 'This diagnostic requires SkyrimSE.exe 1.7.104.0.'
}
if (!(Test-Path -LiteralPath $profile -PathType Container)) { throw 'The Skyrim user settings folder does not exist.' }
$plugins = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)) 'Skyrim Special Edition/plugins.txt'
if (!(Test-Path -LiteralPath $plugins) -or !((Get-Content -LiteralPath $plugins) -match '^\*TrueCoopQuestProbe\.esp\s*$')) {
    throw 'Enable TrueCoopQuestProbe.esp in the diagnostic Vortex profile and deploy it first.'
}
foreach ($name in @('SkyrimTogether.exe','SkyrimTogetherServer.exe','STServer.dll','EarlyLoad.dll','TPProcess.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $runtime $name))) { throw "Missing runtime file: $name" }
}
if (!(Test-Path -LiteralPath (Join-Path $game 'Data/Scripts/TrueCoopQuestGrantProbe.pex'))) { throw 'The diagnostic Papyrus script is missing. Deploy the full diagnostic archive.' }
$info = Join-Path $game 'Data/TrueCoop-AE-Info'
$build = Get-Content -LiteralPath (Join-Path $info 'BUILD.json') -Raw | ConvertFrom-Json
if (!$build.DiagnosticQuestIncluded -or $build.Runtime -ne '1.7.104.0') { throw 'Deploy the full diagnostic archive; the installed build information is from another package.' }
$manifest = @(Get-Content -LiteralPath (Join-Path $info 'MANIFEST.json') -Raw | ConvertFrom-Json)
foreach ($relative in @('SkyrimTogetherReborn/SkyrimTogether.exe','SkyrimTogetherReborn/SkyrimTogetherServer.exe','SkyrimTogetherReborn/STServer.dll',
    'SkyrimTogetherReborn/EarlyLoad.dll','SkyrimTogetherReborn/TPProcess.exe','SkyrimTogether.esp','TrueCoopQuestProbe.esp','Scripts/TrueCoopQuestGrantProbe.pex')) {
    $record = @($manifest | Where-Object Path -IEQ $relative)
    if ($record.Count -ne 1 -or (Get-FileHash -LiteralPath (Join-Path (Join-Path $game 'Data') $relative) -Algorithm SHA256).Hash -ne $record[0].SHA256) {
        throw "Mixed or damaged diagnostic deployment: $relative. Disable old co-op packages in the test profile and deploy again."
    }
}
if ((Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash -ne '2C9CB9D61F3721FFB43A234764FE112864A89F4D63579C31FD028A3720047679') {
    throw 'This SkyrimSE.exe differs from the binary verified through Ghidra. Preserve it and report the message before this native diagnostic.'
}
if (Get-NetUDPEndpoint -LocalPort 38649 -ErrorAction SilentlyContinue) { throw 'Diagnostic UDP port 38649 is already in use. This script has changed nothing.' }
if ($CheckOnly) { Write-Output 'Diagnostic preflight passed. No settings changed and no process started.'; return }

$run = Join-Path $auditRoot ('run-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $run 'config') -Force | Out-Null
$originalExists = Test-Path -LiteralPath $customIni
$originalHash = ''; $text = ''
if ($originalExists) {
    Copy-Item -LiteralPath $customIni -Destination (Join-Path $run 'SkyrimCustom.ini.original')
    $originalHash = (Get-FileHash -LiteralPath $customIni -Algorithm SHA256).Hash
    $text = [IO.File]::ReadAllText($customIni)
}
$saveFolder = 'Saves\TrueCoopQuestAudit_' + [IO.Path]::GetFileName($run).Substring(4) + '\'
$text = Set-AuditIniValue $text 'General' 'sLocalSavePath' $saveFolder
foreach ($key in @('bEnableLogging','bEnableTrace','bLoadDebugInformation')) { $text = Set-AuditIniValue $text 'Papyrus' $key '1' }
$auditBytes = [Text.Encoding]::UTF8.GetBytes($text)
$sha = [Security.Cryptography.SHA256]::Create()
try { $auditHash = [BitConverter]::ToString($sha.ComputeHash($auditBytes)).Replace('-','') } finally { $sha.Dispose() }
[ordered]@{Version=1;Game=$game;Run=$run;OriginalExists=[bool]$originalExists;OriginalSHA256=$originalHash;AuditSHA256=$auditHash;SaveFolder=$saveFolder} |
    ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8

$server = $null
$previousAuditEnvironment = $env:TRUECOOP_QUEST_AUDIT
try {
    Write-AuditFile $customIni $auditBytes
    foreach ($name in @('SkyrimTogetherServer.exe','STServer.dll')) { Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination $run }
    @'
[general]
bConsole=false
[LiveServices]
bAnnounceServer=false
[GameServer]
uPort=38649
uMaxPlayerCount=1
sPassword=questaudit
sServerName=Private single-client quest audit
[Gameplay]
bAutoPartyJoin=true
bAllowConsoleCommands=true
[Persistence]
sWorldPath=Data/TrueCoop/world.sqlite3
'@ | Set-Content -LiteralPath (Join-Path $run 'config/STServer.ini') -Encoding ascii
    $serverPath = Join-Path $run 'SkyrimTogetherServer.exe'
    $server = Start-Process -FilePath $serverPath -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'server.stdout.log') -RedirectStandardError (Join-Path $run 'server.stderr.log')
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($server.HasExited) { throw "Diagnostic server exited. Evidence: $run" }
        if (Get-NetUDPEndpoint -OwningProcess $server.Id -LocalPort 38649 -ErrorAction SilentlyContinue) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (!(Get-NetUDPEndpoint -OwningProcess $server.Id -LocalPort 38649 -ErrorAction SilentlyContinue)) { throw 'The diagnostic server did not start.' }
    $env:TRUECOOP_QUEST_AUDIT = '1'
    Write-Output 'Diagnostic server: 127.0.0.1:38649 | password: questaudit'
    Write-Output 'At the MAIN MENU open the game console and enter: coc qasmoke'
    Write-Output 'Do not load a normal character. Then connect to the diagnostic server using Right Ctrl.'
    Write-Output 'After connecting run these console commands, closing the console for 5 seconds between each:'
    Write-Output '  startquest TrueCoopRewardProbe'
    Write-Output '  cqf TrueCoopRewardProbe GrantForAudit'
    Write-Output 'Expected notification: TrueCoop quest probe: gold delta 17'
    Write-Output 'Exit Skyrim normally when finished. Keep this window open until settings have been restored.'
    $client = Start-Process -FilePath (Join-Path $runtime 'SkyrimTogether.exe') -ArgumentList @('--exePath', ('"' + $gameExe + '"')) -WorkingDirectory $game -PassThru
    $client.WaitForExit()
    Write-Output ("Skyrim process exit code: " + $client.ExitCode)
} finally {
    $env:TRUECOOP_QUEST_AUDIT = $previousAuditEnvironment
    if ($server -and !$server.HasExited) {
        $current = Get-Process -Id $server.Id -ErrorAction SilentlyContinue
        if ($current -and $current.Path -eq (Join-Path $run 'SkyrimTogetherServer.exe')) { Stop-Process -InputObject $current; $current.WaitForExit() }
    }
    foreach ($logRoot in @((Join-Path $runtime 'logs'), (Join-Path $profile 'Logs/Script'))) {
        if (Test-Path -LiteralPath $logRoot) {
            foreach ($file in Get-ChildItem -LiteralPath $logRoot -File | Where-Object Name -Match '^(tp_client.*\.log|Papyrus\.0\.log)$') {
                Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $run $file.Name) -ErrorAction Continue
            }
        }
    }
    try { Restore-AuditProfile } catch { Write-Warning $_.Exception.Message; Write-Warning 'After closing Skyrim run Restore-QuestAudit.cmd. The original backup is preserved.' }
    Write-Output "Diagnostic evidence: $run"
}
