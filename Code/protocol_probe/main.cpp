#include <algorithm>
#include <chrono>
#include <EncodingPch.h>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <Client.hpp>
#include <Packet.hpp>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/RequestActionEvents.h>
#include <Messages/NotifyActionEvents.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Structs/AnniversaryContent.h>
#include <Messages/PartyCreateRequest.h>
#include <Messages/NotifyPartyJoined.h>
#include <Messages/RequestWeatherChange.h>
#include <Messages/RequestCurrentWeather.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <BuildInfo.h>
#include <Messages/NotifyCampaignCheckpoint.h>
#include <Messages/NotifyQuestJournal.h>
#include <Messages/RequestCampaignReady.h>
#include <Messages/RequestCampaignSeed.h>
#include <Messages/RequestCoopSleep.h>
#include <Messages/NotifyCoopSleep.h>
#include <Messages/RequestDialogueLease.h>
#include <Messages/NotifyDialogueLease.h>
#include <Messages/NotifyPartyLocations.h>
#include <Messages/NotifyPartyLeft.h>
#include <Messages/PartyLeaveRequest.h>
#include <chrono>
#include <iostream>
#include <cmath>
#include <optional>
#include <thread>

using namespace TiltedPhoques;
using ResponseType = AuthenticationResponse::ResponseType;

struct Probe final : Client
{
    AuthenticationRequest Request;
    std::optional<AuthenticationResponse> Reply;
    std::optional<AssignCharacterResponse> Assignment;
    std::optional<AssignObjectsResponse> Objects;
    uint32_t InventoryNotifications{};
    uint32_t MovementNotifications{};
    uint32_t ActionNotifications{};
    uint32_t LastActionEpoch{};
    uint32_t LockNotifications{};
    uint32_t QuestNotifications{};
    uint16_t LastQuestStage{};
    bool InParty{};
    bool PartyLeader{};
    uint32_t WeatherNotifications{};
    GameId LastWeather{};
    uint32_t TimeNotifications{};
    std::optional<ServerTimeSettings> Time;
    std::optional<NotifyPlayerCellChanged> PlayerCell;
    bool Ready{};
    bool AutoReady{true};
    uint64_t Nonce{};
    uint64_t CampaignRevision{};
    uint64_t WorldEpoch{};
    bool SeedOwner{};
    Vector<NotifyQuestJournal> Journals;
    Vector<QuestJournal> Seed;
    float LastPositionX{};
    std::optional<NotifyCoopSleep> RestState;
    uint64_t RestSequence{};
    uint32_t RestCommits{};
    std::optional<NotifyDialogueLease> DialogueLease;
    uint32_t DialogueNotifications{};
    std::optional<NotifyPartyLocations> PartyLocations;
    uint32_t LocationNotifications{};
    void Rest(CoopSleepVote::Action aAction, uint8_t aHours = 0, uint64_t aRound = 0)
    {
        RequestCoopSleep request;
        request.Action = aAction;
        request.Sequence = ++RestSequence;
        request.Round = aRound;
        request.Hours = aHours;
        request.Tick = GetClock().GetCurrentTick();
        SendMessage(request);
    }
    void OnUpdate() override {}
    void OnDisconnected(EDisconnectReason) override { Ready = false; }
    void OnConnected() override { WorldEpoch = 0; SendMessage(Request); }
    void SendMessage(const ClientMessage& acMessage)
    {
        Buffer buffer(65536);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8);
        acMessage.Serialize(writer, WorldEpoch);
        PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
        Send(&packet);
    }
    void OnConsume(const void* apData, uint32_t aSize) override
    {
        ViewBuffer buffer(reinterpret_cast<uint8_t*>(const_cast<void*>(apData)), aSize);
        Buffer::Reader reader(&buffer);
        ServerMessageFactory factory;
        const auto message = factory.Extract(reader, WorldEpoch);
        if (message && message->GetOpcode() == AuthenticationResponse::Opcode)
        {
            Reply = *static_cast<AuthenticationResponse*>(message.get());
            if (Reply->Type == ResponseType::kAccepted)
                WorldEpoch = message->WorldEpoch;
        }
        else if (message && message->GetOpcode() == NotifyCampaignCheckpoint::Opcode)
        {
            const auto& checkpoint = *static_cast<NotifyCampaignCheckpoint*>(message.get());
            if (!checkpoint.Decoded)
                return;
            if (checkpoint.State == NotifyCampaignCheckpoint::Begin)
            {
                Nonce = checkpoint.Nonce;
                CampaignRevision = checkpoint.Revision;
                SeedOwner = checkpoint.SeedOwner;
                Journals.clear();
            }
            else if (checkpoint.State == NotifyCampaignCheckpoint::End && AutoReady && (!checkpoint.BaselinePending || SeedOwner))
            {
                if (SeedOwner)
                    for (const auto& quest : Seed)
                    {
                        RequestCampaignSeed seed;
                        seed.Nonce = Nonce;
                        seed.Data = quest;
                        SendMessage(seed);
                    }
                RequestCampaignReady ready;
                ready.Nonce = Nonce;
                ready.Revision = CampaignRevision;
                ready.SeedCount = SeedOwner ? static_cast<uint32_t>(Seed.size()) : 0;
                SendMessage(ready);
            }
            else if (checkpoint.State == NotifyCampaignCheckpoint::Accepted && checkpoint.Nonce == Nonce)
                Ready = true;
        }
        else if (message && message->GetOpcode() == NotifyQuestJournal::Opcode)
            Journals.push_back(*static_cast<NotifyQuestJournal*>(message.get()));
        else if (message && message->GetOpcode() == NotifyDialogueLease::Opcode)
        {
            DialogueLease = *static_cast<NotifyDialogueLease*>(message.get());
            ++DialogueNotifications;
        }
        else if (message && message->GetOpcode() == NotifyPartyLocations::Opcode)
        {
            const auto& snapshot = *static_cast<const NotifyPartyLocations*>(message.get());
            if (snapshot.IsValid())
            {
                PartyLocations = snapshot;
                ++LocationNotifications;
            }
        }
        else if (message && message->GetOpcode() == NotifyPartyLeft::Opcode)
            InParty = false;
        else if (message && message->GetOpcode() == AssignCharacterResponse::Opcode)
            Assignment = *static_cast<AssignCharacterResponse*>(message.get());
        else if (message && message->GetOpcode() == NotifyInventoryChanges::Opcode)
            ++InventoryNotifications;
        else if (message && message->GetOpcode() == AssignObjectsResponse::Opcode)
            Objects = *static_cast<AssignObjectsResponse*>(message.get());
        else if (message && message->GetOpcode() == NotifyLockChange::Opcode)
            ++LockNotifications;
        else if (message && message->GetOpcode() == NotifyPartyJoined::Opcode)
        {
            InParty = true;
            PartyLeader = static_cast<NotifyPartyJoined*>(message.get())->IsLeader;
        }
        else if (message && message->GetOpcode() == NotifyWeatherChange::Opcode)
        {
            ++WeatherNotifications;
            LastWeather = static_cast<NotifyWeatherChange*>(message.get())->Id;
        }
        else if (message && message->GetOpcode() == ServerTimeSettings::Opcode)
        {
            ++TimeNotifications;
            Time = *static_cast<ServerTimeSettings*>(message.get());
        }
        else if (message && message->GetOpcode() == NotifyCoopSleep::Opcode)
        {
            RestState = *static_cast<NotifyCoopSleep*>(message.get());
            if (RestState->IsValid() && RestState->Phase == CoopSleepVote::Phase::Committed)
                ++RestCommits;
        }
        else if (message && message->GetOpcode() == NotifyPlayerCellChanged::Opcode)
            PlayerCell = *static_cast<NotifyPlayerCellChanged*>(message.get());
        else if (message && message->GetOpcode() == NotifyQuestUpdate::Opcode)
        {
            if (!static_cast<NotifyQuestUpdate*>(message.get())->AcknowledgementOnly)
                ++QuestNotifications;
            LastQuestStage = static_cast<NotifyQuestUpdate*>(message.get())->Stage;
        }
        else if (message && message->GetOpcode() == ServerReferencesMoveRequest::Opcode)
        {
            const auto& moves = *static_cast<ServerReferencesMoveRequest*>(message.get());
            MovementNotifications += moves.Updates.size();
            for (const auto& [id, move] : moves.Updates)
                LastPositionX = move.Position.x;
        }
        else if (message && message->GetOpcode() == NotifyActionEvents::Opcode)
        {
            const auto& actions = *static_cast<NotifyActionEvents*>(message.get());
            ActionNotifications += actions.Actions.size();
            for (const auto& [id, epoch] : actions.OwnershipEpochs)
                LastActionEpoch = epoch;
        }
    }
};

