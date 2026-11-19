#include <Services/WorldItemService.h>
#include <Services/WorldCheckpointService.h>
#include <Services/TransportService.h>
#include <World.h>
#include <Components.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Forms/SavedCharacterIdentity.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESBoundObject.h>
#include <AI/NativeActorPose.h>
#include <NativeItemDelivery.h>
#include <NativeGrantDelivery.h>
#include <NativeQuestGate.h>
#include <PlayerCharacter.h>
#include <ScopedReference.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <WorldItemReceipt.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <limits>

WorldItemService* WorldItemService::s_instance{};
namespace
{
bool Supported() noexcept
{
    return VersionDb::Get().GetLoadedVersionString() == "1.7.104.0";
}
bool Plain(ExtraDataList* extra) noexcept
{
    if (!extra)
        return true;
    if (!extra->bitfield)
        return extra->data == nullptr;
    // Reject ALL unrepresented instance data, including ownership/custom names,
    // rather than converting an enchanted, stolen or aliased item to a base form.
    for (size_t i = 0; i < sizeof(extra->bitfield->data); ++i)
    {
        uint8_t allowed{};
        for (auto type : {ExtraDataType::Count, ExtraDataType::Worn, ExtraDataType::WornLeft})
            if (size_t(type) / 8 == i)
                allowed |= uint8_t(1u << (uint32_t(type) % 8));
        if (extra->bitfield->data[i] & ~allowed)
            return false;
    }
    return true;
}
bool SetCount(TESObjectREFR* ref, int32_t count) noexcept
{
    if (!ref || !ref->IsTemporary() || count < 1 || count > 32767 || !NativeActorPose::OnRunner())
        return false;
    using TSet = void(ExtraDataList*, uint16_t);
    POINTER_SKYRIMSE(TSet, set, 11617);
    // MCP: native ExtraCount insertion/removal under its own lock. FF dynamic
    // refs use the same MarkChanged(0) admission as the native ref factory.
    ref->MarkChanged(0);
    set.Get()(ref->GetExtraDataList(), static_cast<uint16_t>(count));
    return ref->GetExtraDataList()->GetCount() == uint32_t(count);
}
PhysicsReplication::Pose Pose(const WorldItemMotion& state) noexcept
{
    PhysicsReplication::Pose pose;
    for (int i = 0; i < 3; ++i)
        pose.Position[i] = state.Position[i];
    pose.Rotation = state.Rotation;
    pose.LinearVelocity = state.LinearVelocity;
    pose.AngularVelocity = state.AngularVelocity;
    return pose;
}
} // namespace
WorldItemService::WorldItemService(World& w, entt::dispatcher& d, TransportService& t) noexcept
    : m_world(w)
    , m_transport(t)
{
    s_instance = this;
    m_connect = d.sink<ConnectedEvent>().connect<&WorldItemService::OnConnected>(this);
    m_disconnect = d.sink<DisconnectedEvent>().connect<&WorldItemService::OnDisconnected>(this);
    m_update = d.sink<UpdateEvent>().connect<&WorldItemService::OnUpdate>(this);
    m_snapshotConnection = d.sink<NotifyWorldItems>().connect<&WorldItemService::OnSnapshot>(this);
    m_transferConnection = d.sink<NotifyWorldItemTransfer>().connect<&WorldItemService::OnTransfer>(this);
    m_motionConnection = d.sink<NotifyWorldItemMotion>().connect<&WorldItemService::OnMotion>(this);
}
void WorldItemService::OnConnected(const ConnectedEvent&) noexcept
{
    if (!Supported())
        return;
    m_generation = m_transport.GetConnectionGeneration();
    m_loadGeneration = NativeItemDelivery::Generation();
    m_scan = m_reconcile = 0;
    m_ready = m_received = m_caughtUp = false;
    m_deadline = GetTickCount64() + 30000;
    m_nextQuery = 0;
    m_session = {};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(m_session.Parts.data()), sizeof(m_session.Parts), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 || !m_session.IsValid())
        Fail("Cannot create a world-item operation identity");
}
void WorldItemService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    if (m_barrier)
        NativeItemDelivery::Abort(m_barrierGeneration, m_mutated || m_intent.has_value());
    m_slots = {};
    m_intent.reset();
    m_snapshot.reset();
    m_inbox.reset();
    m_ready = m_received = m_barrier = m_mutated = m_caughtUp = false;
    m_waiting = m_hostEpoch = 0;
    m_generation = 0;
    m_timeline.Reset(0, 0);
}
void WorldItemService::Fail(const char* reason) noexcept
{
    OnDisconnected(DisconnectedEvent{});
    m_transport.FailCampaign(reason);
}
uint64_t WorldItemService::Cookie() noexcept
{
    return m_cookie == UINT64_MAX ? 0 : ++m_cookie;
}
bool WorldItemService::Count(GameId id, int64_t& total) noexcept
{
    total = 0;
    auto* p = PlayerCharacter::Get();
    const uint32_t form = m_world.GetModSystem().GetGameId(id);
    if (!p || !form)
        return false;
    // Inventory::Entry deliberately omits stolen ownership/custom text. Check
    // every native instance list too, so a base-form removal cannot consume a
    // different hidden variant while the quantity readback still matches.
    if (const auto* changes = p->GetContainerChanges(); changes && changes->entries)
        for (const auto* entry : *changes->entries)
            if (entry && entry->form && entry->form->formID == form && entry->dataList)
                for (auto* extra : *entry->dataList)
                    if (!Plain(extra))
                        return false;
    const auto inventory = p->GetInventory([&](TESForm& f) { return f.formID == form; });
    for (auto item : inventory.Entries)
    {
        item.ExtraWorn = item.ExtraWornLeft = false;
        if (item.ContainsExtraData() || item.Count < 0)
            return false;
        total += item.Count;
    }
    return total <= INT32_MAX;
}
bool WorldItemService::Start(RequestWorldItemTransfer& request) noexcept
{
    if (!m_ready || !m_received || !m_caughtUp || m_intent || m_inbox || m_barrier || NativeGrantDelivery::HasPending() || NativeQuestGate::HasPending() ||
        m_world.ctx().at<WorldCheckpointService>().IsFrozen() || m_operation == UINT64_MAX || !Barrier())
        return false;
    m_barrier = true;
    m_mutated = false;
    request.Operation = ++m_operation;
    request.Admission = m_transport.GetCampaignNonce();
    request.Session = m_session;
    m_intent = request;
    m_deadline = GetTickCount64() + 30000;
    m_retry = 0;
    m_nextQuery = 0;
    return true;
}
bool WorldItemService::Barrier() noexcept
{
    if (m_barrier)
        return true;
    if (!NativeItemDelivery::Begin(m_barrierGeneration))
        return false;
    m_barrier = true;
    m_mutated = false;
    m_deadline = GetTickCount64() + 30000;
    return true;
}
bool WorldItemService::Drop(Actor* actor, TESBoundObject* item, ExtraDataList* extra, int32_t count, const NiPoint3* location) noexcept
{
    auto* service = s_instance;
    if (!service || !Supported() || !actor || actor->formID != 0x14 || !service->m_transport.IsConnected() || !NativeActorPose::OnRunner() ||
        !service->m_world.GetPartyService().IsInParty() || service->m_world.GetPartyService().GetPartyMembers().size() != 2)
        return false;
    if (!item || count < 1 || count > 32767 || !Plain(extra) || item->IsTemporary())
        return false;
    // Only ordinary inventory base forms. Unsupported special objects retain
    // their original native path; they are not advertised as replicated drops.
    const uint32_t type = static_cast<uint8_t>(item->formType);
    if (type != 26 && type != 27 && type != 30 && type != 32 && type != 41 && type != 42 && type != 46 && type != 52)
        return false;
    RequestWorldItemTransfer request;
    auto& state = request.State;
    state.Id = state.Revision = state.Sequence = 1;
    state.WorldEpoch = service->m_transport.GetWorldEpoch();
    state.HostEpoch = service->m_hostEpoch;
    state.Tick = service->m_transport.GetClock().GetCurrentTick();
    state.Item.Count = count;
    request.Delta = -count;
    auto& mods = service->m_world.GetModSystem();
    if (!actor->parentCell || !mods.GetServerModId(actor->parentCell->formID, state.CellId) || !mods.GetServerModId(item->formID, state.Item.BaseId) ||
        (actor->GetWorldSpace() && !mods.GetServerModId(actor->GetWorldSpace()->formID, state.WorldSpaceId)))
        return true;
    NiPoint3 position = location ? *location : actor->position;
    if (!location)
    {
        position.x += std::sin(actor->rotation.z) * 70.f;
        position.y += std::cos(actor->rotation.z) * 70.f;
        position.z += 45.f;
    }
    state.Position = glm::vec3(position.x, position.y, position.z);
    int64_t available{};
    if (!service->Count(state.Item.BaseId, available) || available < count || !state.IsValid() || !service->Start(request))
        service->m_world.GetOverlayService().SendSystemMessage("Предмет пока не выброшен: дождитесь синхронизации или завершения текущей передачи.");
    return true; // Defer BEFORE native inventory mutation; caller receives an empty handle.
}
bool WorldItemService::Pickup(Actor* actor, TESObjectREFR* ref, int32_t count) noexcept
{
    auto* service = s_instance;
    if (!service || !Supported() || !actor || actor->formID != 0x14 || !ref || !ref->IsTemporary() || !service->m_transport.IsConnected() || !NativeActorPose::OnRunner() ||
        !service->m_world.GetPartyService().IsInParty() || service->m_world.GetPartyService().GetPartyMembers().size() != 2)
        return false;
    if (!service->m_ready)
        return true; // Saved aliases have not yet been scanned.
    for (const auto& slot : service->m_slots)
        if (slot.Map.FormId == ref->formID && slot.Map.Id)
        {
            RequestWorldItemTransfer request;
            request.State = slot.State;
            request.Delta = count;
            if (count < 1 || count > slot.State.Item.Count || !service->Start(request))
                service->m_world.GetOverlayService().SendSystemMessage("Подбор ожидает завершения синхронизации предметов.");
            return true;
        }
    return false;
}
void WorldItemService::OnTransfer(const NotifyWorldItemTransfer& message) noexcept
{
    if (!message.IsValid() || !m_intent || message.Operation != m_intent->Operation || message.Admission != m_intent->Admission)
        return;
    if (!message.Accepted)
    {
        m_intent.reset();
        NativeItemDelivery::Complete(m_barrierGeneration);
        m_barrier = m_mutated = false;
        m_world.GetOverlayService().SendSystemMessage("Передача предмета отменена сервером: предмет уже изменён, недоступен или достигнут лимит 64 объектов.");
    }
    else
    {
        m_retry = UINT64_MAX;
        m_nextQuery = 0;
    }
}
void WorldItemService::OnSnapshot(const NotifyWorldItems& message) noexcept
{
    if (!message.IsValid() || !m_transport.IsConnected())
        return;
    if (message.HostEpoch < m_hostEpoch)
        return;
    if (message.Nonce)
    {
        if (message.Nonce != m_waiting || message.After != m_cursor)
            return;
        m_waiting = 0;
        m_inbox = message;
    }
    if (m_snapshot && message.Head < m_snapshot->Head)
        return;
    const bool lifecycleChanged = !m_snapshot || message.HostEpoch != m_hostEpoch || message.Items.size() != m_snapshot->Items.size() ||
                                  std::any_of(
                                      message.Items.begin(), message.Items.end(),
                                      [&](const auto& item)
                                      {
                                          return std::none_of(
                                              m_snapshot->Items.begin(), m_snapshot->Items.end(),
                                              [&](const auto& old) { return old.Id == item.Id && old.Revision == item.Revision && old.Item == item.Item; });
                                      });
    if (m_snapshot)
        for (const auto& old : m_snapshot->Items)
            if (std::none_of(message.Items.begin(), message.Items.end(), [&](const auto& item) { return item.Id == old.Id; }))
                m_timeline.Forget(old.Id);
    m_snapshot = message;
    if (lifecycleChanged)
        m_reconcile = 0;
    if (message.Head > m_cursor)
        m_caughtUp = false;
    if (m_hostEpoch != message.HostEpoch)
    {
        m_hostEpoch = message.HostEpoch;
        m_timeline.Reset(m_transport.GetWorldEpoch(), m_hostEpoch);
        for (auto& slot : m_slots)
        {
            slot.Capture = slot.Apply = 0;
            slot.Input.reset();
        }
    }
    for (const auto& item : message.Items)
        if (item.WorldEpoch != m_transport.GetWorldEpoch() || item.HostEpoch != m_hostEpoch)
        {
            Fail("World-item snapshot belongs to another timeline");
            return;
        }
        else
            m_timeline.Lifecycle(item);
    for (size_t i = 0; i < message.Items.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (message.Items[i].Id == message.Items[j].Id)
            {
                Fail("Duplicate logical item identity in world snapshot");
                return;
            }
    m_received = true;
}
bool WorldItemService::Receive() noexcept
{
    // A latent quest gate can wait for an item that was already picked up.
    // It must not prevent this earlier receipt from reaching the inventory.
    // Each actual mutation/readback runs atomically on the admitted runner.
    if (!m_inbox || NativeGrantDelivery::HasPending())
        return false;
    if (m_inbox->Receipt.empty() && m_inbox->Head == m_cursor)
    {
        m_caughtUp = true;
        m_inbox.reset();
        return true;
    }
    if (!m_barrier)
    {
        if (!Barrier())
        {
            Fail("Interrupted item delivery requires loading the paired checkpoint");
            return false;
        }
        m_barrier = true;
        m_mutated = false;
    }
    const auto& message = *m_inbox;
    Campaign::WorldItemReceipt receipt;
    uint64_t next = message.Head;
    if (!message.Receipt.empty())
    {
        if (!Campaign::Decode(message.Receipt, receipt) || receipt.Recipient != m_character || receipt.State.Revision <= m_cursor || receipt.State.Revision > message.Head ||
            receipt.State.Item.ContainsExtraData() || (m_intent && (receipt.Session != m_session || receipt.Operation != m_intent->Operation)))
        {
            Fail("Invalid or mismatched item receipt; load the paired checkpoint");
            return false;
        }
        const auto delta = receipt.InventoryDelta();
        int64_t before{}, after{};
        if (!Count(delta.BaseId, before) || before + delta.Count < 0 || before + delta.Count > INT32_MAX)
        {
            Fail("World-item receipt cannot be applied to this inventory; load the paired checkpoint");
            return false;
        }
        m_mutated = true;
        {
            ScopedInventoryOverride guard;
            PlayerCharacter::Get()->AddOrRemoveItem(delta);
        }
        if (!Count(delta.BaseId, after) || after != before + delta.Count)
        {
            Fail("World-item native inventory readback failed; reload before saving");
            return false;
        }
        // Update transient server actor inventory once, separately from the
        // durable receipt. Native hooks were suppressed around the mutation.
        m_world.GetRunner().Trigger(InventoryChangeEvent(0x14, delta));
        next = receipt.State.Revision;
    }
    m_mutated = true;
    if (!WorldItemRegistry::AdvanceReceipt(m_cursor, next))
    {
        Fail("Cannot save the world-item receipt cursor");
        return false;
    }
    m_cursor = next;
    m_caughtUp = message.Receipt.empty();
    if (!message.Receipt.empty() && m_intent)
        m_intent.reset();
    m_inbox.reset();
    m_nextQuery = 0;
    return true;
}
bool WorldItemService::Reconcile() noexcept
{
    if (!m_snapshot)
        return false;
    // Four saved aliases are examined per frame; at most one object is
    // created, deleted or changes count. No cell loading is requested here.
    for (size_t visited = 0; visited < 4 && m_reconcile < m_slots.size(); ++visited, ++m_reconcile)
    {
        auto& slot = m_slots[m_reconcile];
        if (!slot.Map.Id)
            continue;
        const auto found = std::find_if(m_snapshot->Items.begin(), m_snapshot->Items.end(), [&](const auto& s) { return s.Id == slot.Map.Id; });
        ScopedReference ref(slot.Map.Handle);
        if (!ref || ref.Get()->formID != slot.Map.FormId || !ref.Get()->baseForm)
        {
            Fail("Saved world-item alias no longer resolves");
            return false;
        }
        if (found == m_snapshot->Items.end())
        {
            if (!Barrier())
                return false;
            m_barrier = m_mutated = true;
            if (!WorldItemRegistry::Clear(m_reconcile, slot.Map.Id))
            {
                Fail("Cannot retire the saved world-item alias");
                return false;
            }
            ref.Get()->Disable();
            ref.Get()->Delete();
            slot = {};
            ++m_reconcile;
            return false;
        }
        const uint32_t form = m_world.GetModSystem().GetGameId(found->Item.BaseId);
        if (ref.Get()->baseForm->formID != form)
        {
            Fail("World-item base form differs from its saved alias");
            return false;
        }
        if (slot.State.Revision != found->Revision)
        {
            slot.Capture = slot.Apply = 0;
            slot.Input.reset();
        }
        slot.State = *found;
        if (const auto* latest = m_timeline.Latest(found->Id); latest && latest->Revision == found->Revision)
            static_cast<WorldItemMotion&>(slot.State) = *latest;
        if (ref.Get()->GetExtraDataList()->GetCount() != uint32_t(found->Item.Count))
        {
            if (!Barrier())
                return false;
            m_barrier = m_mutated = true;
            if (!SetCount(ref.Get(), found->Item.Count))
            {
                Fail("Cannot restore world-item quantity");
                return false;
            }
            ++m_reconcile;
            return false;
        }
    }
    if (m_reconcile < m_slots.size())
        return false;
    for (const auto& item : m_snapshot->Items)
    {
        if (std::any_of(m_slots.begin(), m_slots.end(), [&](const auto& s) { return s.Map.Id == item.Id; }))
            continue;
        auto& mods = m_world.GetModSystem();
        auto* cell = Cast<TESObjectCELL>(TESForm::GetById(mods.GetGameId(item.CellId)));
        auto* world = item.WorldSpaceId ? Cast<TESWorldSpace>(TESForm::GetById(mods.GetGameId(item.WorldSpaceId))) : nullptr;
        if (!cell || !cell->IsAttached())
            continue;
        auto* base = Cast<TESBoundObject>(TESForm::GetById(mods.GetGameId(item.Item.BaseId)));
        if (!base || (item.WorldSpaceId && !world))
        {
            Fail("World-item content is missing");
            return false;
        }
        auto slot = std::find_if(m_slots.begin(), m_slots.end(), [](const auto& s) { return !s.Map.Id; });
        if (slot == m_slots.end())
        {
            Fail("World-item native alias capacity exhausted");
            return false;
        }
        if (!Barrier())
            return false;
        m_barrier = m_mutated = true;
        NiPoint3 position{glm::vec3(item.Position.x, item.Position.y, item.Position.z)}, rotation{};
        const uint32_t handle = ModManager::Get()->SpawnObject(base, position, rotation, cell, world);
        ScopedReference ref(handle);
        const uint32_t index = static_cast<uint32_t>(slot - m_slots.begin());
        if (!ref || !ref.Get()->IsTemporary() || !SetCount(ref.Get(), item.Item.Count) || !WorldItemRegistry::Bind(index, item.Id, ref.Get()) ||
            !WorldItemRegistry::ReadSlot(index, slot->Map))
        {
            Fail("Cannot create and bind a dropped world item; reload before saving");
            return false;
        }
        slot->State = item;
        return false;
    }
    return true;
}
void WorldItemService::OnMotion(const NotifyWorldItemMotion& message) noexcept
{
    const auto& state = message.State;
    if (!message.Decoded || !state.IsValid() || state.WorldEpoch != m_transport.GetWorldEpoch() || state.HostEpoch != m_hostEpoch || !m_ready ||
        m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return;
    for (auto& slot : m_slots)
        if (slot.Map.Id == state.Id && slot.State.Revision == state.Revision)
        {
            if (message.Interaction)
            {
                if (!m_world.GetPartyService().IsLeader() || state.Tick <= slot.LastInputTick)
                    return;
                slot.Input = state;
                slot.InputAt = GetTickCount64();
                slot.LastInputTick = state.Tick;
            }
            else if (!m_world.GetPartyService().IsLeader() && m_timeline.Motion(state) == WorldItemTimeline::Result::Applied)
            {
                slot.Received = GetTickCount64();
                static_cast<WorldItemMotion&>(slot.State) = state;
            }
            return;
        }
}
bool WorldItemService::ValidatePhysics(uint32_t handle, uint64_t cookie) noexcept
{
    auto* self = s_instance;
    if (!self || !self->m_ready || !self->m_transport.IsConnected() || self->m_generation != self->m_transport.GetConnectionGeneration() ||
        self->m_loadGeneration != NativeItemDelivery::Generation() || self->m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return false;
    const bool host = self->m_world.GetPartyService().IsLeader();
    for (const auto& slot : self->m_slots)
        if (slot.Map.Handle == handle && slot.Map.Id && slot.State.HostEpoch == self->m_hostEpoch && slot.State.WorldEpoch == self->m_transport.GetWorldEpoch())
        {
            const bool capture = slot.Capture == cookie, apply = slot.Apply == cookie;
            if ((!capture && !apply) || (capture ? slot.CaptureInteraction == host : slot.ApplyInput != host))
                return false;
            ScopedReference ref(handle);
            if (!ref || ref.Get()->formID != slot.Map.FormId || !ref.Get()->parentCell)
                return false;
            GameId cell, world;
            auto& mods = self->m_world.GetModSystem();
            if (!mods.GetServerModId(ref.Get()->parentCell->formID, cell) || (ref.Get()->GetWorldSpace() && !mods.GetServerModId(ref.Get()->GetWorldSpace()->formID, world)) ||
                cell != slot.State.CellId || world != slot.State.WorldSpaceId)
                return false;
            const uint32_t grabbed = NativeActorPose::GrabbedHandle();
            return capture ? !slot.CaptureInteraction || grabbed == handle : grabbed != handle;
        }
    return false;
}
void WorldItemService::Physics(uint64_t now) noexcept
{
    PhysicsReplication::Result result;
    while (PhysicsReplication::Poll(result))
        for (auto& slot : m_slots)
        {
            if (slot.Apply == result.Cookie)
            {
                slot.Apply = 0;
                break;
            }
            if (slot.Capture != result.Cookie)
                continue;
            slot.Capture = 0;
            if (result.State == PhysicsReplication::Status::Captured && now >= result.CapturedAt && now - result.CapturedAt <= 150 && m_sequence != UINT64_MAX)
            {
                RequestWorldItemMotion request;
                request.Interaction = slot.CaptureInteraction;
                request.State = slot.State;
                request.State.Sequence = ++m_sequence;
                request.State.Tick = m_transport.GetClock().GetCurrentTick();
                request.State.Position = glm::vec3(result.Transform.Position[0], result.Transform.Position[1], result.Transform.Position[2]);
                request.State.Rotation = result.Transform.Rotation;
                request.State.LinearVelocity = result.Transform.LinearVelocity;
                request.State.AngularVelocity = result.Transform.AngularVelocity;
                if (request.State.IsValid())
                    m_transport.Send(request, TiltedPhoques::kUnreliableNoNagle);
            }
            else
                slot.NextCapture = now + 1000;
            break;
        }
    const bool host = m_world.GetPartyService().IsLeader();
    const uint32_t held = NativeActorPose::GrabbedHandle();
    for (size_t visited = 0, queued = 0; visited < 16 && queued < 2; ++visited)
    {
        auto& slot = m_slots[m_physicsCursor++ % m_slots.size()];
        if (!slot.Map.Id || !slot.State.IsValid())
            continue;
        if (now > slot.Deadline)
            slot.Capture = slot.Apply = 0;
        if ((host || held == slot.Map.Handle) && !slot.Capture && !slot.Apply && now >= slot.NextCapture)
        {
            const uint64_t cookie = Cookie();
            slot.Capture = cookie;
            slot.CaptureInteraction = !host;
            if (cookie && PhysicsReplication::QueueCapture(slot.Map.Handle, cookie, &ValidatePhysics))
            {
                slot.Deadline = now + 300;
                slot.NextCapture = now + 100;
                ++queued;
            }
            else
                slot.Capture = 0;
        }
        if (slot.Apply || slot.Capture || held == slot.Map.Handle || now < slot.NextApply)
            continue;
        WorldItemMotion sample;
        bool apply{};
        if (host && slot.Input && now <= slot.InputAt + 150)
        {
            sample = *slot.Input;
            slot.Input.reset();
            apply = true;
        }
        else if (!host && slot.Received && now <= slot.Received + 500)
        {
            const uint64_t tick = m_transport.GetClock().GetCurrentTick();
            const auto* latest = m_timeline.Latest(slot.Map.Id);
            apply = latest && tick <= latest->Tick + 250 && m_timeline.Sample(slot.Map.Id, tick > 100 ? tick - 100 : 0, sample);
        }
        if (apply)
        {
            const uint64_t cookie = Cookie();
            slot.Apply = cookie;
            slot.ApplyInput = host;
            if (cookie && PhysicsReplication::QueueApply(slot.Map.Handle, cookie, Pose(sample), 0, &ValidatePhysics))
            {
                slot.Deadline = now + 300;
                slot.NextApply = now + 30;
                ++queued;
            }
            else
                slot.Apply = 0;
        }
    }
}
void WorldItemService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsConnected() || !m_generation || !NativeActorPose::OnRunner() || m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return;
    if (m_generation != m_transport.GetConnectionGeneration() || m_loadGeneration != NativeItemDelivery::Generation())
    {
        OnDisconnected(DisconnectedEvent{});
        return;
    }
    if (!m_world.GetPartyService().IsInParty() || m_world.GetPartyService().GetPartyMembers().size() != 2)
        return;
    const uint64_t now = GetTickCount64();
    if (!m_ready)
    {
        bool assigned{};
        for (const auto entity : m_world.view<LocalComponent, FormIdComponent>())
            if (m_world.get<FormIdComponent>(entity).Id == 0x14)
                assigned = true;
        if (!assigned || SavedCharacterIdentity::Read(m_character) != SavedCharacterIdentity::Result::Found || !WorldItemRegistry::BindReceipt())
        {
            if (now > m_deadline)
                Fail("Cannot initialize the saved world-item registry");
            return;
        }
        if (!m_barrier)
        {
            if (!Barrier())
            {
                Fail("Interrupted world-item transfer requires a paired reload");
                return;
            }
            m_barrier = true;
        }
        for (size_t i = 0; i < 4 && m_scan < m_slots.size(); ++i, ++m_scan)
            if (!WorldItemRegistry::ReadSlot(m_scan, m_slots[m_scan].Map))
            {
                Fail("Saved world-item registry is invalid");
                return;
            }
        if (m_scan < m_slots.size())
            return;
        for (size_t i = 0; i < m_slots.size(); ++i)
            if (m_slots[i].Map.Id)
                for (size_t j = 0; j < i; ++j)
                    if (m_slots[i].Map.Id == m_slots[j].Map.Id || m_slots[i].Map.FormId == m_slots[j].Map.FormId)
                    {
                        Fail("Duplicate saved world-item alias");
                        return;
                    }
        bool bound{};
        if (!WorldItemRegistry::ReadReceipt(m_cursor, bound) || !bound)
        {
            Fail("Cannot read the saved item cursor");
            return;
        }
        m_ready = true;
    }
    Receive();
    if (!m_transport.IsConnected())
        return;
    const bool reconciled = Reconcile();
    if (!m_transport.IsConnected())
        return;
    if (m_barrier && m_caughtUp && reconciled && !m_inbox && !m_intent)
    {
        NativeItemDelivery::Complete(m_barrierGeneration);
        m_barrier = m_mutated = false;
    }
    if (m_intent && now >= m_retry)
    {
        m_transport.Send(*m_intent);
        m_retry = now + 1000;
    }
    if ((m_intent || m_barrier) && now > m_deadline)
    {
        Fail("World-item synchronization timed out; load the paired checkpoint");
        return;
    }
    if (!m_inbox && now >= m_nextQuery)
    {
        RequestWorldItems query;
        if (!m_waiting)
        {
            if (m_queryNonce == UINT64_MAX)
            {
                Fail("Item query nonce exhausted");
                return;
            }
            m_waiting = ++m_queryNonce;
        }
        query.Nonce = m_waiting;
        query.After = m_cursor;
        m_transport.Send(query);
        m_nextQuery = now + 1000;
    }
    Physics(now);
}
