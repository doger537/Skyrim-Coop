[CmdletBinding()]
param([string]$Label = 'iteration5')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[a-zA-Z0-9-]+$') { throw 'Use letters, numbers and dashes in Label' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = (Resolve-Path (Join-Path $repo '../..')).Path
$outputs = (Resolve-Path (Join-Path $repo '..')).Path
$buildInfo = Get-Content -LiteralPath (Join-Path $repo 'build/BuildInfo.h') -Raw
$match = [regex]::Match($buildInfo, '#define BUILD_COMMIT "([^"]+)"')
if (!$match.Success) { throw 'Missing build identity' }
$version = $match.Groups[1].Value
if ($version -notmatch '^[a-zA-Z0-9.-]+$') { throw 'Unsafe build identity' }
$sourceVersion = git -C $repo describe --tags --always
if ($LASTEXITCODE -ne 0 -or $version -ne $sourceVersion.Trim()) { throw 'Build identity does not match source revision' }
foreach ($name in @('SkyrimTogether.exe','SkyrimTogetherServer.exe','STServer.dll','EarlyLoad.dll','TPProcess.exe')) {
    $built = Join-Path $repo "build/windows/x64/releasedbg/$name"
    $staged = Join-Path $repo "distrib/bin/$name"
    if (!(Test-Path $built) -or !(Test-Path $staged) -or
        (Get-FileHash $built).Hash -ne (Get-FileHash $staged).Hash) { throw "Stale distribution file: $name" }
}
$archive = Join-Path $outputs "SkyrimTrueCoop-$Label-SSE-1.7.104-$version.zip"
if (Test-Path -LiteralPath $archive) { throw 'Output already exists; use another label' }
$stage = Join-Path $workspace ('work/package-' + [Guid]::NewGuid().ToString('N'))
$runtime = Join-Path $stage 'SkyrimTogetherReborn'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
foreach ($name in @('SkyrimTogether.exe','SkyrimTogetherServer.exe','STServer.dll','EarlyLoad.dll','TPProcess.exe','libcef.dll')) {
    if (!(Test-Path (Join-Path $repo "distrib/bin/$name"))) { throw "Missing runtime: $name" }
}
foreach ($item in Get-ChildItem (Join-Path $repo 'distrib/bin')) {
    if ($item.Name -match '\.(pdb|lib|exp|dmp)$|Tests\.exe$|ProtocolProbe\.exe$|^logs$') { continue }
    Copy-Item -LiteralPath $item.FullName -Destination $runtime -Recurse
}
Copy-Item -LiteralPath (Join-Path $repo 'Code/skyrim_ui/dist/UI') -Destination $runtime -Recurse
foreach ($item in Get-ChildItem (Join-Path $repo 'GameFiles/Skyrim')) {
    Copy-Item -LiteralPath $item.FullName -Destination $stage -Recurse
}
$info = Join-Path $stage 'TrueCoop-Test-Info'
foreach ($name in @('SharedContent.ps1','Play-CommonSE.cmd','Restore-Creations.cmd')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $runtime
}
Copy-Item -LiteralPath (Join-Path $repo 'AE_COMMON_MODE.md') -Destination $runtime
New-Item -ItemType Directory -Path $info | Out-Null
foreach ($name in @('LICENSE','ITERATION_2.md','ITERATION_3.md','ITERATION_4.md','ITERATION_5.md','TEST_PLAN.md','CONTENT_COMPATIBILITY.md')) {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $info
}
Copy-Item -LiteralPath (Join-Path $repo 'build/BuildInfo.h') -Destination $info
foreach ($name in @('TiltedConnect','TiltedHooks','TiltedReverse','TiltedUI')) {
    Copy-Item -LiteralPath (Join-Path $repo "Libraries/$name/LICENSE") -Destination (Join-Path $info "$name-LICENSE.txt")
}
$instructions = @"
# True Coop — $Label

Сборка: $version. Skyrim SE Steam: 1.7.104.0. Игровые проверки новой версии ещё нужны.
Оба клиента и сервер обновляются вместе. Предыдущие сборки с этой версией несовместимы.

