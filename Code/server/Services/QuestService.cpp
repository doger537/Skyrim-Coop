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

void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    auto& campaign = m_world.GetCampaign();
    const auto result = campaign.UpdateQuest(message.Id, message.Epoch, message.Status, message.Stage, message.ClientQuestType, AllowsMisc());
    using Result = Campaign::State::QuestResult;
    if (result == Result::Ignored)
        return;
    if (result == Result::StorageError)
    {
        GameServer::Get()->StopForWorldError(campaign.Error());
        return;
    }
    if (result == Result::Invalid || result == Result::Conflict)
    {
        GameServer::Get()->RejectCampaign(acMessage.pPlayer->GetConnectionId(), campaign.Error() + ". Load a matching campaign save before reconnecting");
        return;
    }
    const auto* saved = campaign.FindQuest(message.Id);
    if (!saved)
        return;
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
        GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
    }
    // The sender already applied its local event. Acknowledging must not rerun its fragment.
    notify.AcknowledgementOnly = true;
    acMessage.pPlayer->Send(notify);
}