AuthenticationRequest MakeRequest()
{
    AuthenticationRequest request;
    request.Version = BUILD_COMMIT;
    request.RuntimeVersion = "1.7.104.0";
    request.Username = "Protocol probe";
    uint16_t standard = 0;
    uint16_t lite = 0;
    for (const char* name : {"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"})
        request.UserMods.ModList.push_back({name, standard++, false});
    for (const auto name : kAnniversaryPlugins)
    {
        const bool cLite = name.ends_with(".esl");
        auto& entry = request.UserMods.ModList.emplace_back();
        entry.Filename = String(name.data(), name.size());
        entry.Id = cLite ? lite++ : standard++;
        entry.IsLite = cLite;
        entry.Masters = {"Skyrim.esm", "Update.esm"}; // synthetic metadata, no game files
        auto& archive = request.UserMods.Resources.emplace_back();
        archive.Filename = "data/" + entry.Filename.substr(0, entry.Filename.size() - 4) + ".bsa";
        archive.Digest.fill(43);
    }
    for (auto& entry : request.UserMods.ModList)
        entry.Digest.fill(42);
    for (const char* name : {"runtime/address-library", "data/loose-files"})
    {
        auto& resource = request.UserMods.Resources.emplace_back();
        resource.Filename = name;
        resource.Digest.fill(42);
    }
    request.CellId = GameId(1, 0x123);
    return request;
}

bool ConnectProbe(Probe& aProbe, const std::string& acAddress, ResponseType aExpected, bool aRequireReady = true)
{
    if (!aProbe.Connect(acAddress))
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!aProbe.Reply || (aRequireReady && aExpected == ResponseType::kAccepted && aProbe.Reply->Type == aExpected && !aProbe.Ready)) &&
           std::chrono::steady_clock::now() < deadline)
    {
        aProbe.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return aProbe.Reply && aProbe.Reply->Type == aExpected && (!aRequireReady || aExpected != ResponseType::kAccepted || aProbe.Ready);
}

bool CheckInventoryReplay(Probe& aOwner, const std::string& acAddress)
{
    Probe observer;
    observer.Request = MakeRequest();
    if (!ConnectProbe(observer, acAddress, ResponseType::kAccepted))
        return false;
    EnterInteriorCellRequest enter;
    enter.CellId = GameId(0, 0x123);
    aOwner.SendMessage(enter);
    observer.SendMessage(enter);
    const auto pumpUntil = [&](const auto& acDone)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!acDone() && std::chrono::steady_clock::now() < deadline)
        {
            aOwner.Update();
            observer.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return acDone();
    };
    AssignCharacterRequest assign;
    const uint32_t ccModId = aOwner.Reply->UserMods.ModList.back().Id;
    assign.ReferenceId = GameId(ccModId, 0x900);
    assign.CellId = GameId(0, 0x123);
    Inventory::Entry item;
    item.BaseId = GameId(ccModId, 0x800);
    item.Count = 10;
    assign.CurrentActorData.InitialInventory.Entries.push_back(item);
    aOwner.SendMessage(assign);
    if (!pumpUntil([&] { return aOwner.Assignment.has_value(); }))
    {
        std::cerr << "FAIL: inventory test did not receive NPC assignment\n";
        return false;
    }
    RequestInventoryChanges change;
    change.ServerId = aOwner.Assignment->ServerId;
    change.OwnershipEpoch = aOwner.Assignment->OwnershipEpoch;
    change.OperationId = 100;
    change.Item.BaseId = item.BaseId;
    change.Item.Count = 2;
    aOwner.SendMessage(change);
    aOwner.SendMessage(change); // replay the exact same operation
    change.OperationId = 101;
    change.Item.Count = 3;
    aOwner.SendMessage(change);
    change.OperationId = 102;
    change.OwnershipEpoch += 99;
    change.Item.Count = 100;
    aOwner.SendMessage(change); // stale ownership must not consume this operation ID
    change.OwnershipEpoch = aOwner.Assignment->OwnershipEpoch;
    change.Item.Count = 5;
    aOwner.SendMessage(change);
    if (!pumpUntil([&] { return observer.InventoryNotifications >= 3; }))
    {
        std::cerr << "FAIL: inventory test did not receive expected deltas\n";
        return false;
    }
    ClientReferencesMoveRequest moves;
    moves.Tick = 100;
    auto& move = moves.Updates[aOwner.Assignment->ServerId];
    move.OwnershipEpoch = aOwner.Assignment->OwnershipEpoch + 99;
    move.CellId = enter.CellId;
    move.Position.x = 999;
    aOwner.SendMessage(moves); // Wrong generation must not move or relay the actor.
    move.OwnershipEpoch = aOwner.Assignment->OwnershipEpoch;
    move.Position.x = 10;
    aOwner.SendMessage(moves);
    moves.Tick = 90;
    move.Position.x = 900;
    aOwner.SendMessage(moves); // An old unreliable snapshot must not roll the canonical position back.
    moves.Tick = 101;
    move.Position.x = 20;
    aOwner.SendMessage(moves);
    RequestActionEvents actions;
    const uint32_t actorId = aOwner.Assignment->ServerId;
    actions.OwnershipEpochs[actorId] = aOwner.Assignment->OwnershipEpoch + 99;
    actions.Actions[actorId].emplace_back().Tick = 101;
    aOwner.SendMessage(actions);
    actions.OwnershipEpochs[actorId] = aOwner.Assignment->OwnershipEpoch;
    aOwner.SendMessage(actions);
    if (!pumpUntil([&] { return observer.MovementNotifications >= 2 && observer.ActionNotifications >= 1; }))
    {
        std::cerr << "FAIL: movement/action probe did not receive the valid updates\n";
        return false;
    }
    const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    pumpUntil([&] { return std::chrono::steady_clock::now() >= settle; });
    const bool playbackPassed =
        observer.MovementNotifications == 2 && observer.LastPositionX == 20 && observer.ActionNotifications == 1 && observer.LastActionEpoch == aOwner.Assignment->OwnershipEpoch;
    std::cout << (playbackPassed ? "PASS: " : "FAIL: ") << "stale movement/action generations and reordered movement are rejected\n";

    observer.SendMessage(assign); // query the already owned NPC: return authoritative inventory
    if (!pumpUntil([&] { return observer.Assignment.has_value(); }))
    {
        std::cerr << "FAIL: inventory test did not receive inventory query response\n";
        return false;
    }
    GameId itemId = item.BaseId;
    const bool cPassed = observer.Assignment->CurrentInventory.GetEntryCountById(itemId) == 20 && observer.InventoryNotifications == 3;
    observer.Close();
    std::cout << (cPassed ? "PASS: " : "FAIL: ") << "inventory replay and stale ownership do not duplicate items\n";
    return cPassed && playbackPassed && observer.Assignment->Position.x == 20;
}

