# Skyrim True Coop — ветка разработки

[AE-итерация: аудит 74 Creations, расширенная проверка подключения и статусы](docs/truecoop/ae/AUDIT.md).
Полная поддержка AE пока не подтверждена: для новых изменений **«Реализовано,
игровая проверка не выполнена»**. Клиент и сервер требуют совместной пересборки.

[Итерация 5: исправление потока ошибок ячеек до подключения](ITERATION_5.md) · [проверки](VALIDATION_5.md).

AE у одного и SE у другого: [общий режим контента, установка и ограничения](AE_COMMON_MODE.md).

Ошибка «A backup is still active»: [CommonSE fix2, инструкция для друга](COMMONSE_FIX2_RU.md).

Цель: единый кооперативный мир и сюжет поверх Skyrim Together Reborn.
Базовая сборка проверена. Собственные изменения добавляют проверку активного контента,
защиту сетевых ID, повторных операций инвентаря и поколений движения/анимаций, ограничение предсказания
положения и начальный адаптер Alternate Start. Полное ТЗ и поддержка разных активных DLC ещё не завершены.

- [Итерация 3: движение, анимации и границы интеграции Alternate Start](ITERATION_3.md)
- [Итерация 4: порядок передачи владельца и переходы](ITERATION_4.md) · [результаты проверок](VALIDATION_4.md)
- [Проверки итерации 3 и установка через Vortex](VALIDATION_3.md)
- [Итерация 2: сетевые изменения и ограничения DLC](ITERATION_2.md)
- [Базовая сборка и проверки от 2026-11-10](BUILD_STATUS.md) · [Первоначальное исследование](BASELINE.md)
- [Сборка Windows](BUILD.md) · [Архитектура](ARCHITECTURE.md) · [Этапы](ROADMAP.md)
- [Статусы функций](FEATURES.md) · [Проверки](TEST_PLAN.md) · [Ограничения](KNOWN_ISSUES.md)
- [Протокол](NETWORK_PROTOCOL.md) · [Общий сюжет](QUEST_SYSTEM.md) · [Контент и Alternate Start](CONTENT_COMPATIBILITY.md)
- [Исходное ТЗ пользователя](docs/truecoop/PROJECT_SPEC.md)

Исходные сведения и авторство upstream сохранены ниже.

# Skyrim Together
#### codename "TiltedEvolution"
[![GitHub release](https://img.shields.io/github/v/release/tiltedphoques/TiltedEvolution?color=23314E)](https://github.com/tiltedphoques/TiltedEvolution/releases)
[![Nexus Mods](https://img.shields.io/badge/Nexus%20Mods-download-FB923C?logo=nexusmods&logoColor=white)](https://www.nexusmods.com/skyrimspecialedition/mods/69993)
[![Language](https://img.shields.io/github/languages/top/tiltedphoques/TiltedEvolution?logo=cplusplus&color=F34B7D&logoColor=white)](https://github.com/tiltedphoques/TiltedEvolution)
[![Build status](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/windows.yml/badge.svg)](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/windows.yml)
[![Build linux](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/linux.yml/badge.svg)](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/linux.yml)
[![Discord](https://img.shields.io/discord/247835175860305931.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.gg/skyrimtogether)

<img src="https://avatars.githubusercontent.com/u/52131158?s=200&v=4" align="right"
     alt="Size Limit logo by Anton Lovchikov" width="110" height="100">

TiltedEvolution is a framework created to enable multiplayer in Bethesda games, currently supporting **Skyrim Special Edition**.

## Getting started
To play Skyrim Together, go to the [nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/69993).

For general information, go to the [TiltedEvolution Wiki](https://wiki.tiltedphoques.com/tilted-online/).

Check out the [build guide](https://wiki.tiltedphoques.com/tilted-online/technical-documentation/build-guide) for setup and development info on the project. When writing code, check the CODE_GUIDELINES.md and make sure to run clang-format!

## Reporting bugs
If you would like to report a bug please report them in the "Issues" tab on this page. Detailed and reproducible bug reports are of great importance for the development of the project.

## Contributing
Have some experience in C++, and want to help advance the project faster? Contribute!
- Check the issues, it's a good place to start when you don't know what to do.
- Fork the repository and create pull requests to this repository.
- Create pull requests to the "dev" branch, not to master.
- Try to keep your code clean, following the code guidelines.

## Main project source tree

* [**client/**](./Code/client): Skyrim SE client: engine hooks, reverse-engineered game types and the sync logic.
* [**immersive_launcher/**](./Code/immersive_launcher): `SkyrimTogether.exe`, which starts the game with the client loaded.
* [**immersive_elf/**](./Code/immersive_elf): `EarlyLoad.dll` helper DLL for the launcher. Detects if Mod Organizer 2 was injected first.
* [**tp_process/**](./Code/tp_process): Worker for the CEF (Chromium Embedded Framework) overlay.
* [**skyrim_ui/**](./Code/skyrim_ui): In-game UI, written in Angular/TypeScript.
* [**server/**](./Code/server): Game server (`STServer.dll`).
* [**server_runner/**](./Code/server_runner): Dedicated server executable that hosts the server DLL.
* [**encoding/**](./Code/encoding): Network message and struct definitions shared by the client and server.
* [**common/**](./Code/common), [**base/**](./Code/base): Shared utilities.
* [**components/**](./Code/components): Shared modules (console/settings, ESP loader, resources, ImGui).
* [**tests/**](./Code/tests): Unit tests.
* [**GameFiles/**](./GameFiles): Plugins, scripts and assets shipped with the mod.
* [**Libraries/**](./Libraries): Networking, hooking, reverse-engineering and UI libraries (git submodules).
* [**Tools/**](./Tools): Reverse-engineering and address scripts.

## License
[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

TiltedEvolution is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
