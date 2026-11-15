[CmdletBinding()]
param([string]$NativeRevision = '', [string]$Distribution = '', [string]$Destination = '')
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
$nativeChanges = @(git -C $repo diff --name-only $revision -- Code Libraries modules xmake.lua)
if ($LASTEXITCODE -ne 0 -or $nativeChanges.Count) { throw 'Native/UI source changed since the selected native build' }
$untracked = @(git -C $repo ls-files --others --exclude-standard -- Code Libraries modules)
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
if (Test-Path -LiteralPath (Join-Path $runtime 'Play-CommonSE.cmd')) { throw 'AE package must not ship the CommonSE launcher' }
$info = Join-Path $stage 'TrueCoop-AE-Info'
New-Item -ItemType Directory -Path $info -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'AE_INSTALL_RU.md') -Destination $info
Copy-Item -LiteralPath (Join-Path $repo 'AE_INSTALL_RU.md') -Destination (Join-Path (Split-Path $Destination -Parent) 'SkyrimTrueCoop-AE-INSTALL-RU.md')
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination $info
Copy-Item -LiteralPath (Join-Path $repo 'build/BuildInfo.h') -Destination $info
foreach ($name in @('TiltedConnect','TiltedHooks','TiltedReverse','TiltedUI')) { Copy-Item -LiteralPath (Join-Path $repo "Libraries/$name/LICENSE") -Destination (Join-Path $info "$name-LICENSE.txt") }
foreach ($name in @('CONNECTION.md','PERSISTENCE.md','QUESTS.md','OBJECTS.md')) { Copy-Item -LiteralPath (Join-Path $repo "docs/truecoop/ae/$name") -Destination $info }
Copy-Item -LiteralPath (Join-Path $repo 'GameFiles/Server/TrueCoopQuestAdapters.example.json') -Destination $info
[ordered]@{NativeRevision=$revision;Build=$version;Runtime='1.7.104.0';Profile='Identical active AE content';GameTested=$false;CommonSELauncherIncluded=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $info 'BUILD.json') -Encoding utf8
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
