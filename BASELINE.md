# Первый отчёт — 2026-11-09

## 1. Что сделано

Получен настоящий TiltedEvolution, полная история и закреплённые подмодули.
Создана локальная ветка truecoop/bootstrap от 8b2b1b0cd9fc53ab7518a2c980486d07e5edea97.
Изучены AGENTS/README/GETTINGSTARTED/CODE_GUIDELINES, лицензии корня/подмодулей,
xmake, Windows CI, сервисы квестов, погоды, календаря, инвентаря, объектов, владения NPC,
интерполяция и основные тесты. Подготовлены архитектура, план и сценарий сборки.

Установка Skyrim прочитана без изменений: D:/SteamLibrary/steamapps/common/Skyrim Special Edition,
SkyrimSE.exe FileVersion/ProductVersion **1.7.104.0**. Поддержка этой строки явно есть в main.cpp
при default skyrim_version=latest. Игровая совместимость хуков ещё не проверена.

## 2. Файлы

README расширен; добавлены ARCHITECTURE, ROADMAP, FEATURES, BUILD, TEST_PLAN,
NETWORK_PROTOCOL, QUEST_SYSTEM, CONTENT_COMPATIBILITY, KNOWN_ISSUES и этот отчёт.
Tools/TrueCoop/Build.ps1 — проверка среды и последовательный запуск сборки/тестов/UI.
Tools/TrueCoop/Test-BuildGate.ps1 — проверка отказа при недостающем инструменте.
docs/truecoop/PROJECT_SPEC.md — исходное ТЗ; docs/truecoop/baseline — реальные логи.
Code/client, Code/server, Code/encoding и игровые ресурсы не изменялись.

## 3. Что реально работает

Git-ветка с исходниками и подмодулями; установленный локально portable xmake;
production-сборка существующего интерфейса; диагностика отсутствующих инструментов.
Новых игровых функций и готовой установленной True Coop сборки нет.

## 4. Что проверено

| Проверка | Результат |
|---|---|
| git submodule status --recursive | Все четыре подмодуля соответствуют закреплённым коммитам |
| Portable xmake 3.1.0 | Запускается; zip SHA256 совпал с GitHub release digest |
| xmake config windows/x64/releasedbg/latest --yes -vD | **Ошибка**, exit -1: install lua v5.4.8 failed, target(lualib): toolchain not found; Microsoft Visual Studio (x64) ... no |
| Build.ps1 -CheckOnly с portable xmake | Ожидаемый отказ: нет Visual Studio C++ и Windows SDK; записан preflight.json |
| pnpm 9.15.9 install --frozen-lockfile | Успех, lockfile не изменён |
| pnpm deploy:production, Node 20.18.1 | **Успех**, Angular build hash f9dbb45df4c927c5; dist/UI/index.html и bundles созданы |
| Test-BuildGate.ps1 | PASS: отсутствующий xmake блокирует сборку и отражён в отчёте; найденная ошибка проверки текста самого теста исправлена перед повторным запуском |
| git diff --check | Успех |
| TPTests / компонентные C++-тесты | НЕ ЗАПУЩЕНЫ: нет C++ toolchain |
| Сервер, launcher, два клиента, A–J/AS | НЕ ЗАПУЩЕНЫ |

Portable xmake находится вне репозитория в ../../work/toolchain/xmake/xmake/xmake.exe.
Архив: https://github.com/xmake-io/xmake/releases/download/v3.1.0/xmake-v3.1.0.win64.zip
SHA256: 92c320923a0ba52d21934d107ae7fa4cbf03ea5ce972177e7abc2fb7905bd8bd.
xmake загрузил часть зависимостей, включая CMake/Ninja. Полная native-конфигурация не завершилась;
воспроизводимая игровая сборка пока не подтверждена. UI dist не устанавливался в игру.

## 5. Оставшиеся проблемы

Главный блокер этапа 1 — отсутствующие MSVC/Windows SDK. Сообщение о .NET SDK встречается
в диагностике поиска инструментов, но это не доказанная отдельная обязательная зависимость проекта.
Address Library/ASLAL/USSEP не найдены в проверенном Data; виртуальные профили не проверялись.
Нужен запуск с двумя клиентами. Нет числового baseline рассинхрона.
Права распространения Tilted-подмодулей и конкретные версии ASLAL/USSEP требуют уточнения.

## 6. Следующий шаг

Установить native-инструменты, повторить Build.ps1, устранить реальные ошибки, запустить TPTests,
проверить сервер и двух клиентов. Пока не менять игровую логику. Затем отдельный тестовый профиль
Alternate Start, далее диагностика владения NPC на этапе 2 согласно ROADMAP.

## 7. Что требуется от владельца компьютера

Установить **Build Tools for Visual Studio 2022** с компонентами **Desktop development with C++**,
**MSVC v143 x64/x86** и **Windows 10/11 SDK**, если установщик запросит локальное подтверждение.
Официальная загрузка: https://visualstudio.microsoft.com/downloads/ (Tools for Visual Studio).
После установки из Developer PowerShell можно выполнить:

```powershell
./Tools/TrueCoop/Build.ps1 -XmakePath '../../work/toolchain/xmake/xmake/xmake.exe'
```

Для игровой приёмки понадобятся тестовые персонажи и второй клиент; точные сценарии — TEST_PLAN.md.
Этап 1 не завершён, этап 2 ещё не начат.