bool CheckObjectState(Probe& aOwner, const std::string& acAddress)
{
    Probe observer;
    observer.Request = MakeRequest();
    if (!ConnectProbe(observer, acAddress, ResponseType::kAccepted))
        return false;
    const auto pump = [&](Probe& aOther, const auto& acDone)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!acDone() && std::chrono::steady_clock::now() < deadline)
        {
            aOwner.Update();
            aOther.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return acDone();
    };
    const uint32_t ccModId = aOwner.Reply->UserMods.ModList.back().Id;
    EnterInteriorCellRequest enter;
    enter.CellId = GameId(ccModId, 0x801);
    aOwner.SendMessage(enter);
    observer.SendMessage(enter);
    AssignObjectsRequest assign;
    auto& object = assign.Objects.emplace_back();
    object.Id = GameId(ccModId, 0x802);
    object.CellId = enter.CellId;
    Inventory::Entry item;
    item.BaseId = GameId(ccModId, 0x803);
    item.Count = 10;
    object.CurrentInventory.Entries.push_back(item);
    aOwner.SendMessage(assign);
    if (!pump(observer, [&] { return aOwner.Objects.has_value(); }) || aOwner.Objects->Objects.size() != 1)
        return false;
    const uint32_t serverId = aOwner.Objects->Objects.front().ServerId;
    // The second client's stale upload must not overwrite the shared object snapshot.
    object.CurrentInventory.Entries.front().Count = 999;
    observer.SendMessage(assign);
    if (!pump(observer, [&] { return observer.Objects.has_value(); }) || observer.Objects->Objects.size() != 1 || observer.Objects->Objects.front().ServerId != serverId ||
        observer.Objects->Objects.front().IsSenderFirst || observer.Objects->Objects.front().CurrentInventory.GetEntryCountById(item.BaseId) != 10)
        return false;

    LockChangeRequest lock;
    lock.Id = object.Id;
    lock.CellId = enter.CellId;
    lock.IsLocked = true;
    lock.LockLevel = 49;
    aOwner.SendMessage(lock);
    RequestInventoryChanges change;
    change.ServerId = serverId;
    change.OperationId = 200;
    change.Item.BaseId = item.BaseId;
    change.Item.Count = -1;
    change.UpdateClients = true;
    aOwner.SendMessage(change);
    aOwner.SendMessage(change);
    observer.SendMessage(change); // a different authenticated connection has its own operation window
    if (!pump(observer, [&] { return observer.LockNotifications > 0 && observer.InventoryNotifications > 0; }))
        return false;
    // Wait for both reliable streams via a fresh snapshot request, then reconnect while
    // the owner keeps the cell alive. This deliberately does not claim disk persistence.
    observer.Objects.reset();
    observer.SendMessage(assign);
    if (!pump(observer, [&] { return observer.Objects.has_value(); }) || observer.Objects->Objects.front().CurrentInventory.GetEntryCountById(item.BaseId) != 8)
        return false;
    observer.Close();
    Probe reconnected;
    reconnected.Request = MakeRequest();
    if (!ConnectProbe(reconnected, acAddress, ResponseType::kAccepted))
        return false;
    reconnected.SendMessage(enter);
    reconnected.SendMessage(assign);
    if (!pump(reconnected, [&] { return reconnected.Objects.has_value(); }) || reconnected.Objects->Objects.size() != 1)
        return false;
    const auto& restored = reconnected.Objects->Objects.front();
    const bool cPassed = restored.ServerId == serverId && restored.CurrentLockData.IsLocked && restored.CurrentLockData.LockLevel == 49 &&
                         restored.CurrentInventory.GetEntryCountById(item.BaseId) == 8;
    reconnected.Close();
    std::cout << (cPassed ? "PASS: " : "FAIL: ") << "CC object shares inventory/lock snapshot across a reconnect while its cell remains active\n";
    return cPassed;
}

