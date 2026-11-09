# Сетевой протокол

В этой итерации сообщения и бинарные форматы НЕ изменены.
Определения и сериализаторы: Code/encoding/Messages; структуры: Code/encoding/Structs.
Транспорт: Libraries/TiltedConnect (GameNetworkingSockets).

| Существующий обмен | Назначение |
|---|---|
| AuthenticationRequest/Response | Version=BUILD_COMMIT, Token, UserMods, ячейка/время; GameServer отклоняет другую сборку |
| RequestQuestUpdate/NotifyQuestUpdate | GameId Id, uint16 Stage, uint8 Status (StageUpdate/Started/Stopped), uint8 ClientQuestType |
| RequestWeatherChange/NotifyWeatherChange, RequestCurrentWeather | Обновление и запрос кешированной погоды группы |
| RequestOwnershipTransfer/Claim, NotifyOwnershipTransfer | Владение NPC и проверка поколения OwnershipEpoch |
| RequestInventoryChanges/Snapshot и Notify* | Дельты и снимки; эпоха владельца защищает от части устаревших обновлений |
| AssignObjectsRequest/Response, ActivateRequest, LockChangeRequest | Регистрация объектов, активация и замки |

GameId использует сетевые ModId/BaseId, не произвольный runtime FormID.
Authentication Token — секрет; не включать в диагностические отчёты.
Catch2-тесты сериализации находятся в Code/tests/encoding.cpp.

Предлагаемые будущие поля событий мира: WorldId, EventId, ExpectedRevision, ResultRevision,
версия схемы и подтверждение применения. Форматы и опкоды ещё не выбраны и не зарегистрированы.
Стабильный ключ сохранения должен разрешаться через манифест плагинов; нельзя считать ModId
неизменным между перезапусками/конфигурациями. Изменения протокола требуют одинаковой сборки обоих концов.
