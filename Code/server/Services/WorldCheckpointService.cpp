#include <Services/WorldCheckpointService.h>
#include <World.h>
#include <GameServer.h>
#include <Components.h>
#include <TiltedCore/ViewBuffer.hpp>
#include <Messages/PartyCreateRequest.h>
#include <Services/QuestService.h>
#include <Services/QuestGrantService.h>
#include <algorithm>
#include <cstring>

WorldCheckpointService::WorldCheckpointService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
    m_requestConnection = aDispatcher.sink<PacketEvent<RequestWorldCheckpoint>>().connect<&WorldCheckpointService::OnRequest>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&WorldCheckpointService::OnUpdate>(this);
}
bool WorldCheckpointService::IsFrozen() const noexcept
{
    return m_step == Step::Saving || m_step == Step::PrepareLoad || m_step == Step::WaitingReconnect;
}
std::array<Player*, 2> WorldCheckpointService::Players() noexcept
{
    std::array<Player*, 2> players{};
    for (Player* player : m_world.GetPlayerManager())
        for (size_t i = 0; i < players.size(); ++i)
            if (player->GetPersistentCharacterId() == m_control.Members[i] && player->IsCampaignReady())
                players[i] = player;
    return players;
}
bool WorldCheckpointService::ReadManifest(uint64_t aId, WorldCheckpoint& aManifest) noexcept
{
    WorldStore::Checkpoint checkpoint;
    if (!m_world.GetCampaign().ReadCheckpoint(aId, checkpoint))
        return false;
    TiltedPhoques::ViewBuffer view(checkpoint.Metadata.data(), checkpoint.Metadata.size());
    TiltedPhoques::Buffer::Reader reader(&view);
    aManifest.Deserialize(reader);
    if (!aManifest.IsValid() || aManifest.Id != aId)
        return false;
    // Reject trailing bytes and noncanonical padding, not just a readable prefix.
    TiltedPhoques::Buffer encoded(4096);
    TiltedPhoques::Buffer::Writer writer(&encoded);
    aManifest.Serialize(writer);
    return writer.Size() == checkpoint.Metadata.size() && std::memcmp(encoded.GetWriteData(), checkpoint.Metadata.data(), writer.Size()) == 0;
}
bool WorldCheckpointService::Initialize() noexcept
{
    if (m_initialized)
        return true;
    auto& campaign = m_world.GetCampaign();
    if (!campaign.IsReady())
        return false;
    uint64_t id{}, epoch{};
    bool pending{};
    if (!campaign.LastRestoredCheckpoint(id, epoch) || !campaign.IsRestorePending(pending))
        return false;
    if (id && pending)
    {
        if (!ReadManifest(id, m_control.Manifest) || !campaign.ReserveCheckpoint(m_control.Nonce))
            return false;
        m_control.Id = id;
        m_control.WorldId = campaign.WorldId();
        m_control.PreviousEpoch = campaign.WorldEpoch();
        m_control.Calendar = m_control.Manifest.Calendar;
        for (size_t i = 0; i < 2; ++i)
            m_control.Members[i] = m_control.Manifest.Players[i].Character;
        m_step = Step::WaitingReconnect;
        m_world.GetCalendarService().RestoreCheckpointModel(m_control.Calendar);
    }
    m_initialized = true;
    return true;
}
bool WorldCheckpointService::CanAuthenticate(const CharacterIdentity& acCharacter, uint64_t aToken, const CheckpointAdmissionProof& acProof, String& aError) noexcept
{
    if (!Initialize())
    {
        aError = "Cannot recover checkpoint admission metadata; preserve the server database";
        return false;
    }
    if (m_step != Step::Idle && m_step != Step::WaitingReconnect)
    {
        aError = "A paired save/load is in progress. Reconnect after it completes";
        return false;
    }
    if (m_step == Step::WaitingReconnect && (aToken != m_control.Id ||
        (acCharacter != m_control.Members[0] && acCharacter != m_control.Members[1])))
    {
        aError = "The restored world is waiting for its original two checkpoint characters";
        return false;
    }
    uint64_t restored{}, epoch{};
    if (!m_world.GetCampaign().LastRestoredCheckpoint(restored, epoch))
    {
        aError = "Cannot verify the restored world timeline";
        return false;
    }
    // A native save from an abandoned future has a token below the rollback
    // revision. Only the selected pair or a later committed pair can rejoin.
    WorldCheckpoint manifest;
    if (restored && (!aToken || (aToken != restored && aToken <= epoch)))
    {
        aError = "This save is outside the restored world timeline. Load your matching paired checkpoint save";
        return false;
    }
    if (!aToken)
        return true;
    if (!acProof.IsValid() || acProof.WorldId != m_world.GetCampaign().WorldId() || acProof.Player.Character != acCharacter ||
        !ReadManifest(aToken, manifest) ||
        std::none_of(manifest.Players.begin(), manifest.Players.end(), [&](const auto& player) { return player == acProof.Player; }))
    {
        aError = "The loaded ESS/SKSE files do not match this world's committed paired checkpoint. Load the complete matching checkpoint save";
        return false;
    }
    return true;
}
void WorldCheckpointService::Send(NotifyWorldCheckpoint::Phase aPhase) noexcept
{
    m_control.State = aPhase;
    for (Player* player : Players())
        if (player)
            player->Send(m_control);
}
void WorldCheckpointService::Cancel(NotifyWorldCheckpoint::Error aError) noexcept
{
    m_control.Failure = aError;
    Send(NotifyWorldCheckpoint::Phase::Cancelled);
    m_step = Step::Idle;
    m_control = {};
    m_prepared = m_saved = 0;
}
void WorldCheckpointService::Begin(Player& aLeader, uint64_t aLoadId) noexcept
{
    using Error = NotifyWorldCheckpoint::Error;
    m_control = {};
    m_control.WorldId = m_world.GetCampaign().WorldId();
    m_control.PreviousEpoch = m_world.GetCampaign().WorldEpoch();
    if (m_world.ctx().at<QuestService>().HasPendingAuthority() || m_world.ctx().at<QuestGrantService>().HasPending())
    {
        m_control.State = NotifyWorldCheckpoint::Phase::Cancelled; m_control.Failure = Error::Busy;
        aLeader.Send(m_control); return;
    }
    if (!m_world.GetPartyService().IsPlayerLeader(&aLeader))
    {
        m_control.State = NotifyWorldCheckpoint::Phase::Cancelled; m_control.Failure = Error::NotLeader;
        aLeader.Send(m_control); return;
    }
    auto* party = m_world.GetPartyService().GetPlayerParty(&aLeader);
    if (!party || party->Members.size() != 2 || m_world.GetPlayerManager().Count() != 2 ||
        !party->Members[0]->IsCampaignReady() || !party->Members[1]->IsCampaignReady())
    {
        m_control.State = NotifyWorldCheckpoint::Phase::Cancelled; m_control.Failure = Error::NeedTwoPlayers;
        aLeader.Send(m_control); return;
    }
    m_control.Members[0] = aLeader.GetPersistentCharacterId();
    for (const Player* member : party->Members)
        if (member != &aLeader)
            m_control.Members[1] = member->GetPersistentCharacterId();
    if (aLoadId)
    {
        if (!ReadManifest(aLoadId, m_control.Manifest) || m_control.Manifest.Players[0].Character != m_control.Members[0] ||
            m_control.Manifest.Players[1].Character != m_control.Members[1])
        {
            Cancel(Error::DifferentCharacters); return;
        }
        m_control.Id = aLoadId;
    }
    if (!m_world.GetCampaign().ReserveCheckpoint(m_control.Nonce))
    {
        Cancel(Error::Storage); return;
    }
    if (!aLoadId)
        m_control.Id = m_control.Nonce;
    m_control.Calendar = m_world.GetCalendarService().GetModel();
    m_prepared = m_saved = 0;
    m_deadline = GameServer::Get()->GetTick() + 90000;
    m_step = aLoadId ? Step::PrepareLoad : Step::PrepareSave;
    Send(aLoadId ? NotifyWorldCheckpoint::Phase::PrepareLoad : NotifyWorldCheckpoint::Phase::PrepareSave);
}
void WorldCheckpointService::OnRequest(const PacketEvent<RequestWorldCheckpoint>& acPacket) noexcept
{
    const auto& request = acPacket.Packet;
    if (!request.IsValid() || !Initialize())
        return;
    if (request.Action == RequestWorldCheckpoint::Kind::Save || request.Action == RequestWorldCheckpoint::Kind::Load)
    {
        if (m_step == Step::Idle)
            Begin(*acPacket.pPlayer, request.Action == RequestWorldCheckpoint::Kind::Load ? request.Id : 0);
        return;
    }
    if (m_step == Step::Idle || m_step == Step::WaitingReconnect || request.Nonce != m_control.Nonce || request.Id != m_control.Id)
        return;
    const auto players = Players();
    size_t slot = 2;
    for (size_t i = 0; i < 2; ++i)
        if (players[i] == acPacket.pPlayer)
            slot = i;
    if (slot == 2)
        return;
    if (request.Action == RequestWorldCheckpoint::Kind::Failed)
    {
        Cancel(NotifyWorldCheckpoint::Error::Files); return;
    }
    if (request.Action == RequestWorldCheckpoint::Kind::Prepared && (m_step == Step::PrepareSave || m_step == Step::PrepareLoad))
    {
        m_prepared |= static_cast<uint8_t>(1 << slot);
        if (m_prepared != 3)
            return;
        if (m_step == Step::PrepareLoad)
        {
            Restore(); return;
        }
        m_step = Step::Saving;
        m_control.Calendar = m_world.GetCalendarService().GetModel();
        m_control.Manifest.Id = m_control.Id;
        m_control.Manifest.SavedEpoch = m_control.PreviousEpoch;
        m_control.Manifest.Calendar = m_control.Calendar;
        Send(NotifyWorldCheckpoint::Phase::SaveNow);
    }
    else if (request.Action == RequestWorldCheckpoint::Kind::Saved && m_step == Step::Saving)
    {
        if ((m_saved & (1 << slot)) || request.Contribution.Character != m_control.Members[slot])
            return;
        m_control.Manifest.Players[slot] = request.Contribution;
        m_saved |= static_cast<uint8_t>(1 << slot);
        if (m_saved != 3)
            return;
        TiltedPhoques::Buffer buffer(4096);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        m_control.Manifest.Serialize(writer);
        Vector<uint8_t> metadata(buffer.GetWriteData(), buffer.GetWriteData() + writer.Size());
        if (!m_control.Manifest.IsValid() || !m_world.GetCampaign().CaptureCheckpoint(m_control.Id, metadata))
        {
            Cancel(NotifyWorldCheckpoint::Error::Storage); return;
        }
        Send(NotifyWorldCheckpoint::Phase::Saved);
        m_step = Step::Idle;
        m_control = {};
    }
}
void WorldCheckpointService::Restore() noexcept
{
    const auto players = Players();
    if (!players[0] || !players[1])
    {
        Cancel(NotifyWorldCheckpoint::Error::Membership); return;
    }
    const std::array<ConnectionId_t, 2> connections{players[0]->GetConnectionId(), players[1]->GetConnectionId()};
    // Normal departure flushes loaded-object state. Do ALL of it before the
    // durable restore, never after it where it could overwrite restored rows.
    for (const auto connection : connections)
        GameServer::Get()->DetachCheckpointSession(connection);
    m_world.clear();
    if (!m_world.GetCampaign().RestoreCheckpoint(m_control.Id))
    {
        GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error()); return;
    }
    m_control.Calendar = m_control.Manifest.Calendar;
    m_world.GetCalendarService().RestoreCheckpointModel(m_control.Calendar);
    m_control.State = NotifyWorldCheckpoint::Phase::LoadNow;
    for (const auto connection : connections)
        GameServer::Get()->Send(connection, m_control);
    m_step = Step::WaitingReconnect;
}
void WorldCheckpointService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!Initialize())
        return;
    if (m_step == Step::Idle)
        return;
    const auto players = Players();
    if (m_step == Step::WaitingReconnect)
    {
        if (players[0] && players[1])
        {
            if (!m_world.GetCampaign().CompleteRestore())
            {
                GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error()); return;
            }
            m_world.GetPartyService().RestoreCheckpointLeader(players[0], players[1]);
            m_control.PreviousEpoch = m_world.GetCampaign().WorldEpoch();
            Send(NotifyWorldCheckpoint::Phase::Resume);
            m_step = Step::Idle; m_control = {};
        }
        return;
    }
    if (!players[0] || !players[1] || !m_world.GetPartyService().IsPlayerLeader(players[0]) ||
        players[0]->GetParty().JoinedPartyId != players[1]->GetParty().JoinedPartyId)
        Cancel(NotifyWorldCheckpoint::Error::Membership);
    else if (GameServer::Get()->GetTick() >= m_deadline)
        Cancel(NotifyWorldCheckpoint::Error::Timeout);
}