bool CheckQuestRelay(Probe& aOwner, const std::string& acAddress)
{
    Probe member;
    member.Request = MakeRequest();
    if (!ConnectProbe(member, acAddress, ResponseType::kAccepted))
        return false;
    const auto pump = [&](const auto& acDone)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!acDone() && std::chrono::steady_clock::now() < deadline)
        {
            aOwner.Update();
            member.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return acDone();
    };
    RequestQuestUpdate quest;
    quest.Id = GameId(aOwner.Reply->UserMods.ModList.back().Id, 0x804);
    quest.Stage = 0;
    quest.Status = RequestQuestUpdate::Started;
    quest.ClientQuestType = 2;
    aOwner.SendMessage(quest);
    quest.Stage = 10;
    quest.Status = RequestQuestUpdate::StageUpdate;
    aOwner.SendMessage(quest);
    if (!pump([&] { return member.QuestNotifications >= 2; }))
        return false;
    quest.Status = RequestQuestUpdate::Started;
    member.SendMessage(quest); // duplicate Started must not forget stage 10
    quest.Status = RequestQuestUpdate::StageUpdate;
    member.SendMessage(quest);
    quest.Stage = 20;
    member.SendMessage(quest); // another participant advances the same live quest
    if (!pump([&] { return aOwner.QuestNotifications >= 1; }))
        return false;
    const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    pump([&] { return std::chrono::steady_clock::now() >= settle; });
    const bool cPassed = aOwner.QuestNotifications == 1 && aOwner.LastQuestStage == 20 && member.QuestNotifications == 2;
    member.Close();
    std::cout << (cPassed ? "PASS: " : "FAIL: ") << "clients without a party advance one durable synthetic CC quest without relaying duplicate starts/stages\n";
    return cPassed;
}

bool CheckPersistentWorld(const std::string& acAddress, bool aTouch = false)
{
    Probe client;
    client.Request = MakeRequest();
    if (!ConnectProbe(client, acAddress, ResponseType::kAccepted))
        return false;
    const uint32_t mod = client.Reply->UserMods.ModList.back().Id;
    EnterInteriorCellRequest enter;
    enter.CellId = GameId(mod, 0x801);
    client.SendMessage(enter);
    AssignObjectsRequest assign;
    auto& object = assign.Objects.emplace_back();
    object.Id = GameId(mod, 0x802);
    object.CellId = enter.CellId;
    Inventory::Entry item;
    item.BaseId = GameId(mod, 0x803);
    item.Count = 999; // deliberately stale, must not replace the committed snapshot
    object.CurrentInventory.Entries.push_back(item);
    client.SendMessage(assign);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!client.Objects && std::chrono::steady_clock::now() < deadline)
    {
        client.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool passed = false;
    if (client.Objects && client.Objects->Objects.size() == 1)
    {
        const auto& restored = client.Objects->Objects.front();
        passed = !restored.IsSenderFirst && restored.Id == object.Id && restored.CurrentInventory.GetEntryCountById(item.BaseId) == 8 && restored.CurrentLockData.IsLocked &&
                 restored.CurrentLockData.LockLevel == 49;
    }
    std::cout << (passed ? "PASS: " : "FAIL: ") << "CC container survives empty cells and server restart without stale-upload replacement\n";
    if (passed && aTouch)
    {
        const uint32_t serverId = client.Objects->Objects.front().ServerId;
        LockChangeRequest lock;
        lock.Id = object.Id;
        lock.CellId = enter.CellId;
        lock.IsLocked = false;
        lock.LockLevel = 0;
        client.SendMessage(lock);
        lock.IsLocked = true;
        lock.LockLevel = 49;
        client.SendMessage(lock);
        RequestInventoryChanges change;
        change.ServerId = serverId;
        change.OperationId = 300;
        change.Item.BaseId = item.BaseId;
        change.Item.Count = 1;
        client.SendMessage(change);
        change.OperationId = 301;
        change.Item.Count = -1;
        client.SendMessage(change);
        client.Objects.reset();
        client.SendMessage(assign);
        const auto touched = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!client.Objects && std::chrono::steady_clock::now() < touched)
        {
            client.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        passed = client.Objects && client.Objects->Objects.size() == 1 && client.Objects->Objects.front().CurrentInventory.GetEntryCountById(item.BaseId) == 8 &&
                 client.Objects->Objects.front().CurrentLockData.IsLocked;
        std::cout << (passed ? "PASS: " : "FAIL: ") << "fresh lock and inventory commits are visible before crash recovery\n";
    }
    const auto savedQuest = std::find_if(client.Journals.begin(), client.Journals.end(), [&](const auto& entry) { return entry.Data.Id == GameId(mod, 0x804); });
    const bool questPassed = savedQuest != client.Journals.end() && savedQuest->Data.Events.size() == 3 && savedQuest->Data.Events.back().Stage == 20;
    std::cout << (questPassed ? "PASS: " : "FAIL: ") << "shared quest stages survive reconnect and server restart\n";
    client.Close();
    return passed && questPassed;
}

bool CheckCampaignAdmission(const std::string& address)
{
    Probe owner, waiting;
    owner.Request = MakeRequest();
    owner.AutoReady = false;
    waiting.Request = MakeRequest();
    const uint32_t mod = static_cast<uint32_t>(waiting.Request.UserMods.ModList.size() - 1);
    QuestJournal baseline;
    baseline.Id = GameId(mod, 0x805);
    baseline.Type = 8;
    baseline.Started = true;
    baseline.Events = {{1, 0, true}, {0, 5, true}};
    waiting.Seed.push_back(baseline);
    if (!ConnectProbe(owner, address, ResponseType::kAccepted, false) || !ConnectProbe(waiting, address, ResponseType::kAccepted, false))
        return false;
    const auto pump = [&](const auto& done)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done() && std::chrono::steady_clock::now() < deadline)
        {
            owner.Update();
            waiting.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return done();
    };
    if (!pump([&] { return owner.Nonce && waiting.Nonce; }) || !owner.SeedOwner || waiting.SeedOwner || owner.Ready || waiting.Ready)
        return false;
    RequestCampaignReady stale;
    stale.Nonce = owner.Nonce + 100;
    owner.SendMessage(stale);
    RequestCampaignSeed partial;
    partial.Nonce = owner.Nonce;
    partial.Data = baseline;
    partial.Data.Id.BaseId = 0x806;
    owner.SendMessage(partial); // no Ready: this partial import must never reach disk
    RequestQuestUpdate premature;
    premature.Id = GameId(mod, 0x808);
    premature.ClientQuestType = 8;
    premature.Status = RequestQuestUpdate::StageUpdate;
    premature.Stage = 77;
    owner.SendMessage(premature);
    AssignObjectsRequest assign;
    auto& object = assign.Objects.emplace_back();
    object.Id = GameId(mod, 0x807);
    object.CellId = GameId(mod, 0x801);
    Inventory::Entry item;
    item.BaseId = GameId(mod, 0x803);
    item.Count = 99;
    object.CurrentInventory.Entries.push_back(item);
    owner.SendMessage(assign);
    auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    pump([&] { return std::chrono::steady_clock::now() >= settle; });
    bool passed = !owner.Ready && !waiting.Ready && !owner.Objects;
    std::cout << (passed ? "PASS: " : "FAIL: ") << "campaign barrier blocks gameplay and ignores stale ready nonce\n";
    owner.Close();
    passed &= pump([&] { return waiting.Ready; });
    const bool seedPassed =
        waiting.Ready && waiting.Journals.size() == 1 && waiting.Journals.front().Data.Id == baseline.Id && waiting.Journals.front().Data.Events == baseline.Events;
    std::cout << (seedPassed ? "PASS: " : "FAIL: ") << "baseline lease transfers after disconnect and discards the partial import\n";
    EnterInteriorCellRequest enter;
    enter.CellId = object.CellId;
    waiting.SendMessage(enter);
    object.CurrentInventory.Entries.front().Count = 3;
    waiting.SendMessage(assign);
    passed &= pump([&] { return waiting.Objects.has_value(); });
    const bool objectPassed = waiting.Objects && waiting.Objects->Objects.size() == 1 && waiting.Objects->Objects.front().IsSenderFirst &&
                              waiting.Objects->Objects.front().CurrentInventory.GetEntryCountById(item.BaseId) == 3;
    std::cout << (objectPassed ? "PASS: " : "FAIL: ") << "pre-admission object uploads leave no persistent state\n";
    Probe reconnect;
    reconnect.Request = MakeRequest();
    const bool restored = ConnectProbe(reconnect, address, ResponseType::kAccepted) && reconnect.Journals.size() == 1 && reconnect.Journals.front().Data.Events == baseline.Events;
    std::cout << (restored ? "PASS: " : "FAIL: ") << "committed initial quest baseline is shared on reconnect\n";
    RequestCampaignSeed reseed;
    reseed.Nonce = reconnect.Nonce;
    reseed.Data = baseline;
    reconnect.SendMessage(reseed); // ready players cannot import another baseline
    // Admission seed packets outside a pending handshake cannot change the journal.
    reconnect.Close();
    waiting.Close();
    return passed && seedPassed && objectPassed && restored;
}

