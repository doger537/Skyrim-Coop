#include <GameServer.h>
#include <BuildInfo.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifyCampaignCheckpoint.h>
#include <Messages/NotifyQuestJournal.h>
#include <Messages/RequestCampaignReady.h>
#include <Messages/RequestCampaignSeed.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Events/PlayerJoinEvent.h>

void GameServer::RejectCampaign(ConnectionId_t aConnectionId, const String& acReason)
{
    spdlog::warn("Campaign admission/update refused: {}", acReason);
    AuthenticationResponse response;
    response.Type = AuthenticationResponse::ResponseType::kContentMismatch;
    response.Version = BUILD_COMMIT;
    response.ContentError = acReason;
    Send(aConnectionId, response);
    m_rejectedCampaigns[aConnectionId] = true;
    CampaignDisconnected(aConnectionId);
    Kick(aConnectionId);
}

void GameServer::BeginCampaignSync(Player* apPlayer)
{
    if (!apPlayer || apPlayer->IsCampaignReady() || m_requestStop)
        return;
    const auto connection = apPlayer->GetConnectionId();
    const auto found = m_pendingCampaigns.find(connection);
    if (found == m_pendingCampaigns.end())
        return;
    auto& pending = found.value();
    auto& campaign = m_pWorld->GetCampaign();
    if (!campaign.IsBootstrapped() && !m_seedOwner)
        m_seedOwner = connection;
    pending.Nonce = ++m_campaignNonce;
    pending.Revision = campaign.QuestRevision();
    pending.SeedOwner = !campaign.IsBootstrapped() && m_seedOwner == connection;
    pending.Seed.clear();
    NotifyCampaignCheckpoint checkpoint;
    checkpoint.State = NotifyCampaignCheckpoint::Begin;
    checkpoint.WorldId = campaign.WorldId();
    checkpoint.Nonce = pending.Nonce;
    checkpoint.Revision = pending.Revision;
    checkpoint.Count = static_cast<uint32_t>(campaign.Quests().size());
    checkpoint.SeedOwner = pending.SeedOwner;
    checkpoint.BaselinePending = !campaign.IsBootstrapped();
    checkpoint.AllowMisc = m_pWorld->GetQuestService().AllowsMisc();
    checkpoint.MiscQuests = campaign.MiscQuestIds();
    apPlayer->Send(checkpoint);
    for (const auto& quest : campaign.Quests())
    {
        NotifyQuestJournal journal;
        journal.Nonce = pending.Nonce;
        journal.Revision = quest.Revision;
        journal.Data = quest.Data;
        if (const auto* adapter = campaign.Adapter(quest.Data.Id))
        {
            journal.ReplayStages = adapter->ReplayStages;
            journal.ReplayLifecycle = adapter->ReplayLifecycle && quest.Data.Epoch == 1;
        }
        apPlayer->Send(journal);
    }
    checkpoint.State = NotifyCampaignCheckpoint::End;
    apPlayer->Send(checkpoint);
}

void GameServer::HandleCampaignSeed(ConnectionId_t aConnectionId, const RequestCampaignSeed& acRequest)
{
    if (m_requestStop || m_rejectedCampaigns.contains(aConnectionId))
        return;
    const auto found = m_pendingCampaigns.find(aConnectionId);
    if (found == m_pendingCampaigns.end() || found->second.Nonce != acRequest.Nonce)
        return;
    auto& pending = found.value();
    if (!pending.SeedOwner || m_pWorld->GetCampaign().IsBootstrapped() || !acRequest.Data.IsValid() || !m_pWorld->GetCampaign().ValidForm(acRequest.Data.Id) ||
        acRequest.Data.Epoch != 1 || !acRequest.Data.Decisions.empty() ||
        std::any_of(acRequest.Data.Events.begin(), acRequest.Data.Events.end(), [](const auto& event) { return !event.Bootstrap; }) || pending.Seed.size() >= 4095)
    {
        RejectCampaign(aConnectionId, "Invalid campaign baseline sender, nonce or quest count");
        return;
    }
    if (std::any_of(pending.Seed.begin(), pending.Seed.end(), [&](const auto& quest) { return quest.Id == acRequest.Data.Id; }))
    {
        RejectCampaign(aConnectionId, "Duplicate quest in the initial campaign baseline");
        return;
    }
    pending.Seed.push_back(acRequest.Data);
}

