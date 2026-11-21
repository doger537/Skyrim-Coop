#include <GameServer.h>
#include <Components.h>
#include <World.h>
#include <Services/QuestService.h>
#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Setting.h>

namespace
{
Console::Setting bEnableMiscQuestSync{"Gameplay:bEnableMiscQuestSync", "(Experimental) Syncs miscellaneous quests when possible", false};
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);
}

bool QuestService::AllowsMisc() const noexcept
{
    return bEnableMiscQuestSync;
}

void QuestService::FillPlayerJournal(Player& aPlayer) const noexcept
{
    auto& entries = aPlayer.GetQuestLogComponent().QuestContent.Entries;
    entries.clear();
    for (const auto& saved : m_world.GetCampaign().Quests())
    {
        if (saved.Data.Stopped)
            continue;
        uint16_t last = 0;
        for (const auto& event : saved.Data.Events)
            if (event.Status != RequestQuestUpdate::Stopped)
                last = event.Stage;
        entries.push_back({saved.Data.Id, last});
    }
}

bool QuestService::Commit(Player& aInitiator, const RequestQuestUpdate& message, const Campaign::QuestRunParticipants* apParticipants) noexcept
{
    auto& campaign = m_world.GetCampaign();
    const auto result = campaign.UpdateQuest(message.Id, message.Epoch, message.Status, message.Stage, message.ClientQuestType, AllowsMisc(),
        apParticipants);
    using Result = Campaign::State::QuestResult;
    if (result == Result::Ignored)
        return true;
    if (result == Result::StorageError)
    {
        GameServer::Get()->StopForWorldError(campaign.Error());
        return false;
    }
    if (result == Result::Invalid || result == Result::Conflict)
    {
        GameServer::Get()->RejectCampaign(aInitiator.GetConnectionId(), fmt::format("Quest {:08X}:{:08X} stage {} epoch {}. {}. Load a matching paired save",
            message.Id.ModId, message.Id.BaseId, message.Stage, message.Epoch, campaign.Error()).c_str(), true);
        return false;
    }
    const auto* saved = campaign.FindQuest(message.Id);
    if (!saved)
        return false;
    NotifyQuestUpdate notify;
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    notify.Status = message.Status;
    notify.ClientQuestType = message.ClientQuestType;
    notify.Epoch = saved->Data.Epoch;
    notify.Revision = saved->Revision;
    notify.RunStopped = saved->Data.Stopped;
    if (result == Result::Applied)
    {
        for (Player* pPlayer : m_world.GetPlayerManager())
            if (pPlayer->IsCampaignReady())
                FillPlayerJournal(*pPlayer);
        GameServer::Get()->SendToPlayers(notify, &aInitiator);
    }
    // The sender already applied its local event. Acknowledging must not rerun its fragment.
    notify.AcknowledgementOnly = true;
    aInitiator.Send(notify);
    return true;
}

void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acPacket) noexcept
{
    auto& initiator = *acPacket.pPlayer;
    const auto& change = acPacket.Packet;
    if (!change.IsValid())
    {
        GameServer::Get()->RejectCampaign(initiator.GetConnectionId(), "Malformed or truncated quest update");
        return;
    }
    auto* party = m_world.GetPartyService().GetPlayerParty(&initiator);
    if (party && !m_world.GetPartyService().IsPlayerLeader(&initiator))
    {
        const auto preview = m_world.GetCampaign().PreviewQuest(change.Id, change.Epoch, change.Status, change.Stage, change.ClientQuestType, AllowsMisc());
        if (preview == Campaign::State::QuestResult::Duplicate || preview == Campaign::State::QuestResult::Ignored)
        { Commit(initiator, change, nullptr); return; }
        GameServer::Get()->RejectCampaign(initiator.GetConnectionId(),
            fmt::format("Quest {:08X}:{:08X} stage {} epoch {}. Shared quests are advanced by the host. Load the matching paired save",
                        change.Id.ModId, change.Id.BaseId, change.Stage, change.Epoch).c_str(), true);
        return;
    }
    Campaign::QuestRunParticipants participants;
    participants.Initiator = initiator.GetPersistentCharacterId();
    if (party && party->Members.size() == 2)
        for (size_t i = 0; i < 2; ++i) participants.Players[i] = party->Members[i]->GetPersistentCharacterId();
    Commit(initiator, change, participants.IsValid() ? &participants : nullptr);
}