bool CheckEnvironment(const std::string& acAddress)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    Probe leader, member;
    leader.Request = MakeRequest();
    member.Request = MakeRequest();
    const auto check = [](bool aPassed, const char* apName)
    {
        std::cout << (aPassed ? "PASS: " : "FAIL: ") << apName << std::endl;
        return aPassed;
    };
    if (!check(ConnectProbe(leader, acAddress, ResponseType::kAccepted) && ConnectProbe(member, acAddress, ResponseType::kAccepted), "environment probes authenticate"))
        return false;
    const auto pump = [&](const auto& acDone, int aSeconds = 5)
    {
        const auto until = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(aSeconds));
        while (!acDone() && std::chrono::steady_clock::now() < until)
        {
            leader.Update();
            member.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return acDone();
    };
    // Admission/assignment do not enter a cell for Player.GetCellComponent().
    EnterInteriorCellRequest enter;
    enter.CellId = leader.Request.CellId;
    leader.SendMessage(enter);
    member.SendMessage(enter);
    AssignCharacterRequest assign;
    assign.ReferenceId = GameId(0, 0x14);
    assign.CellId = leader.Request.CellId;
    leader.SendMessage(assign);
    member.SendMessage(assign);
    if (!check(pump([&] { return leader.Assignment && member.Assignment; }), "both synthetic player characters are loaded"))
        return false;
    leader.SendMessage(PartyCreateRequest());
    if (!check(pump([&] { return leader.InParty && member.InParty; }) && leader.PartyLeader && !member.PartyLeader,
               "private default auto-party joins member under the leader"))
        return false;

    enter.CellId = GameId(1, 0x124);
    member.SendMessage(enter);
    if (!check(pump([&] { return leader.PlayerCell && leader.PlayerCell->PlayerId == member.Reply->PlayerId && leader.PlayerCell->CellId == enter.CellId; }),
               "member enters a different cell before weather replication"))
        return false;

    RequestWeatherChange weather;
    weather.Id = GameId(0, 0x10a230);
    leader.SendMessage(weather);
    if (!check(pump([&] { return member.WeatherNotifications > 0; }) && member.LastWeather == weather.Id, "leader weather reaches party member in a different cell"))
        return false;
    uint32_t before = member.WeatherNotifications;
    RequestWeatherChange forged;
    forged.Id = GameId(0, 0x10a231);
    member.SendMessage(forged);
    member.SendMessage(RequestCurrentWeather()); // Same reliable stream fences the attempted overwrite.
    if (!check(pump([&] { return member.WeatherNotifications > before; }) && member.LastWeather == weather.Id, "guest cannot overwrite party weather cache"))
        return false;
    before = leader.WeatherNotifications;
    RequestWeatherChange empty;
    empty.Id = GameId{};
    leader.SendMessage(empty);
    leader.SendMessage(RequestCurrentWeather());
    if (!check(pump([&] { return leader.WeatherNotifications > before; }) && leader.LastWeather == weather.Id, "empty leader weather cannot erase cache"))
        return false;
    before = member.WeatherNotifications;
    weather.Id = GameId(0, 0x10a232);
    leader.SendMessage(weather);
    if (!pump([&] { return member.WeatherNotifications > before; }))
        return check(false, "leader replacement weather arrives");
    before = member.WeatherNotifications;
    member.SendMessage(RequestCurrentWeather());
    if (!check(pump([&] { return member.WeatherNotifications > before; }) && member.LastWeather == weather.Id, "member cache request returns updated leader weather"))
        return false;

    if (!leader.Time || !member.Time)
        return check(false, "initial server clock snapshot arrives");
    const uint32_t leaderTimeBefore = leader.TimeNotifications;
    const uint32_t memberTimeBefore = member.TimeNotifications;
    const float previousHour = leader.Time->timeModel.Time;
    std::cout << "Waiting for the normal 60-second server calendar refresh (90-second total deadline)" << std::endl;
    const bool refreshed = pump([&] { return leader.TimeNotifications > leaderTimeBefore && member.TimeNotifications > memberTimeBefore; }, 90);
    const bool passed = check(refreshed && leader.TimeNotifications == leaderTimeBefore + 1 && member.TimeNotifications == memberTimeBefore + 1 &&
                              *leader.Time == *member.Time && leader.Time->timeModel.TimeScale > 0.f && leader.Time->timeModel.Time != previousHour,
                              "one automatic calendar refresh reaches both loaded players and time advances");
    leader.Close();
    member.Close();
    return passed;
}

