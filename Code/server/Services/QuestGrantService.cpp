#include <World.h>
#include <GameServer.h>
#include <Services/QuestGrantService.h>
#include <Services/WorldCheckpointService.h>
#include <Messages/RequestQuestGrants.h>
#include <Messages/NotifyQuestGrant.h>
#include <Messages/RequestSharedQuestEffect.h>
#include <Messages/NotifySharedQuestEffect.h>
#include <Events/UpdateEvent.h>
#include <algorithm>
#include <common/AcceleratedQuests.h>

QuestGrantService::QuestGrantService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
    m_queryConnection = aDispatcher.sink<PacketEvent<RequestQuestGrants>>().connect<&QuestGrantService::OnQuery>(this);
    m_effectConnection = aDispatcher.sink<PacketEvent<RequestSharedQuestEffect>>().connect<&QuestGrantService::OnEffect>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&QuestGrantService::OnUpdate>(this);
}
void QuestGrantService::OnQuery(const PacketEvent<RequestQuestGrants>& acPacket) noexcept
{
    const auto& request = acPacket.Packet;
    auto& player = *acPacket.pPlayer;
    if (!request.IsValid() || !player.IsCampaignReady() || m_world.ctx().at<WorldCheckpointService>().IsFrozen() ||
        !player.AcceptQuestGrantQuery(GameServer::Get()->GetTick())) return;
    auto& campaign = m_world.GetCampaign();
    if (request.AfterRevision > campaign.Revision())
    {
        GameServer::Get()->RejectCampaign(player.GetConnectionId(), "The saved grant cursor is newer than this world. Restore the matching paired checkpoint");
        return;
    }
    Vector<Campaign::QuestGrantReceipt> receipts;
    // Recipient comes only from the authenticated saved character, not a packet field.
    if (!campaign.ReadQuestGrants(player.GetPersistentCharacterId(), request.AfterRevision, receipts))
    {
        GameServer::Get()->StopForWorldError(campaign.Error()); return;
    }
    if (request.CursorAvailable)
        for (auto& effect : m_effects)
            if (effect.Revision && request.AfterRevision >= effect.Revision)
                for (size_t i = 0; i < 2; ++i)
                    if (effect.Members[i] == player.GetPersistentCharacterId() && effect.ParticipantIds[i] == player.GetId() &&
                        effect.Admissions[i] == player.GetCampaignNonce()) effect.Applied[i] = true;
    NotifyQuestGrant reply;
    reply.Nonce = request.Nonce; reply.AfterRevision = request.AfterRevision; reply.HeadRevision = campaign.Revision();
    if (!receipts.empty())
    {
        if (!request.CursorAvailable)
        {
            GameServer::Get()->RejectCampaign(player.GetConnectionId(), "This character needs the matching TrueCoopWorldState plugin to receive saved quest grants"); return;
        }
        const auto& receipt = receipts.front();
        reply.ReceiptRevision = receipt.Revision;
        if (!Campaign::Encode(receipt, reply.Payload) || !reply.IsValid())
        {
            GameServer::Get()->StopForWorldError("Quest grant receipt exceeds the bounded delivery format"); return;
        }
    }
    // No delivery ACK deletes the immutable ledger. Retrying the same saved
    // cursor returns the same first receipt, including after a server restart.
    player.Send(reply);
}
void QuestGrantService::OnEffect(const PacketEvent<RequestSharedQuestEffect>& acPacket) noexcept
{
    const auto& request = acPacket.Packet;
    auto& player = *acPacket.pPlayer;
    if (!AcceleratedQuests::kEnabled || !request.IsValid() || !player.IsCampaignReady() || request.Admission != player.GetCampaignNonce() ||
        m_world.ctx().at<WorldCheckpointService>().IsFrozen()) return;
    NotifySharedQuestEffect reply; reply.Nonce = request.Nonce; reply.State = NotifySharedQuestEffect::Rejected;
    auto reject = [&]() { player.Send(reply); };
    auto* party = m_world.GetPartyService().GetPlayerParty(&player);
    if (!party || party->Members.size() != 2 || party->LeaderPlayerId != player.GetId() ||
        !party->Members[0]->IsCampaignReady() || !party->Members[1]->IsCampaignReady()) { reject(); return; }
    auto found = std::find_if(m_effects.begin(), m_effects.end(), [&](const auto& value)
        { return value.Host == player.GetId() && value.Nonce == request.Nonce && value.Admission == request.Admission; });
    if (request.State == RequestSharedQuestEffect::Cancel)
    {
        if (found != m_effects.end() && !found->Revision) m_effects.erase(found);
        return;
    }
    Campaign::QuestGrantPlan plan;
    const auto expectedEvent = String("shared:e") + std::to_string(m_world.GetCampaign().WorldEpoch()).c_str() + ":a" +
        std::to_string(request.Admission).c_str() + ":n" + std::to_string(request.Nonce).c_str();
    if (!Campaign::Decode(request.Payload, plan) || !plan.Shared() || plan.Items.size() != 1 ||
        plan.Event.size() != expectedEvent.size() + 18 || plan.Event.compare(0, expectedEvent.size(), expectedEvent) != 0 ||
        plan.Event.compare(expectedEvent.size(), 2, ":s") != 0 ||
        !std::all_of(plan.Event.end() - 16, plan.Event.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        plan.Initiator != player.GetPersistentCharacterId() || !m_world.GetCampaign().ValidForm(plan.Quest) ||
        !m_world.GetCampaign().ValidForm(plan.Items[0].Item.BaseId)) { reject(); return; }
    if (request.State == RequestSharedQuestEffect::Reserve)
    {
        if (!plan.Items[0].Item.Count) { reject(); return; }
        if (found != m_effects.end())
        {
            if (found->Plan != plan) { reject(); return; }
        }
        else
        {
            Campaign::QuestRunParticipants run;
            run.Initiator = player.GetPersistentCharacterId();
            run.Players = {party->Members[0]->GetPersistentCharacterId(), party->Members[1]->GetPersistentCharacterId()};
            if (run.Players[1].Parts < run.Players[0].Parts) std::swap(run.Players[0], run.Players[1]);
            const auto* quest = m_world.GetCampaign().FindQuest(plan.Quest);
            // The native start event can still be queued on the host runner.
            // Retrying the same unexecuted reservation does not grant anything.
            if (!quest) return;
            if (m_effects.size() >= AcceleratedQuests::kMaxPending || quest->Data.Epoch != plan.Run || !quest->Data.Started ||
                !m_world.GetCampaign().BindSharedQuestRun(plan.Quest, plan.Run, run)) { reject(); return; }
            Effect effect; effect.Plan = plan; effect.Members = run.Players; effect.Host = player.GetId(); effect.Nonce = request.Nonce;
            for (size_t i = 0; i < 2; ++i)
                for (const auto* member : party->Members)
                    if (member->GetPersistentCharacterId() == effect.Members[i])
                    { effect.ParticipantIds[i] = member->GetId(); effect.Admissions[i] = member->GetCampaignNonce(); }
            effect.Admission = request.Admission; effect.WorldEpoch = m_world.GetCampaign().WorldEpoch(); effect.Deadline = GameServer::Get()->GetTick() + 180000;
            m_effects.push_back(std::move(effect));
        }
        reply.State = NotifySharedQuestEffect::Permitted; player.Send(reply); return;
    }
    if (found == m_effects.end()) { reject(); return; }
    auto actual = plan; actual.Items[0].Item.Count = found->Plan.Items[0].Item.Count;
    const int32_t wanted = found->Plan.Items[0].Item.Count, count = plan.Items[0].Item.Count;
    if (actual != found->Plan || (wanted > 0 ? count < 0 || count > wanted : count > 0 || count < wanted)) { reject(); return; }
    Campaign::QuestGrantReceipt receipt;
    const auto result = m_world.GetCampaign().CommitQuestGrant(plan, receipt);
    if (result != Campaign::State::GrantResult::Committed && result != Campaign::State::GrantResult::Duplicate)
    {
        if (result == Campaign::State::GrantResult::StorageError) GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error());
        else reject();
        return;
    }
    found->Revision = receipt.Revision;
    for (auto* member : party->Members)
    {
        NotifySharedQuestEffect wake; wake.State = NotifySharedQuestEffect::Committed;
        wake.Nonce = member == &player ? request.Nonce : 0; wake.Revision = receipt.Revision; member->Send(wake);
    }
}
void QuestGrantService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t now = GameServer::Get()->GetTick();
    for (auto it = m_effects.begin(); it != m_effects.end();)
    {
        auto* host = m_world.GetPlayerManager().GetById(it->Host);
        const auto* party = host ? m_world.GetPartyService().GetPlayerParty(host) : nullptr;
        bool sameSessions = true;
        for (size_t i = 0; i < 2; ++i)
        {
            const auto* member = m_world.GetPlayerManager().GetById(it->ParticipantIds[i]);
            if (!member || !member->IsCampaignReady() || member->GetCampaignNonce() != it->Admissions[i] || member->GetPersistentCharacterId() != it->Members[i])
                sameSessions = false;
        }
        const bool complete = sameSessions && it->Revision && it->Applied[0] && it->Applied[1];
        if (!host || !host->IsCampaignReady() || host->GetCampaignNonce() != it->Admission || it->WorldEpoch != m_world.GetCampaign().WorldEpoch())
            it = m_effects.erase(it);
        else if (complete || !sameSessions || now >= it->Deadline || !party || party->LeaderPlayerId != it->Host || party->Members.size() != 2 ||
            !party->Members[0]->IsCampaignReady() || !party->Members[1]->IsCampaignReady() ||
            !((party->Members[0]->GetPersistentCharacterId() == it->Members[0] && party->Members[1]->GetPersistentCharacterId() == it->Members[1]) ||
              (party->Members[0]->GetPersistentCharacterId() == it->Members[1] && party->Members[1]->GetPersistentCharacterId() == it->Members[0])))
        {
            NotifySharedQuestEffect reply; reply.Nonce = it->Nonce; reply.Revision = complete ? it->Revision : 0;
            reply.State = complete ? NotifySharedQuestEffect::Delivered : NotifySharedQuestEffect::Rejected; host->Send(reply);
            it = m_effects.erase(it);
        }
        else ++it;
    }
}
