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
#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <BuildInfo.h>
#include <Messages/NotifyCampaignCheckpoint.h>
#include <Messages/NotifyQuestJournal.h>
#include <Messages/RequestCampaignReady.h>
#include <Messages/RequestCampaignSeed.h>
#include <chrono>
#include <iostream>
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
    bool Ready{};
    bool AutoReady{true};
    uint64_t Nonce{};
    uint64_t CampaignRevision{};
    bool SeedOwner{};
    Vector<NotifyQuestJournal> Journals;
    Vector<QuestJournal> Seed;
    float LastPositionX{};
    void OnUpdate() override {}
    void OnDisconnected(EDisconnectReason) override { Ready = false; }
    void OnConnected() override { SendMessage(Request); }
    void SendMessage(const ClientMessage& acMessage)
    {
        Buffer buffer(65536);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8);
        acMessage.Serialize(writer);
        PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
        Send(&packet);
    }
    void OnConsume(const void* apData, uint32_t aSize) override
    {
        ViewBuffer buffer(reinterpret_cast<uint8_t*>(const_cast<void*>(apData)), aSize);
        Buffer::Reader reader(&buffer);
        ServerMessageFactory factory;
        const auto message = factory.Extract(reader);
        if (message && message->GetOpcode() == AuthenticationResponse::Opcode)
            Reply = *static_cast<AuthenticationResponse*>(message.get());
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
        else if (message && message->GetOpcode() == AssignCharacterResponse::Opcode)
            Assignment = *static_cast<AssignCharacterResponse*>(message.get());
        else if (message && message->GetOpcode() == NotifyInventoryChanges::Opcode)
            ++InventoryNotifications;
        else if (message && message->GetOpcode() == AssignObjectsResponse::Opcode)
            Objects = *static_cast<AssignObjectsResponse*>(message.get());
        else if (message && message->GetOpcode() == NotifyLockChange::Opcode)
            ++LockNotifications;
        else if (message && message->GetOpcode() == NotifyPartyJoined::Opcode)
            InParty = true;
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

int main(int argc, char** argv)
{
    const std::string address = argc > 1 ? argv[1] : "127.0.0.1:10582";
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