bool CheckCoopSleep(const std::string& acAddress)
{
    Probe host, guest;
    host.Request = MakeRequest();
    host.Request.PlayerTime.Year = 201;
    host.Request.PlayerTime.Month = 11;
    host.Request.PlayerTime.Day = 31;
    host.Request.PlayerTime.Time = 23.5f;
    host.Request.PlayerTime.DaysPassed = 7000.5;
    guest.Request = MakeRequest();
    guest.Request.PlayerTime.Year = 299;
    const auto check = [](bool aPassed, const char* apName)
    {
        std::cout << (aPassed ? "PASS: " : "FAIL: ") << apName << std::endl;
        return aPassed;
    };
    if (!check(ConnectProbe(host, acAddress, ResponseType::kAccepted) && ConnectProbe(guest, acAddress, ResponseType::kAccepted),
               "rest probes authenticate on the same world epoch"))
        return false;
    const auto pump = [&](const auto& acDone)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!acDone() && std::chrono::steady_clock::now() < until)
        {
            host.Update();
            guest.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return acDone();
    };
    EnterInteriorCellRequest enter;
    enter.CellId = host.Request.CellId;
    host.SendMessage(enter);
    guest.SendMessage(enter);
    AssignCharacterRequest assign;
    assign.ReferenceId = GameId(0, 0x14);
    assign.CellId = host.Request.CellId;
    host.SendMessage(assign);
    guest.SendMessage(assign);
    if (!check(pump([&] { return host.Assignment && guest.Assignment; }), "rest probe characters are loaded"))
        return false;
    host.SendMessage(PartyCreateRequest());
    if (!check(pump([&] { return host.InParty && guest.InParty; }), "two rest participants form a party while in different cells"))
        return false;
    if (!check(host.Time && guest.Time && host.Time->timeModel.Year == 201 && guest.Time->timeModel.Year == 201 && host.WorldEpoch == guest.WorldEpoch && host.WorldEpoch != 0,
               "late guest's future calendar cannot replace the host timeline"))
        return false;
    enter.CellId = GameId(1, 0x124);
    guest.SendMessage(enter);
    if (!check(pump([&] { return host.PlayerCell && host.PlayerCell->PlayerId == guest.Reply->PlayerId && host.PlayerCell->CellId == enter.CellId; }),
               "guest enters another cell before the rest transaction"))
        return false;
    const uint32_t initialTime = host.TimeNotifications;
    host.Rest(CoopSleepVote::Action::Vote, 8);
    if (!check(pump([&] { return host.RestState && guest.RestState && guest.RestState->Phase == CoopSleepVote::Phase::Waiting; }) &&
               host.RestCommits == 0 && guest.RestCommits == 0 && host.TimeNotifications == initialTime,
               "one vote notifies both players without skipping time"))
        return false;
    const uint64_t firstRound = guest.RestState->Round;
    guest.Rest(CoopSleepVote::Action::Vote, 4, firstRound);
    if (!check(pump([&] { return host.RestState->Phase == CoopSleepVote::Phase::Preparing && guest.RestState->Phase == CoopSleepVote::Phase::Preparing; }) &&
               host.RestCommits == 0 && guest.RestCommits == 0 && host.TimeNotifications == initialTime,
               "both votes still wait for live menu acknowledgements"))
        return false;
    host.Rest(CoopSleepVote::Action::Ready, 0, firstRound);
    guest.Rest(CoopSleepVote::Action::Ready, 0, firstRound);
    if (!check(pump([&] { return host.RestCommits == 1 && guest.RestCommits == 1; }) && host.RestState->Hours == 4 &&
               host.RestState->Time == guest.RestState->Time && host.RestState->Time.Year == 202 && host.RestState->Time.Month == 0 &&
               host.RestState->Time.Day == 1 && std::abs(host.RestState->Time.Time - 3.5f) < 0.1f &&
               std::abs(host.RestState->Time.DaysPassed - (7000.5 + 4.0 / 24.0)) < 0.005,
               "one four-hour commit advances the same hour, date and elapsed days across midnight"))
        return false;
    host.Rest(CoopSleepVote::Action::Ready, 0, firstRound); // fresh sequence, expired transaction
    host.Rest(CoopSleepVote::Action::Vote, 2);
    if (!pump([&] { return guest.RestState->Round > firstRound && guest.RestState->Phase == CoopSleepVote::Phase::Waiting; }))
        return check(false, "next rest transaction starts");
    const uint64_t secondRound = guest.RestState->Round;
    guest.Rest(CoopSleepVote::Action::Vote, 2, secondRound);
    if (!pump([&] { return guest.RestState->Phase == CoopSleepVote::Phase::Preparing; }))
        return check(false, "next rest transaction reaches prepare");
    host.Rest(CoopSleepVote::Action::Cancel, 0, secondRound);
    guest.Rest(CoopSleepVote::Action::Ready, 0, secondRound);
    if (!check(pump([&] { return guest.RestState->Phase == CoopSleepVote::Phase::Cancelled; }) && host.RestCommits == 1 && guest.RestCommits == 1,
               "closing either menu cancels prepare and replay cannot skip time twice"))
        return false;
    host.Rest(CoopSleepVote::Action::Vote, 1);
    if (!pump([&] { return guest.RestState->Round > secondRound && guest.RestState->Phase == CoopSleepVote::Phase::Waiting; }))
        return check(false, "disconnect scenario starts");
    guest.Close();
    if (!check(pump([&] { return host.RestState->Phase == CoopSleepVote::Phase::Cancelled; }) && host.RestCommits == 1,
               "participant disconnect cancels an outstanding rest vote"))
        return false;
    host.Close();
    return true;
}