## Установка через Vortex

1. Завершите игру и сервер. Сохраните прежний ZIP для отката.
2. В отдельном тестовом профиле отключите прежнюю сборку True Coop / Skyrim Together.
3. Установите этот ZIP через Install From File, включите и выполните Manual apply (Deploy).
4. Включите SkyrimTogether.esp и SkyrimTogetherQuestPatches.esp. Address Library для 1.7.104 остаётся нужна.
5. Ваш существующий инструмент Vortex по пути Data/SkyrimTogetherReborn/SkyrimTogether.exe подходит.
6. Обновите сервер этим же пакетом. Сервер из прежнего ZIP использовать нельзя.

## Первая проверка

1. Одинаковые активные плагины и их порядок, новый тестовый сервер, тестовые сохранения.
2. Подключитесь оба, создайте группу; проверьте видимость, переход между локациями и экипировку.
3. Проверьте передачу обычного предмета и лут NPC; запишите фактические количества.
4. После отключения одного игрока проверьте переподключение.
5. На ОТДЕЛЬНОМ тестовом профиле с НОВЫМ персонажем включите лишний DLC-плагин только у одного:
   сервер должен отказать с причиной Extra/Missing plugin. Для смены базового набора перезапустите сервер.

## Что означает совместимость DLC сейчас

Разные покупки/файлы на диске допустимы, если активный игровой набор одинаков.
SHA-256 сравнивает содержимое активных ESP/ESM/ESL. Скрипты, DLL, BSA и текстуры в эту проверку не входят.
Разные активные DLC-квесты, безопасные замены предметов и Limited Compatibility ещё не реализованы.
Это первый рабочий слой проверки и предотвращения ошибок, а не завершённое ТЗ.
Не отключайте DLC посреди основного прохождения ради этого теста.

В новой версии исправлены неизвестные сетевые ID плагинов, дубли одинаковых операций инвентаря,
обработка некорректного времени. Добавлены ограничение предсказания и поколения движения/анимаций.
Alternate Start: проверка USSEP и изоляция личных стартовых квестов; полный совместный старт не готов.
Для него сначала завершите выбор начала офлайн. Файлы ASLAL/USSEP устанавливаются отдельно.
Это не гарантия отсутствия всех дюпов, рывков и ошибок Havok/скелетов.
Исправление офлайн-обнаружения ячеек описано в ITERATION_5.md; полное прохождение, дома,
постоянный мир, адаптация квестов и гражданская война ещё впереди.
"@
$instructions | Set-Content -LiteralPath (Join-Path $info 'INSTALL-RU.md') -Encoding UTF8
$instructions | Set-Content -LiteralPath (Join-Path $outputs "SkyrimTrueCoop-$Label-INSTALL-RU.md") -Encoding UTF8
$manifest = @(foreach ($file in Get-ChildItem $stage -Recurse -File) {
    [ordered]@{ Path=[IO.Path]::GetRelativePath($stage,$file.FullName).Replace('\','/'); Bytes=$file.Length; SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
})
$manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $info 'MANIFEST.json') -Encoding UTF8
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$archive,[IO.Compression.CompressionLevel]::Optimal,$false)
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($item in $manifest) {
        $entry=$zip.GetEntry($item.Path)
        if (!$entry -or $entry.Length -ne $item.Bytes) { throw "Invalid archive entry: $($item.Path)" }
        $stream=$entry.Open(); $sha=[Security.Cryptography.SHA256]::Create()
        try { $digest=[Convert]::ToHexString($sha.ComputeHash($stream)) } finally { $stream.Dispose(); $sha.Dispose() }
        if ($digest -ne $item.SHA256) { throw "Corrupt archive entry: $($item.Path)" }
    }
} finally { $zip.Dispose() }
$checksum=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
"$checksum  $([IO.Path]::GetFileName($archive))" | Set-Content "$archive.sha256" -Encoding ASCII
[ordered]@{Archive=$archive;Bytes=(Get-Item $archive).Length;FilesVerified=$manifest.Count;Build=$version;SHA256=$checksum} | ConvertTo-Json