void GameServer::HandleCampaignReady(ConnectionId_t aConnectionId, const RequestCampaignReady& acRequest)
{
    if (m_requestStop || m_rejectedCampaigns.contains(aConnectionId))
        return;
    auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);
    const auto found = m_pendingCampaigns.find(aConnectionId);
    if (!pPlayer || pPlayer->IsCampaignReady() || found == m_pendingCampaigns.end() || found->second.Nonce != acRequest.Nonce)
        return;
    auto& pending = found.value();
    auto& campaign = m_pWorld->GetCampaign();
    if (acRequest.SeedCount > 4095 || acRequest.Revision != pending.Revision)
    {
        RejectCampaign(aConnectionId, "Campaign checkpoint revision or seed count is invalid");
        return;
    }
    if (!campaign.IsBootstrapped())
    {
        if (!pending.SeedOwner)
            return; // The baseline owner will wake pending peers after its atomic import.
        if (pending.Seed.size() != acRequest.SeedCount || !campaign.BootstrapQuests(pending.Seed, m_pWorld->GetQuestService().AllowsMisc()))
        {
            RejectCampaign(aConnectionId, "Cannot initialize the shared quest baseline: " + campaign.Error());
            return;
        }
        m_seedOwner.reset();
        Vector<ConnectionId_t> waiting;
        for (const auto& entry : m_pendingCampaigns)
            waiting.push_back(entry.first);
        for (const auto connection : waiting)
            BeginCampaignSync(m_pWorld->GetPlayerManager().GetByConnectionId(connection));
        return;
    }
    if (acRequest.SeedCount != 0)
    {
        RejectCampaign(aConnectionId, "The shared campaign baseline is already committed");
        return;
    }
    if (pending.Revision != campaign.QuestRevision())
    {
        BeginCampaignSync(pPlayer);
        return;
    }
    const auto joined = pending;
    m_pendingCampaigns.erase(aConnectionId);
    pPlayer->SetCampaignReady();
    m_pWorld->GetQuestService().FillPlayerJournal(*pPlayer);
    NotifyCampaignCheckpoint accepted;
    accepted.State = NotifyCampaignCheckpoint::Accepted;
    accepted.WorldId = campaign.WorldId();
    accepted.Nonce = joined.Nonce;
    accepted.Revision = campaign.QuestRevision();
    accepted.Count = static_cast<uint32_t>(campaign.Quests().size());
    pPlayer->Send(accepted);
    for (Player* pOther : m_pWorld->GetPlayerManager())
    {
        if (pOther == pPlayer || !pOther->IsCampaignReady())
            continue;
        NotifyPlayerJoined notify;
        notify.PlayerId = pOther->GetId();
        notify.Username = pOther->GetUsername();
        notify.WorldSpaceId = pOther->GetCellComponent().WorldSpaceId;
        notify.CellId = pOther->GetCellComponent().Cell;
        notify.Level = pOther->GetLevel();
        pPlayer->Send(notify);
    }
    m_pWorld->GetDispatcher().trigger(PlayerJoinEvent(pPlayer, joined.WorldSpace, joined.Cell, joined.Time));
}

void GameServer::CampaignDisconnected(ConnectionId_t aConnectionId)
{
    m_pendingCampaigns.erase(aConnectionId);
    if (m_seedOwner == aConnectionId)
        m_seedOwner.reset();
    if (m_requestStop || m_pWorld->GetCampaign().IsBootstrapped() || m_seedOwner)
        return;
    for (const auto& pending : m_pendingCampaigns)
    {
        BeginCampaignSync(m_pWorld->GetPlayerManager().GetByConnectionId(pending.first));
        if (m_seedOwner)
            break;
    }
}

void GameServer::ExpireCampaignSync()
{
    Vector<ConnectionId_t> expired;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& entry : m_pendingCampaigns)
        if (entry.second.Deadline <= now)
            expired.push_back(entry.first);
    for (const auto connection : expired)
        RejectCampaign(connection, "Shared campaign admission timed out. Load a matching save and reconnect");
}
