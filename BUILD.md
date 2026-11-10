# Сборка Windows

База: upstream 8b2b1b0cd9fc53ab7518a2c980486d07e5edea97, ветка truecoop/bootstrap.
Полная история и закреплённые подмодули загружены. Публичный форк не создавался.
Источник истины: xmake.lua, Libraries/*/xmake.lua и .github/workflows/windows.yml.
См. также [upstream guide](https://wiki.tiltedphoques.com/tilted-online/technical-documentation/build-guide).

## Требования

- Visual Studio 2022 / Build Tools: Desktop development with C++, MSVC v143 x64/x86 и Windows 10/11 SDK.
- xmake 3.1.0 (версия CI); проект требует минимум 3.0.0. CMake/Ninja и C++-библиотеки получает xmake.
- Git с подмодулями. Node 20.x (CI) и pnpm 9; здесь используется pnpm 9.15.9 через npm exec.
- Для запуска: Skyrim SE нужной версии и соответствующая Address Library. Версия exe не определяет покупку Anniversary Upgrade.

Из Developer PowerShell for VS 2022 в корне репозитория:

```powershell
git submodule update --init --recursive
./Tools/TrueCoop/Build.ps1 -CheckOnly
./Tools/TrueCoop/Build.ps1
# Если xmake не в PATH:
./Tools/TrueCoop/Build.ps1 -XmakePath 'C:/path/to/xmake.exe'
```

Сценарий останавливается при первой ошибке, пишет preflight.json и логи в build/truecoop.
Он не устанавливает инструменты и не копирует файлы в Skyrim.
Установка VS 2022 определяется через vswhere с UTF-8. Для пути с кириллицей сценарий
передаёт xmake VSInstallDir/VCInstallDir с завершающим разделителем и VisualStudioVersion;
после выполнения прежние значения переменных процесса восстанавливаются. Глобальные
переменные Windows и файлы установленного Visual Studio не изменяются.
`-SkipUI` пропускает только UI. `-SkyrimVersion 1.6.1170` выбирает отдельную старую сборку.
Default latest соответствует обнаруженному Skyrim 1.7.104.0.

Эквивалент upstream:

```powershell
xmake config --plat=windows --arch=x64 --mode=releasedbg --skyrim_version=latest --yes
xmake build --yes
xmake run TPTests
xmake install -o distrib
npm.cmd exec --yes --package=pnpm@9.15.9 -- pnpm --prefix Code/skyrim_ui install --frozen-lockfile
npm.cmd exec --yes --package=pnpm@9.15.9 -- pnpm --prefix Code/skyrim_ui deploy:production
```

Основные цели: SkyrimTogetherClient (static), SkyrimImmersiveLauncher (SkyrimTogether.exe),
SkyrimTogetherServer (STServer.dll), SkyrimServerRunner (SkyrimTogetherServer.exe), TPTests (Catch2).
Дополнительные тесты: Console_Tests, ESLoader_Tests, Resources_Tests.
Корневой npm test — заглушка upstream; это не проверка мода.

Ручная упаковка и установка отдельно, после успешной сборки и проверки лицензий.
Обычная компиляция не доказывает корректность Address Library и хуков в игре.
