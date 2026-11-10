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
#include <BuildInfo.h>
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
    uint32_t InventoryNotifications{};
    uint32_t MovementNotifications{};
    uint32_t ActionNotifications{};
    uint32_t LastActionEpoch{};
    float LastPositionX{};
    void OnUpdate() override {}
    void OnDisconnected(EDisconnectReason) override {}
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
        else if (message && message->GetOpcode() == AssignCharacterResponse::Opcode)
            Assignment = *static_cast<AssignCharacterResponse*>(message.get());
        else if (message && message->GetOpcode() == NotifyInventoryChanges::Opcode)
            ++InventoryNotifications;
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
    request.UserMods.ModList.push_back({"Skyrim.esm", 0, false});
    request.UserMods.ModList.push_back({"Update.esm", 1, false});
    for (auto& entry : request.UserMods.ModList)
        entry.Digest.fill(42);
    request.CellId = GameId(1, 0x123);
    return request;
}

bool ConnectProbe(Probe& aProbe, const std::string& acAddress, ResponseType aExpected)
{
    if (!aProbe.Connect(acAddress))
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!aProbe.Reply && std::chrono::steady_clock::now() < deadline)
    {
        aProbe.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return aProbe.Reply && aProbe.Reply->Type == aExpected;
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
    assign.ReferenceId = GameId(0, 0x1337);
    assign.CellId = GameId(0, 0x123);
    Inventory::Entry item;
    item.BaseId = GameId(0, 0xF);
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

int main(int argc, char** argv)
{
    const std::string address = argc > 1 ? argv[1] : "127.0.0.1:10582";
    Probe baseline;
    baseline.Request = MakeRequest();
    if (!ConnectProbe(baseline, address, ResponseType::kAccepted))
    {
        std::cerr << "FAIL: baseline was not accepted; use a fresh private server with content checks enabled\n";
        return 1;
    }
    bool success = CheckInventoryReplay(baseline, address);
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
    changed.RuntimeVersion = "1.6.1170.0";
    check("runtime mismatch", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList[1].Digest[3] = 9;
    check("same filename with different bytes", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList.pop_back();
    check("missing active plugin", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    changed.UserMods.ModList.push_back({"ccExtra.esl", 0, true});
    changed.UserMods.ModList.back().Digest.fill(9);
    check("unsupported extra Creation", changed, ResponseType::kContentMismatch);
    changed = MakeRequest();
    std::swap(changed.UserMods.ModList[0], changed.UserMods.ModList[1]);
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
