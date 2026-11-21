#include <Services/QuestGrantService.h>
#include <Services/TransportService.h>
#include <Services/WorldCheckpointService.h>
#include <Services/SharedQuestEffectService.h>
#include <World.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/SavedCharacterIdentity.h>
#include <Forms/TESQuest.h>
#include <Forms/TESBoundObject.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <Forms/EffectSetting.h>
#include <NativeGrantCursor.h>
#include <NativeGrantDelivery.h>
#include <NativeQuestGate.h>
#include <PlayerCharacter.h>
#include <Games/TES.h>
#include <common/QuestGrantCursorIds.h>
#include <algorithm>
#include <limits>

namespace
{
bool Mapped(World& aWorld, GameId aId, bool aOptional = false) noexcept
{
    if (!aId) return aOptional;
    const uint32_t local = aWorld.GetModSystem().GetGameId(aId);
    GameId roundtrip;
    return local && TESForm::GetById(local) && aWorld.GetModSystem().GetServerModId(local, roundtrip) && roundtrip == aId;
}
int64_t Count(PlayerCharacter* apPlayer, uint32_t aForm, const Inventory::Entry& acItem) noexcept
{
    // Filter before extracting extra data. Count only the same item variant;
    // an unrelated enchanted copy is not proof of this receipt's application.
    const auto inventory = apPlayer->GetInventory([aForm](TESForm& aFormValue) { return aFormValue.formID == aForm; });
    int64_t count{};
    for (auto item : inventory.Entries)
    {
        item.ExtraWorn = item.ExtraWornLeft = false;
        if (item.BaseId == acItem.BaseId && item.IsExtraDataEquals(acItem)) count += item.Count;
    }
    return count;
}
}
QuestGrantService::QuestGrantService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestGrantService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&QuestGrantService::OnDisconnected>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&QuestGrantService::OnUpdate>(this);
    m_receiptConnection = aDispatcher.sink<NotifyQuestGrant>().connect<&QuestGrantService::OnReceipt>(this);
}
bool QuestGrantService::RecordNativeExecution(GameId aQuest, uint32_t aRun, const String& acEvent) noexcept
{
    if (!m_ready || !m_available || !NativeQuestGate::HasPending() || !aQuest || !aRun || acEvent.empty() || acEvent.size() > 96 || m_executed.size() >= 128) return false;
    if (std::any_of(m_executed.begin(), m_executed.end(), [&](const auto& value) { return value.Quest == aQuest && value.Run == aRun && value.Event == acEvent; })) return false;
    m_executed.push_back({aQuest, aRun, acEvent});
    return true;
}
void QuestGrantService::OnConnected(const ConnectedEvent&) noexcept
{
    const uint64_t generation = m_transport.GetConnectionGeneration();
    m_world.GetRunner().QueueGameplay([this, generation]()
    {
        if (!m_transport.IsConnected() || generation != m_transport.GetConnectionGeneration()) return;
        if (m_nativeLoadGeneration != NativeGrantDelivery::Generation()) m_executed.clear();
        m_nativeLoadGeneration = NativeGrantDelivery::Generation();
        m_generation = generation;
        m_worldId = m_transport.GetCampaignWorldId();
        if (SavedCharacterIdentity::Read(m_character) != SavedCharacterIdentity::Result::Found) { Fail("Cannot read the saved grant recipient"); return; }
        const auto* plugin = ModManager::Get()->GetByName(QuestGrantCursorIds::kPlugin);
        m_available = plugin && plugin->IsLoaded();
        m_cursor = 0;
        if (m_available)
        {
            String world;
            if (!NativeGrantCursor::Bind(m_worldId) || NativeGrantCursor::Read(world, m_cursor) != NativeGrantCursor::Result::Bound || world != m_worldId)
            { Fail("Quest grant cursor is invalid or belongs to another world. Load a matching paired checkpoint"); return; }
        }
        m_ready = true; m_nextPoll = 0; m_waiting = false;
    });
}
void QuestGrantService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    if (m_pending) NativeGrantDelivery::Abort(m_nativeGeneration, m_mutated);
    m_ready = m_pending = m_waiting = m_mutated = m_skipNative = false;
    m_receipt.reset(); m_delivery.Entries.clear(); m_item = 0;
    // Keep native execution markers across a socket reconnect. Only loading an
    // ESS proves those unsaved effects have been replaced and clears the map.
}
void QuestGrantService::Fail(const char* apReason) noexcept
{
    OnDisconnected(DisconnectedEvent{});
    m_transport.FailCampaign(apReason);
}
void QuestGrantService::OnReceipt(const NotifyQuestGrant& acMessage) noexcept
{
    const uint64_t generation = m_transport.GetConnectionGeneration();
    m_world.GetRunner().QueueGameplay([this, message = acMessage, generation]()
    {
        if (m_ready && generation == m_generation && generation == m_transport.GetConnectionGeneration() && m_transport.IsConnected()) Apply(message);
    });
}
bool QuestGrantService::Preflight(const Campaign::QuestGrantReceipt& acReceipt) noexcept
{
    const auto& plan = acReceipt.Plan;
    if (!plan.IsValid() || !acReceipt.Participants.IsValid() || plan.Initiator != acReceipt.Participants.Initiator ||
        (m_character != acReceipt.Participants.Players[0] && m_character != acReceipt.Participants.Players[1]) ||
        !Mapped(m_world, plan.Quest) || !Cast<TESQuest>(TESForm::GetById(m_world.GetModSystem().GetGameId(plan.Quest)))) return false;
    m_skipNative = std::any_of(m_executed.begin(), m_executed.end(), [&](const auto& value)
        { return value.Quest == plan.Quest && value.Run == plan.Run && value.Event == plan.Event; });
    if (m_skipNative && m_character != plan.Initiator) return false;
    // An old host save cannot reconstruct proof that its original fragment
    // ran. Recover through the paired world checkpoint, not another AddItem.
    if (plan.Shared() && m_character == plan.Initiator && !m_skipNative) return false;
    m_delivery = acReceipt.For(m_character);
    for (const auto& item : m_delivery.Entries)
    {
        // A quest alias/reference cannot be reconstructed by adding a base form.
        // Its initiator must have the matching original native execution.
        if (item.IsQuestItem && !m_skipNative) return false;
        if (item.ExtraCharge < 0 || item.ExtraHealth < 0 || item.ExtraSoulLevel < 0 || item.ExtraSoulLevel > 5 || item.ExtraPoisonCount > 1000000) return false;
        if (!Mapped(m_world, item.BaseId) || !Mapped(m_world, item.ExtraEnchantId, true) || !Mapped(m_world, item.ExtraPoisonId, true)) return false;
        auto* form = TESForm::GetById(m_world.GetModSystem().GetGameId(item.BaseId));
        if (!Cast<TESBoundObject>(form) || form->formType == FormType::LeveledItem) return false;
        if (item.ExtraEnchantId && !Cast<EnchantmentItem>(TESForm::GetById(m_world.GetModSystem().GetGameId(item.ExtraEnchantId)))) return false;
        if (item.ExtraPoisonId && !Cast<AlchemyItem>(TESForm::GetById(m_world.GetModSystem().GetGameId(item.ExtraPoisonId)))) return false;
        for (const auto& effect : item.EnchantData.Effects)
            if (!Mapped(m_world, effect.EffectId) || !Cast<EffectSetting>(TESForm::GetById(m_world.GetModSystem().GetGameId(effect.EffectId)))) return false;
    }
    return true;
}
void QuestGrantService::Apply(const NotifyQuestGrant& acMessage) noexcept
{
    if (m_world.ctx().at<WorldCheckpointService>().IsFrozen()) { m_waiting = false; return; }
    if (!m_waiting || acMessage.Nonce != m_query.Nonce) return; // stale retry response
    if (!acMessage.IsValid() || m_receipt || acMessage.AfterRevision != m_cursor || acMessage.AfterRevision != m_query.AfterRevision)
    { Fail("Invalid quest grant inbox boundary"); return; }
    m_waiting = false;
    if (!m_available)
    {
        if (acMessage.ReceiptRevision) Fail("Quest grant delivery requires TrueCoopWorldState.esp");
        else m_nextPoll = GetTickCount64() + 5000;
        return;
    }
    Campaign::QuestGrantReceipt receipt;
    if (acMessage.ReceiptRevision && (!Campaign::Decode(acMessage.Payload, receipt) || !Preflight(receipt)))
    { Fail("Quest grant cannot be safely applied: missing content, native custody or recipient proof"); return; }
    if (!NativeGrantDelivery::Begin(m_nativeGeneration)) { Fail("A previous interrupted quest grant requires reloading a matching checkpoint"); return; }
    m_pending = true; m_mutated = false;
    if (!acMessage.ReceiptRevision)
    {
        m_mutated = acMessage.HeadRevision != m_cursor;
        if (!NativeGrantCursor::Advance(m_worldId, m_cursor, acMessage.HeadRevision)) { Fail("Cannot save the quest grant inbox cursor"); return; }
        m_cursor = acMessage.HeadRevision;
        NativeGrantDelivery::Complete(m_nativeGeneration); m_pending = m_mutated = false;
        m_nextPoll = GetTickCount64() + 2000;
        return;
    }
    receipt.Revision = acMessage.ReceiptRevision;
    m_receipt = std::move(receipt); m_item = 0; m_deadline = GetTickCount64() + (m_receipt->Plan.Shared() ? 180000 : 30000);
}
void QuestGrantService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_ready || !m_transport.IsConnected()) return;
    if (m_generation != m_transport.GetConnectionGeneration() || m_nativeLoadGeneration != NativeGrantDelivery::Generation())
    { OnDisconnected(DisconnectedEvent{}); return; }
    if (m_world.ctx().at<WorldCheckpointService>().IsFrozen()) return;
    const uint64_t now = GetTickCount64();
    if (m_receipt)
    {
        CharacterIdentity character;
        String world;
        uint64_t cursor{};
        auto* player = PlayerCharacter::Get();
        if (!player || SavedCharacterIdentity::Read(character) != SavedCharacterIdentity::Result::Found || character != m_character ||
            NativeGrantCursor::Read(world, cursor) != NativeGrantCursor::Result::Bound || world != m_worldId || cursor != m_cursor || now > m_deadline)
        { Fail("Quest grant application lost its saved character/cursor or timed out. Reload a matching checkpoint"); return; }
        if (!m_skipNative && m_item < m_delivery.Entries.size())
        {
            if (m_receipt->Plan.Shared() && !m_world.ctx().at<SharedQuestEffectService>().GuestReady(m_receipt->Plan)) return;
            const auto& item = m_delivery.Entries[m_item];
            if (!item.Count) { ++m_item; return; } // matched native no-op, still receipt/cursor fenced
            const uint32_t form = m_world.GetModSystem().GetGameId(item.BaseId);
            const int64_t before = Count(player, form, item);
            if (before < 0 || before + int64_t(item.Count) < 0 || before + int64_t(item.Count) > std::numeric_limits<int32_t>::max())
            { Fail("Shared quest effect has insufficient items or would overflow the inventory count"); return; }
            m_mutated = true;
            // Existing owned-player inventory hooks relay the resulting delta.
            // Do not use the remote-actor inventory override here.
            player->AddOrRemoveItem(item);
            if (Count(player, form, item) != before + item.Count)
            { Fail("Native quest grant readback differs. Reload a matching checkpoint before saving"); return; }
            ++m_item;
            return; // At most one native item mutation per admitted runner tick.
        }
        m_mutated = true; // A partially written multiword cursor must not be saved.
        if (!NativeGrantCursor::Advance(m_worldId, m_cursor, m_receipt->Revision))
        { Fail("Cannot commit the saved quest grant receipt. Reload a matching checkpoint"); return; }
        m_cursor = m_receipt->Revision;
        const auto& plan = m_receipt->Plan;
        if (plan.Shared() && !m_skipNative) m_world.ctx().at<SharedQuestEffectService>().GuestApplied(plan);
        std::erase_if(m_executed, [&](const auto& value) { return value.Quest == plan.Quest && value.Run == plan.Run && value.Event == plan.Event; });
        NativeGrantDelivery::Complete(m_nativeGeneration); m_pending = m_mutated = m_skipNative = false;
        m_receipt.reset(); m_delivery.Entries.clear(); m_nextPoll = now + 300;
    }
    if (m_waiting && now < m_deadline) return;
    if (!m_waiting && now < m_nextPoll) return;
    if (!m_waiting)
    {
        if (m_nonce == std::numeric_limits<uint64_t>::max()) { Fail("Quest grant query nonce exhausted"); return; }
        m_query.Nonce = ++m_nonce; m_query.AfterRevision = m_cursor; m_query.CursorAvailable = m_available;
    }
    m_waiting = m_transport.Send(m_query); m_deadline = now + 5000;
    if (!m_waiting) m_nextPoll = now + 2000;
}
