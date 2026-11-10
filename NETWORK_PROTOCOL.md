# Сетевой протокол

В первой итерации сообщения не менялись. В итерации 2 протокол изменён и требует обновления
всех клиентов и сервера вместе. BUILD_COMMIT новой сборки отделяет её от предыдущего ZIP.

Изменения итерации 2:

- Mods::Entry: после имени плагина идут 32 байта SHA-256. Serialize пишет ровно объявленное число
  записей (не более 4096). Активный манифест ограничен расчётным бюджетом 48 KiB из пакета 64 KiB.
- AuthenticationRequest: после Version идёт RuntimeVersion. WorldSpaceId/CellId в ЭТОМ запросе
  используют ModId=index+1 в UserMods, ноль — отсутствие объекта. Сервер преобразует их до PlayerJoinEvent.
  Во всех игровых сообщениях остаются обычные серверные ModId/BaseId.
- AuthenticationResponse: после Version идёт ContentError; добавлен kContentMismatch.
- RequestInventoryChanges: после ServerId идёт uint64 OperationId (varint), уникальный в соединении.
  Сервер проверяет владение до replay-фильтра; устаревший запрос не расходует номер операции.
- До успешного AuthenticationResponse игровые сервисы клиента не публикуют состояние.

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
