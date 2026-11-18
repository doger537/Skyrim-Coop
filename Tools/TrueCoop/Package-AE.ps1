[CmdletBinding()]
param([string]$NativeRevision = '', [string]$Distribution = '', [string]$Destination = '', [string]$InstallGuide = '', [string]$QuestProbePath = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = (Resolve-Path (Join-Path $repo '../..')).Path
if (!$NativeRevision) { $NativeRevision = (git -C $repo rev-parse HEAD).Trim() }
$revision = git -C $repo rev-parse "$NativeRevision^{commit}"
if ($LASTEXITCODE -ne 0) { throw 'Unknown native source revision' }
$revision = $revision.Trim()
$version = (git -C $repo describe --tags --always $revision).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify native source' }
$buildInfo = Get-Content -LiteralPath (Join-Path $repo 'build/BuildInfo.h') -Raw
if (!$buildInfo.Contains('#define BUILD_COMMIT "' + $version + '"')) { throw 'Native build identity differs from selected source' }
# Packaging scripts/docs may evolve without rebuilding unchanged C++ under a new protocol identity.
$nativeChanges = @(git -C $repo diff --name-only $revision -- Code Libraries modules GameFiles xmake.lua)
if ($LASTEXITCODE -ne 0 -or $nativeChanges.Count) { throw 'Native/UI source changed since the selected native build' }
$untracked = @(git -C $repo ls-files --others --exclude-standard -- Code Libraries modules GameFiles)
if ($untracked.Count) { throw 'Untracked source files prevent native identity verification' }
$configuration = Get-Content -LiteralPath (Join-Path $repo '.xmake/windows/x64/xmake.conf') -Raw
if ($configuration -notmatch 'skyrim_version\s*=\s*"latest"' -or $configuration -notmatch 'mode\s*=\s*"releasedbg"') { throw 'This package requires the verified latest/1.7.104 Windows releasedbg configuration' }
if (!$Distribution) { $Distribution = Join-Path $workspace ('work/ae-distrib-' + $revision.Substring(0,8)) }
if (!$Destination) { $Destination = Join-Path (Split-Path $repo -Parent) ('SkyrimTrueCoop-AE-test-1.7.104-' + $revision.Substring(0,8) + '.zip') }
$Distribution = (Resolve-Path -LiteralPath $Distribution).Path
if (Test-Path -LiteralPath $Destination) { throw 'Archive already exists; refusing to replace it' }
$native = Join-Path $repo 'build/windows/x64/releasedbg'
foreach ($name in @('SkyrimTogether.exe','SkyrimTogetherServer.exe','STServer.dll','EarlyLoad.dll','TPProcess.exe')) {
    $built = Join-Path $native $name
    $installed = Join-Path $Distribution "bin/$name"
    if (!(Test-Path -LiteralPath $installed) -or (Get-FileHash -LiteralPath $built).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw "Stale distribution binary: $name" }
}
if (!(Test-Path -LiteralPath (Join-Path $Distribution 'bin/libcef.dll'))) { throw 'CEF runtime missing' }
# Inspect compiled consumers too. A refreshed header alone does not prove the
# incremental build recompiled the authentication/version code in each binary.
foreach ($name in @('SkyrimTogether.exe','STServer.dll','SkyrimTogetherServer.exe')) {
    $binary = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $Distribution "bin/$name")))
    if (!$binary.Contains($version)) { throw "Compiled native version differs in $name; rebuild both client and server" }
    if ($version -match '^(.*)-[0-9]+-g[0-9a-f]+$') {
        $family = [regex]::Escape($Matches[1]) + '-[0-9]+-g[0-9a-f]+'
        $tags = @([regex]::Matches($binary, $family) | ForEach-Object Value | Sort-Object -Unique)
        if ($tags.Count -ne 1 -or $tags[0] -cne $version) { throw "Mixed native revisions in $name; rebuild from one source revision" }
    }
}
$stage = Join-Path $workspace ('work/package-ae-' + [guid]::NewGuid().ToString('N'))
$runtime = Join-Path $stage 'SkyrimTogetherReborn'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
foreach ($item in Get-ChildItem -LiteralPath (Join-Path $Distribution 'bin')) {
    if ($item.Name -match '\.(pdb|lib|exp|dmp)$|Tests\.exe$|ProtocolProbe\.exe$|^logs$|^config$|^Data$') { continue }
    Copy-Item -LiteralPath $item.FullName -Destination $runtime -Recurse
}
Copy-Item -LiteralPath (Join-Path $repo 'Code/skyrim_ui/dist/UI') -Destination $runtime -Recurse
foreach ($item in Get-ChildItem -LiteralPath (Join-Path $repo 'GameFiles/Skyrim')) { Copy-Item -LiteralPath $item.FullName -Destination $stage -Recurse }
foreach ($name in @('Play-AE.ps1','Play-AE.cmd','Start-AE-Server.cmd','Restore-AE-Creations.cmd')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $runtime }
$recovery = Join-Path $runtime 'TrueCoop-Recovery'
New-Item -ItemType Directory -Path $recovery -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'SharedContent.ps1') -Destination $recovery
if ($QuestProbePath) {
    $probe = (Resolve-Path -LiteralPath $QuestProbePath).Path
    foreach ($name in @('TrueCoopQuestProbe.esp','Scripts/TrueCoopQuestGrantProbe.pex')) {
        $source = Join-Path $probe $name
        if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing diagnostic fixture: $name" }
        $target = Join-Path $stage $name
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
    New-Item -ItemType Directory -Path (Join-Path $runtime 'QuestAudit') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'QuestProbe/Start-QuestAudit.ps1') -Destination (Join-Path $runtime 'QuestAudit')
    foreach ($name in @('Start-QuestAudit.cmd','Restore-QuestAudit.cmd')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot "QuestProbe/$name") -Destination $runtime }
}
if (Test-Path -LiteralPath (Join-Path $runtime 'Play-CommonSE.cmd')) { throw 'AE package must not ship the CommonSE launcher' }
$info = Join-Path $stage 'TrueCoop-AE-Info'
New-Item -ItemType Directory -Path $info -Force | Out-Null
if (!$InstallGuide) { $InstallGuide = Join-Path $repo 'AE_INSTALL_RU.md' }
Copy-Item -LiteralPath $InstallGuide -Destination (Join-Path $info 'AE_INSTALL_RU.md')
$guideOutput = Join-Path (Split-Path $Destination -Parent) ([IO.Path]::GetFileNameWithoutExtension($Destination) + '-INSTALL-RU.md')
Copy-Item -LiteralPath $InstallGuide -Destination $guideOutput
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination $info
Copy-Item -LiteralPath (Join-Path $repo 'build/BuildInfo.h') -Destination $info
foreach ($name in @('TiltedConnect','TiltedHooks','TiltedReverse','TiltedUI')) { Copy-Item -LiteralPath (Join-Path $repo "Libraries/$name/LICENSE") -Destination (Join-Path $info "$name-LICENSE.txt") }
foreach ($name in @('CONNECTION.md','PERSISTENCE.md','QUESTS.md','OBJECTS.md')) { Copy-Item -LiteralPath (Join-Path $repo "docs/truecoop/ae/$name") -Destination $info }
Copy-Item -LiteralPath (Join-Path $repo 'GameFiles/Server/TrueCoopQuestAdapters.example.json') -Destination $info
foreach ($name in @('COOP_RESYNC.md','DEEP_COOP_TEST_RU.md','docs/truecoop/HOST_AUTHORITY_DESIGN.md','docs/truecoop/PHYSICS_NPC_AUDIT.md','docs/truecoop/PHYSICS_NPC_20261118.md','docs/truecoop/WORLD_ITEM_RECEIPTS.md','docs/truecoop/DIALOGUE_LEASE.md','docs/truecoop/PLAYER_MARKERS.md','docs/truecoop/CHARACTER_IDENTITY.md','docs/truecoop/PAIRED_CHECKPOINTS.md','docs/truecoop/QUEST_GRANTS.md','docs/truecoop/NATIVE_QUEST_GATE.md','docs/truecoop/QUEST_GRANT_DELIVERY.md','docs/truecoop/IMPLEMENTATION_20261118.md','docs/truecoop/ACCELERATED_QUESTS_RU.md')) {
    $source = Join-Path $repo $name
    if (Test-Path -LiteralPath $source) {
        $target = Join-Path $info $name
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
}
$protocolHeader = Get-Content -LiteralPath (Join-Path $repo 'Code/encoding/Structs/ProtocolVersion.h') -Raw
if ($protocolHeader -notmatch 'kTrueCoopProtocolVersion\s*=\s*(0x[0-9A-Fa-f]+)') { throw 'Cannot read the protocol identity' }
$protocol = $Matches[1]
[ordered]@{NativeRevision=$revision;Build=$version;Protocol=$protocol;CorePluginSHA256=(Get-FileHash -LiteralPath (Join-Path $repo 'GameFiles/Skyrim/SkyrimTogether.esp')).Hash;GrantCursorPluginSHA256=(Get-FileHash -LiteralPath (Join-Path $repo 'GameFiles/Skyrim/TrueCoopWorldState.esp')).Hash;AutomaticQuestGrantPolicyEnabled=$false;Runtime='1.7.104.0';Profile='Identical active AE content';GameTested=$false;CommonSELauncherIncluded=$false;DiagnosticQuestIncluded=[bool]$QuestProbePath} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $info 'BUILD.json') -Encoding utf8
$records = @(foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
    [pscustomobject]@{Path=[IO.Path]::GetRelativePath($stage,$file.FullName).Replace('\','/');Bytes=$file.Length;SHA256=(Get-FileHash -LiteralPath $file.FullName).Hash}
})
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $info 'MANIFEST.json') -Encoding utf8
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$Destination,[IO.Compression.CompressionLevel]::Optimal,$false)
$zip = [IO.Compression.ZipFile]::OpenRead($Destination)
try {
    foreach ($record in $records) {
        $entry = $zip.GetEntry($record.Path)
        if (!$entry -or $entry.Length -ne $record.Bytes) { throw "Bad archive entry: $($record.Path)" }
        $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try { $digest = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') } finally { $stream.Dispose(); $sha.Dispose() }
        if ($digest -ne $record.SHA256) { throw "Archive hash mismatch: $($record.Path)" }
    }
} finally { $zip.Dispose() }
$digest = (Get-FileHash -LiteralPath $Destination).Hash
"$digest  $([IO.Path]::GetFileName($Destination))" | Set-Content -LiteralPath ($Destination + '.sha256') -Encoding ascii
[ordered]@{Archive=$Destination;Bytes=(Get-Item -LiteralPath $Destination).Length;FilesVerified=$records.Count;Build=$version;SHA256=$digest} | ConvertTo-Json
