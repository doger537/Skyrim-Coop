#include <World.h>
#include <GameServer.h>
#include <Services/QuestGrantService.h>
#include <Services/WorldCheckpointService.h>
#include <Messages/RequestQuestGrants.h>
#include <Messages/NotifyQuestGrant.h>

QuestGrantService::QuestGrantService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
    m_queryConnection = aDispatcher.sink<PacketEvent<RequestQuestGrants>>().connect<&QuestGrantService::OnQuery>(this);
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
