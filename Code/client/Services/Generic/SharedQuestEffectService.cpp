#include <Services/SharedQuestEffectService.h>
#include <Services/QuestGrantService.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>
#include <Services/WorldCheckpointService.h>
#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Messages/NotifySharedQuestEffect.h>
#include <Forms/SavedCharacterIdentity.h>
#include <Forms/TESBoundObject.h>
#include <Forms/TESQuest.h>
#include <PlayerCharacter.h>
#include <Components.h>
#include <World.h>
#include <AI/NativeActorPose.h>
#include <common/AcceleratedQuests.h>
#include <algorithm>
#include <limits>

SharedQuestEffectService* SharedQuestEffectService::s_instance{};
namespace
{
String SourceKey(const NativeQuestGate::Invocation& call) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    const auto byte = [&](uint8_t value) { hash ^= value; hash *= 1099511628211ull; };
    for (const auto* name : {&call.Source.Script, &call.Source.Function}) { for (char c : *name) byte(static_cast<uint8_t>(c)); byte(0); }
    for (uint32_t value : {call.Source.InstructionOffset, call.Count, uint32_t(call.Remove)})
        for (uint32_t shift = 0; shift < 32; shift += 8) byte(static_cast<uint8_t>(value >> shift));
    constexpr char hex[] = "0123456789abcdef";
    String result; for (int shift = 60; shift >= 0; shift -= 4) result += hex[(hash >> shift) & 15]; return result;
}
}
SharedQuestEffectService::SharedQuestEffectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    s_instance = this;
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&SharedQuestEffectService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&SharedQuestEffectService::OnDisconnect>(this);
    m_notifyConnection = aDispatcher.sink<NotifySharedQuestEffect>().connect<&SharedQuestEffectService::OnNotify>(this);
    NativeQuestGate::Policy policy;
    policy.Begin = [](const auto& call) noexcept { return s_instance->Begin(call); };
    policy.Poll = [](uint64_t cookie) noexcept { return s_instance->Poll(cookie); };
    policy.Executed = [](uint64_t cookie) noexcept { s_instance->Executed(cookie); };
    policy.Failed = [](uint64_t cookie) noexcept { s_instance->Failed(cookie); };
    NativeQuestGate::Configure(policy);
}
SharedQuestEffectService::Pending* SharedQuestEffectService::Find(uint64_t aCookie) noexcept
{
    const auto it = std::find_if(m_pending.begin(), m_pending.end(), [&](const auto& value) { return value.Cookie == aCookie; });
    return it == m_pending.end() ? nullptr : &*it;
}
bool SharedQuestEffectService::Count(uint32_t aForm, int64_t& aCount) noexcept
{
    aCount = 0;
    auto* player = PlayerCharacter::Get();
    if (!player || !NativeActorPose::OnRunner()) return false;
    const auto inventory = player->GetInventory([aForm](TESForm& value) { return value.formID == aForm; });
    for (auto item : inventory.Entries)
    {
        item.ExtraWorn = item.ExtraWornLeft = false;
        // This first producer handles base-form quantities only. Do not guess
        // instance identity, quest aliases, enchantments or arbitrary metadata.
        if (item.ContainsExtraData() || item.Count < 0) return false;
        aCount += item.Count;
    }
    return aCount <= std::numeric_limits<int32_t>::max();
}
NativeQuestGate::Decision SharedQuestEffectService::Begin(const NativeQuestGate::Invocation& call) noexcept
{
    using D = NativeQuestGate::Decision;
    if (!AcceleratedQuests::kEnabled || !m_transport.IsConnected() || !NativeActorPose::OnRunner() ||
        !m_world.GetPartyService().IsInParty() || m_world.GetPartyService().GetPartyMembers().size() != 2) return D::PassThrough;
    // Both clients replay shared quest-stage fragments. A host-only dialogue
    // or scene callback has no guaranteed matching guest call and stays outside
    // this producer until a dedicated execution rule exists for that source.
    if (call.Source.Kind != QuestItemScriptOrigin::Source::Quest || call.Source.Script.compare(0, 3, "QF_") != 0 ||
        call.Source.Function.compare(0, 9, "Fragment_") != 0) return D::PassThrough;
    auto* form = TESForm::GetById(call.ItemFormId);
    // Restrict this producer to inventory base forms. Reference-valued AddItem
    // and unsupported functor destinations retain their original engine path.
    if (!form || !Cast<TESBoundObject>(form) || form->IsTemporary() || form->formType == FormType::LeveledItem ||
        form->formType == FormType::Npc || form->formType == FormType::LeveledCharacter) return D::PassThrough;
    GameId quest; uint32_t run{};
    if (!m_world.ctx().at<QuestService>().SharedRun(call.Source.QuestFormId, quest, run)) return D::PassThrough;
    if (!call.Count) return D::Complete;
    const bool guest = !m_world.GetPartyService().IsLeader();
    bool assigned{};
    const auto view = m_world.view<LocalComponent, FormIdComponent>();
    for (const auto entity : view) if (view.get<FormIdComponent>(entity).Id == 0x14) { assigned = true; break; }
    if (!assigned) return D::Reject;
    if (call.Count > 1000000 || m_pending.size() >= AcceleratedQuests::kMaxPending ||
        m_world.ctx().at<WorldCheckpointService>().IsFrozen()) return D::Reject;
    Pending pending;
    pending.Cookie = call.Cookie; pending.Form = call.ItemFormId; pending.Admission = m_transport.GetCampaignNonce();
    pending.Guest = guest; pending.Source = SourceKey(call);
    pending.WorldEpoch = m_transport.GetWorldEpoch(); pending.Deadline = GetTickCount64() + 180000;
    pending.Plan.Quest = quest; pending.Plan.Run = run;
    auto* nativeQuest = Cast<TESQuest>(TESForm::GetById(call.Source.QuestFormId));
    pending.Plan.Stage = nativeQuest ? nativeQuest->currentStage : 0;
    pending.Plan.Event = String("shared:e") + std::to_string(pending.WorldEpoch).c_str() + ":a" +
        std::to_string(pending.Admission).c_str() + ":n" + std::to_string(call.Cookie).c_str() + ":s" + pending.Source;
    if (SavedCharacterIdentity::Read(pending.Plan.Initiator) != SavedCharacterIdentity::Result::Found) return D::Reject;
    Campaign::QuestGrantItem item; item.Role = Campaign::QuestGrantItem::Shared;
    if (!m_world.GetModSystem().GetServerModId(call.ItemFormId, item.Item.BaseId) || (!guest && !Count(call.ItemFormId, pending.Before))) return D::Reject;
    item.Item.Count = call.Remove ? -static_cast<int32_t>(call.Count) : static_cast<int32_t>(call.Count);
    pending.Plan.Items.push_back(item);
    m_pending.push_back(std::move(pending));
    if (!guest) Send(m_pending.back(), RequestSharedQuestEffect::Reserve);
    return D::Wait;
}
void SharedQuestEffectService::Send(Pending& aPending, uint8_t aState) noexcept
{
    RequestSharedQuestEffect request; request.Nonce = aPending.Cookie; request.Admission = aPending.Admission; request.State = aState;
    if (aState != RequestSharedQuestEffect::Cancel && !Campaign::Encode(aPending.Plan, request.Payload)) { aPending.Failed = true; return; }
    m_transport.Send(request); aPending.Retry = GetTickCount64() + 1000;
}
NativeQuestGate::Decision SharedQuestEffectService::Poll(uint64_t aCookie) noexcept
{
    using D = NativeQuestGate::Decision;
    auto* pending = Find(aCookie);
    if (!pending || pending->Failed || !m_transport.IsConnected() || pending->Guest == m_world.GetPartyService().IsLeader() ||
        pending->WorldEpoch != m_transport.GetWorldEpoch() || pending->Admission != m_transport.GetCampaignNonce() ||
        (!pending->Delivered && !pending->Empty && GetTickCount64() > pending->Deadline)) return D::Reject;
    if (pending->Empty || pending->Delivered) return D::Complete;
    if (pending->Guest) return D::Wait;
    if (pending->Permitted && !pending->Executed)
    {
        if (!Count(pending->Form, pending->Before)) return D::Reject;
        return D::Execute;
    }
    return D::Wait;
}
void SharedQuestEffectService::Executed(uint64_t aCookie) noexcept
{
    auto* pending = Find(aCookie);
    if (!pending) { Failed(aCookie); return; }
    pending->Executed = true;
    int64_t after{};
    const int32_t wanted = pending->Plan.Items[0].Item.Count;
    if (!Count(pending->Form, after)) { pending->Failed = true; return; }
    const int64_t delta = after - pending->Before;
    if (wanted > 0 ? delta < 0 || delta > wanted : delta > 0 || delta < wanted) { pending->Failed = true; return; }
    pending->Plan.Items[0].Item.Count = static_cast<int32_t>(delta);
    // Native inventory hooks inside the managed original are suppressed;
    // publish only the actual, read-back delta, never the requested amount.
    if (delta) m_world.GetRunner().Trigger(InventoryChangeEvent(0x14, pending->Plan.Items[0].Item));
    if (!m_world.ctx().at<QuestGrantService>().RecordNativeExecution(pending->Plan.Quest, pending->Plan.Run, pending->Plan.Event))
    { pending->Failed = true; return; }
    Send(*pending, RequestSharedQuestEffect::Commit);
}
void SharedQuestEffectService::Failed(uint64_t aCookie) noexcept
{
    if (auto* pending = Find(aCookie)) pending->Failed = true;
    // Native Run must not synchronously close transport while it still holds
    // a functor/VM frame. The update tick reports the interrupted operation.
}
void SharedQuestEffectService::OnNotify(const NotifySharedQuestEffect& message) noexcept
{
    if (!message.IsValid()) return;
    if (message.State == NotifySharedQuestEffect::Committed) m_world.ctx().at<QuestGrantService>().Wake();
    auto* pending = Find(message.Nonce); if (!pending) return;
    if (pending->Guest) return;
    if (pending->Delivered || pending->Empty) return;
    if (pending->Committed && message.State == NotifySharedQuestEffect::Permitted) return;
    if (message.State == NotifySharedQuestEffect::Permitted) pending->Permitted = true;
    else if (message.State == NotifySharedQuestEffect::Rejected) pending->Failed = true;
    else
    {
        if (!pending->Executed || (pending->Revision && pending->Revision != message.Revision)) { pending->Failed = true; return; }
        pending->Revision = message.Revision; pending->Committed = true;
        if (message.State == NotifySharedQuestEffect::Delivered) pending->Delivered = true;
    }
}
void SharedQuestEffectService::OnDisconnect(const DisconnectedEvent&) noexcept { m_pending.clear(); }
void SharedQuestEffectService::OnUpdate(const UpdateEvent&) noexcept
{
    NativeQuestGate::Disposal disposal;
    while (NativeQuestGate::PollDisposal(disposal))
        std::erase_if(m_pending, [&](const auto& value) { return value.Cookie == disposal.Cookie; });
    if (!m_transport.IsConnected()) return;
    if (NativeQuestGate::NeedsReload()) { m_transport.FailCampaign("Shared quest item operation was interrupted or unsupported. Reload the matching paired save"); return; }
    const uint64_t now = GetTickCount64();
    for (auto& pending : m_pending)
    {
        if (pending.Failed || (!pending.Delivered && !pending.Empty && now > pending.Deadline))
        { m_transport.FailCampaign("Shared quest item delivery failed. Reload the matching paired save"); return; }
        if (!pending.Guest && now >= pending.Retry && !pending.Empty && !pending.Delivered)
            Send(pending, pending.Executed ? RequestSharedQuestEffect::Commit : RequestSharedQuestEffect::Reserve);
    }
}
bool SharedQuestEffectService::GuestReady(const Campaign::QuestGrantPlan& plan) noexcept
{
    if (!plan.Shared() || plan.Event.size() < 18 || plan.Event.compare(plan.Event.size() - 18, 2, ":s") != 0 || plan.Items.size() != 1) return false;
    const String source = plan.Event.substr(plan.Event.size() - 16);
    return std::any_of(m_pending.begin(), m_pending.end(), [&](const auto& value)
    {
        return value.Guest && !value.Delivered && !value.Failed && value.Source == source && value.Plan.Quest == plan.Quest && value.Plan.Run == plan.Run &&
            value.Plan.Items[0].Item.BaseId == plan.Items[0].Item.BaseId &&
            (plan.Items[0].Item.Count == 0 || (value.Plan.Items[0].Item.Count > 0 ? plan.Items[0].Item.Count > 0 && plan.Items[0].Item.Count <= value.Plan.Items[0].Item.Count :
                plan.Items[0].Item.Count < 0 && plan.Items[0].Item.Count >= value.Plan.Items[0].Item.Count));
    });
}
void SharedQuestEffectService::GuestApplied(const Campaign::QuestGrantPlan& plan) noexcept
{
    const String source = plan.Event.substr(plan.Event.size() - 16);
    for (auto& value : m_pending)
        if (value.Guest && !value.Delivered && !value.Failed && value.Source == source && value.Plan.Quest == plan.Quest && value.Plan.Run == plan.Run &&
            value.Plan.Items[0].Item.BaseId == plan.Items[0].Item.BaseId) { value.Delivered = true; return; }
}
