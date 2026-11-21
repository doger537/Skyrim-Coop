# Контрольная точка: парные сохранения

- Native source: `ebeafe36f33a735344699961f2ac5fd23573066f`.
- Build: `v1.8.4-59-gebeafe36`, protocol `0x54430009`, SQLite schema 2.
- Core ESP: 8470 bytes, SHA-256 `c16976f0eff6490c17bc53d2ef5c7651250b4c9de9dc525f9261d913aa90806e`.
- New module: [PAIRED_CHECKPOINTS.md](docs/truecoop/PAIRED_CHECKPOINTS.md). Оно содержит engine evidence, поток операций и известные ограничения.

Собраны launcher/client, server runner, STServer.dll, EarlyLoad.dll, TPProcess.exe, protocol probe и TPTests. Production UI пересобран локальным Angular CLI из закреплённых node_modules, hash `12a68f6843091fc2`; несовместимый новый pnpm отказался переустанавливать зависимости, lockfile не менялся.

104 Catch2 cases / 3412 assertions; шесть детерминированных ESP-проверок. Финальный headless server/probe одного native commit: 17 paired checkpoints, четыре campaign admission, 10 sleep, 13 dialogue, 11 locations, девять environment с реальным минутным refresh, 22 general — 86 PASS. Ещё 24 PASS в повторяемом сценарии rollback → прерывание → restart → rejoin → restart без повторного rendezvous. Тестовые серверы остановлены.

Evidence вне репозитория: workspace/work/paired-ebeafe36-*.log, paired-ebeafe36-regress/*/probe.log и paired-checkpoint-test-bd184f2fb62f455091387af7aa97b4df. Профили, ESS и production DB не включены в архив и не заменялись.

Сервер атомарно пишет инициатора и двух участников НОВОГО квестового запуска; повторы и следующий отправитель не меняют запись, rollback возвращает прежний запуск. Убран конкретный дубль quest item при репликации удалённого инвентаря. **Полная custody/двойная выдача наград, VM/alias bridge и клиентские долговечные квитанции ещё не завершены.**

Сборка остаётся тестовой (`GameTested=false`). Native paired save/load, UI pause, восстановление двух настоящих персонажей, NPC/Havok и кадры требуют двух игр. Обычный ручной reconnect после произвольного локального ESS не имеет той же проверки manifest, что `/coop-load`. Host-only AI/physics, full ragdoll/furniture/navmesh и полноценная синхронизация дропа остаются отдельной незавершённой работой. Alternate Start не менялся.
