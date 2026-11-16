#include "CoopSleepService.h"
#include <Components.h>
#include <World.h>
#include <GameServer.h>
#include <Events/UpdateEvent.h>
#include <Messages/RequestCoopSleep.h>
#include <Messages/NotifyCoopSleep.h>

CoopSleepService::CoopSleepService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_requestConnection = aDispatcher.sink<PacketEvent<RequestCoopSleep>>().connect<&CoopSleepService::OnRequest>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CoopSleepService::OnUpdate>(this);
}

bool CoopSleepService::Refresh(uint32_t aPartyId, CoopSleepVote& aVote) noexcept
{
    uint32_t host{};
    uint32_t guest{};
    const auto* party = m_world.GetPartyService().GetById(aPartyId);
    if (party && party->Members.size() == 2 && party->Members[0]->IsCampaignReady() && party->Members[1]->IsCampaignReady())
    {
        host = party->LeaderPlayerId;
        for (const auto* player : party->Members)
            if (player->GetId() != host)
                guest = player->GetId();
    }
    const auto old = aVote;
    if (!aVote.Configure(m_world.GetCampaign().WorldEpoch(), host, guest))
        return false;
    // Notify the old membership too; a leaving player must not keep a ready UI.
    if (old.Round() != 0 && (old.GetPhase() == CoopSleepVote::Phase::Waiting || old.GetPhase() == CoopSleepVote::Phase::Preparing))
    {
        auto cancelled = old;
        cancelled.Abort();
        Broadcast(cancelled);
    }
    return true;
}

void CoopSleepService::Broadcast(const CoopSleepVote& acVote) noexcept
{
    if (acVote.Round() == 0)
        return;
    NotifyCoopSleep message;
    message.Phase = acVote.GetPhase();
    message.Members = acVote.Members();
    message.Round = acVote.Round();
    message.Deadline = acVote.Deadline();
    message.Hours = acVote.Hours();
    message.Votes = acVote.Votes();
    message.Tick = GameServer::Get()->GetTick();
    message.Time = m_world.GetCalendarService().GetModel();
    for (Player* player : m_world.GetPlayerManager())
        if (player->IsCampaignReady() && (player->GetId() == message.Members[0] || player->GetId() == message.Members[1]))
            player->Send(message);
}

void CoopSleepService::OnRequest(const PacketEvent<RequestCoopSleep>& acPacket) noexcept
{
    const auto& message = acPacket.Packet;
    Player* player = acPacket.pPlayer;
    if (!message.IsValid() || !player || !player->IsCampaignReady() || !player->GetParty().JoinedPartyId)
        return;
    const uint32_t partyId = *player->GetParty().JoinedPartyId;
    auto& vote = m_votes[partyId];
    Refresh(partyId, vote);
    vote.SetRoundFloor(m_lastRound);
    const auto before = vote.GetPhase();
    const bool changed = vote.Submit(player->GetId(), message.Sequence, message.Round, message.Action,
                                     message.Hours, message.Tick, GameServer::Get()->GetTick());
    m_lastRound = std::max(m_lastRound, vote.Round());
    if (!changed && before == vote.GetPhase())
        return;
    if (changed && vote.GetPhase() == CoopSleepVote::Phase::Committed && !m_world.GetCalendarService().AdvanceHours(vote.Hours()))
        vote.Abort();
    Broadcast(vote);
}

void CoopSleepService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t now = GameServer::Get()->GetTick();
    for (auto it = m_votes.begin(); it != m_votes.end();)
    {
        auto& vote = it.value();
        Refresh(it->first, vote);
        if (vote.Expire(now))
            Broadcast(vote);
        if (!m_world.GetPartyService().GetById(it->first))
            it = m_votes.erase(it);
        else
            ++it;
    }
}