bool CheckDialogueLease(const std::string& acAddress)
{
    Probe host, guest;
    host.Request = MakeRequest();
    guest.Request = MakeRequest();
    const auto check = [](bool aPassed, const char* apName)
    {
        std::cout << (aPassed ? "PASS: " : "FAIL: ") << apName << std::endl;
        return aPassed;
    };
    if (!check(ConnectProbe(host, acAddress, ResponseType::kAccepted) && ConnectProbe(guest, acAddress, ResponseType::kAccepted),
               "dialogue participants authenticate"))
        return false;
    const auto pump = [&](const auto& acDone)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!acDone() && std::chrono::steady_clock::now() < until)
        {
            host.Update();
            guest.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return acDone();
    };
    const auto settle = [&](uint32_t aMilliseconds = 350)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(aMilliseconds);
        return pump([&] { return std::chrono::steady_clock::now() >= until; });
    };
    EnterInteriorCellRequest enter;
    enter.CellId = host.Request.CellId;
    host.SendMessage(enter);
    guest.SendMessage(enter);
    AssignCharacterRequest assign;
    assign.ReferenceId = GameId(0, 0x14);
    assign.CellId = enter.CellId;
    host.SendMessage(assign);
    guest.SendMessage(assign);
    if (!check(pump([&] { return host.Assignment && guest.Assignment; }), "dialogue player characters load"))
        return false;
    host.SendMessage(PartyCreateRequest());
    if (!check(pump([&] { return host.InParty && guest.InParty; }), "dialogue players form one party"))
        return false;
    host.Assignment.reset();
    assign.ReferenceId = GameId(0, 0xabc123);
    host.SendMessage(assign);
    if (!check(pump([&] { return host.Assignment.has_value(); }), "host owns the synthetic NPC"))
        return false;
    RequestDialogueLease request;
    request.ServerId = host.Assignment->ServerId;
    request.OwnershipEpoch = host.Assignment->OwnershipEpoch;
    request.Sequence = 1;
    request.Tick = guest.GetClock().GetCurrentTick();
    request.Active = true;
    guest.SendMessage(request);
    if (!check(pump([&] { return host.DialogueLease && guest.DialogueLease; }) && host.DialogueLease->Active &&
               host.DialogueLease->SpeakerPlayerId == guest.Reply->PlayerId && host.DialogueLease->OwnershipEpoch == request.OwnershipEpoch,
               "guest conversation reaches the NPC owner with the current ownership epoch"))
        return false;
    const uint64_t firstRevision = host.DialogueLease->Revision;
    guest.SendMessage(request);
    request.Sequence = 2;
    request.OwnershipEpoch += 1;
    request.Tick = guest.GetClock().GetCurrentTick();
    guest.SendMessage(request);
    request.OwnershipEpoch = host.Assignment->OwnershipEpoch;
    host.SendMessage(request); // Another participant cannot steal a live lease.
    settle();
    if (!check(host.DialogueLease->Revision == firstRevision && host.DialogueLease->Active,
               "replay, stale ownership and a competing speaker cannot replace a live conversation"))
        return false;
    request.Sequence = 3;
    request.Tick = guest.GetClock().GetCurrentTick();
    guest.SendMessage(request);
    if (!check(pump([&] { return host.DialogueLease->Revision > firstRevision; }) && host.DialogueLease->Active,
               "a fresh heartbeat renews the finite conversation lease"))
        return false;
    request.Sequence = 4;
    request.Tick = guest.GetClock().GetCurrentTick();
    request.Active = false;
    guest.SendMessage(request);
    if (!check(pump([&] { return !host.DialogueLease->Active; }), "closing the guest dialogue releases the NPC"))
        return false;
    settle();
    request.Sequence = 5;
    request.Tick = guest.GetClock().GetCurrentTick();
    request.Active = true;
    guest.SendMessage(request);
    if (!pump([&] { return host.DialogueLease->Active; }))
        return check(false, "new conversation starts");
    request.Sequence = 4;
    request.Active = false;
    guest.SendMessage(request);
    settle();
    if (!check(host.DialogueLease->Active, "an old release cannot end a newer conversation"))
        return false;
    if (!check(pump([&] { return !host.DialogueLease->Active; }), "missing heartbeats expire while both sockets remain connected"))
        return false;
    request.Sequence = 6;
    request.Tick = guest.GetClock().GetCurrentTick();
    request.Active = true;
    guest.SendMessage(request);
    if (!pump([&] { return host.DialogueLease->Active; }))
        return check(false, "cell change scenario starts");
    enter.CellId = GameId(0, 0x124);
    guest.SendMessage(enter);
    if (!check(pump([&] { return !host.DialogueLease->Active; }), "leaving the NPC cell releases its conversation"))
        return false;
    const uint64_t releasedRevision = host.DialogueLease->Revision;
    settle();
    request.Sequence = 7;
    request.Tick = guest.GetClock().GetCurrentTick();
    guest.SendMessage(request);
    settle();
    if (!check(host.DialogueLease->Revision == releasedRevision && !host.DialogueLease->Active,
               "a participant in another interior cannot renew the NPC lease"))
        return false;
    enter.CellId = host.Request.CellId;
    guest.SendMessage(enter);
    settle();
    request.Sequence = 8;
    request.Tick = guest.GetClock().GetCurrentTick();
    guest.SendMessage(request);
    if (!pump([&] { return host.DialogueLease->Active; }))
        return check(false, "disconnect scenario starts");
    guest.Close();
    if (!check(pump([&] { return !host.DialogueLease->Active; }), "guest disconnect releases its NPC without a release packet"))
        return false;
    host.Close();
    return true;
}

bool CheckPartyLocations(const std::string& acAddress)
{
    Probe host, guest, outsider;
    host.Request = guest.Request = outsider.Request = MakeRequest();
    const auto check = [](bool aPassed, const char* apName)
    {
        std::cout << (aPassed ? "PASS: " : "FAIL: ") << apName << std::endl;
        return aPassed;
    };
    const auto pump = [&](const auto& acDone, int aMilliseconds = 6000)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(aMilliseconds);
        while (!acDone() && std::chrono::steady_clock::now() < until)
        {
            host.Update(); guest.Update(); outsider.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return acDone();
    };
    const auto settle = [&](int aMilliseconds)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(aMilliseconds);
        pump([&] { return std::chrono::steady_clock::now() >= until; }, aMilliseconds + 1000);
    };
    if (!check(ConnectProbe(host, acAddress, ResponseType::kAccepted) && ConnectProbe(guest, acAddress, ResponseType::kAccepted) &&
               ConnectProbe(outsider, acAddress, ResponseType::kAccepted), "location probes authenticate"))
        return false;
    EnterInteriorCellRequest enter;
    enter.CellId = host.Request.CellId;
    host.SendMessage(enter); guest.SendMessage(enter); outsider.SendMessage(enter);
    AssignCharacterRequest assign;
    assign.ReferenceId = GameId(0, 0x14);
    assign.CellId = enter.CellId;
    host.SendMessage(assign); guest.SendMessage(assign); outsider.SendMessage(assign);
    if (!check(pump([&] { return host.Assignment && guest.Assignment && outsider.Assignment; }), "all synthetic player characters load"))
        return false;
    host.SendMessage(PartyCreateRequest());
    if (!pump([&] { return host.InParty && guest.InParty && outsider.InParty; }))
        return check(false, "location party forms");
    outsider.SendMessage(PartyLeaveRequest());
    if (!check(pump([&] { return !outsider.InParty; }), "observer leaves the private party"))
        return false;
    const uint32_t outsiderBefore = outsider.LocationNotifications;
    if (!check(pump([&] { return host.PartyLocations && guest.PartyLocations; }) && host.PartyLocations->Players.size() == 2,
               "bounded snapshots include both ready party characters"))
        return false;
    auto findGuest = [&]() -> const NotifyPartyLocations::Location*
    {
        if (host.PartyLocations)
            for (const auto& location : host.PartyLocations->Players)
                if (location.PlayerId == guest.Reply->PlayerId)
                    return &location;
        return nullptr;
    };
    enter.CellId = GameId(0, 0x124);
    guest.SendMessage(enter);
    const uint64_t beforeCellChange = host.PartyLocations->Sequence;
    if (!check(pump([&] { return host.PartyLocations->Sequence > beforeCellChange && findGuest() == nullptr; }),
               "a cell transition cannot relabel coordinates sampled in the previous cell"))
        return false;
    ClientReferencesMoveRequest movement;
    movement.Tick = guest.GetClock().GetCurrentTick() + 100;
    auto& position = movement.Updates[guest.Assignment->ServerId];
    position.OwnershipEpoch = guest.Assignment->OwnershipEpoch;
    position.CellId = enter.CellId;
    position.Position = glm::vec3(100.f, -200.f, 30.f);
    guest.SendMessage(movement);
    if (!check(pump([&] { const auto* location = findGuest(); return location && location->Cell == enter.CellId && location->Position.x == 100.f; }),
               "a guest in another interior still reaches the host map stream"))
        return false;
    const uint64_t acceptedTick = findGuest()->ReceivedTick;
    const uint64_t acceptedSequence = host.PartyLocations->Sequence;
    movement.Tick += 100;
    position.OwnershipEpoch += 10;
    position.Position.x = 999.f;
    guest.SendMessage(movement);
    position.OwnershipEpoch = guest.Assignment->OwnershipEpoch;
    host.SendMessage(movement); // Wrong sender for the guest's character.
    movement.Tick -= 200;
    guest.SendMessage(movement); // Out-of-order packet from the correct sender.
    if (!check(pump([&] { return host.PartyLocations->Sequence > acceptedSequence; }) && findGuest() &&
               findGuest()->Position.x == 100.f && findGuest()->ReceivedTick == acceptedTick,
               "wrong sender, stale ownership and replay cannot move or refresh the marker"))
        return false;
    settle(600);
    if (!check(outsider.LocationNotifications == outsiderBefore, "party map positions are not sent to a non-member"))
        return false;
    if (!check(pump([&] { return findGuest() == nullptr; }), "missing movement observations expire from full snapshots"))
        return false;
    movement.Tick = guest.GetClock().GetCurrentTick() + 200;
    position.Position.x = 150.f;
    guest.SendMessage(movement);
    if (!check(pump([&] { const auto* location = findGuest(); return location && location->Position.x == 150.f; }),
               "a fresh accepted observation restores the distant player location"))
        return false;
    guest.SendMessage(PartyLeaveRequest());
    if (!check(pump([&] { return !guest.InParty && findGuest() == nullptr; }), "leaving the party removes the marker location"))
        return false;
    host.Close(); guest.Close(); outsider.Close();
    return true;
}

int main(int argc, char** argv)
{
    const std::string address = argc > 1 ? argv[1] : "127.0.0.1:10582";
    if (argc > 2 && std::string_view(argv[2]) == "--verify-locations")
        return CheckPartyLocations(address) ? 0 : 1;
    if (argc > 2 && std::string_view(argv[2]) == "--verify-dialogue")
        return CheckDialogueLease(address) ? 0 : 1;
    if (argc > 2 && std::string_view(argv[2]) == "--verify-coop-sleep")
        return CheckCoopSleep(address) ? 0 : 1;
    if (argc > 2 && std::string_view(argv[2]) == "--verify-environment")
        return CheckEnvironment(address) ? 0 : 1;
    if (argc > 2 && std::string_view(argv[2]) == "--verify-campaign-admission")
        return CheckCampaignAdmission(address) ? 0 : 1;
    if (argc > 2 && (std::string_view(argv[2]) == "--verify-world" || std::string_view(argv[2]) == "--touch-world"))
        return CheckPersistentWorld(address, std::string_view(argv[2]) == "--touch-world") ? 0 : 1;
    Probe rejected;
    rejected.Request = MakeRequest();
    rejected.Request.CellId = GameId(1000, 1);
    rejected.Request.UserMods.ModList.front().Digest[0] ^= 1;
    if (!ConnectProbe(rejected, address, ResponseType::kContentMismatch))
    {
        std::cerr << "FAIL: malformed first client must be refused before pinning a session\n";
        return 1;
    }
    rejected.Close();
    Probe baseline;
    baseline.Request = MakeRequest();
    if (!ConnectProbe(baseline, address, ResponseType::kAccepted))
    {
        std::cerr << "FAIL: baseline was not accepted; use a fresh private server with content checks enabled\n";
        return 1;
    }
    bool success = CheckInventoryReplay(baseline, address);
    success &= CheckObjectState(baseline, address);
    success &= CheckQuestRelay(baseline, address);
    success &= !baseline.Reply->UserMods.ModList.empty() && baseline.Reply->UserMods.ModList.front().Id == 0;
    const auto check = [&](const char* apName, AuthenticationRequest aRequest, ResponseType aExpected)
    {
        Probe client;
        client.Request = std::move(aRequest);
        const bool cPassed = ConnectProbe(client, address, aExpected);
        std::cout << (cPassed ? "PASS: " : "FAIL: ") << apName << '\n';
        success &= cPassed;
        client.Close();
        baseline.Update();
    };
    check("matching active content", MakeRequest(), ResponseType::kAccepted);
    auto changed = MakeRequest();
    changed.ProtocolVersion += 1;
    check("unknown protocol schema", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.Token = "deliberately-wrong-password";
    check("wrong password", changed, ResponseType::kWrongPassword);
    changed = MakeRequest();
    changed.RuntimeVersion = "1.6.1170.0";
    check("runtime mismatch", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList[1].Digest[3] = 9;
    check("same filename with different bytes", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList.pop_back();
    check("missing Anniversary ESL", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.Resources.front().Digest[0] ^= 1;
    check("same plugins with different BSA", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.Resources.pop_back();
    check("missing loose-resource fingerprint", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList.back().Masters.push_back("missing-master.esm");
    check("missing plugin master", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList.push_back({"ccExtra.esl", 0, true});
    changed.UserMods.ModList.back().Digest.fill(9);
    check("unsupported extra Creation", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    std::swap(changed.UserMods.ModList[1], changed.UserMods.ModList[2]);
    check("different load order", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList[0].Digest.fill(0);
    check("missing fingerprint", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.CellId = GameId(1000, 1);
    check("invalid initial location", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.Version = "old-client";
    check("old protocol version", changed, ResponseType::kWrongVersion);
    baseline.Close();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    changed = MakeRequest();
    changed.UserMods.ModList[0].Digest[0] ^= 1;
    check("session contract remains after disconnect", changed, ResponseType::kContentMismatch);
    check("reconnect to the unchanged session", MakeRequest(), ResponseType::kAccepted);
    return success ? 0 : 1;
}
