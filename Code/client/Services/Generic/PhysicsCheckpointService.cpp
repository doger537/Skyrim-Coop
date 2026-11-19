#include <Services/PhysicsCheckpointService.h>
#include <Services/WorldCheckpointService.h>
#include <Services/PlacedPhysicsService.h>
#include <Services/WorldItemService.h>
#include <World.h>
#include <Components.h>
#include <NativeCheckpoint.h>
#include <AI/NativeActorPose.h>
#include <Forms/SavedCharacterIdentity.h>
#include <Forms/TESObjectCELL.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <ScopedReference.h>
#include <common/PhysicsFrameHash.h>
#include <algorithm>

PhysicsCheckpointService* PhysicsCheckpointService::s_instance{};
namespace
{
bool Equal(const RigidBodyRig& a, const RigidBodyRig& b) noexcept
{
    if (!a.SameGraph(b))
        return false;
    for (size_t i = 0; i < a.Count; ++i)
    {
        const auto& p = a.Bodies[i].Pose;
        const auto& q = b.Bodies[i].Pose;
        if (p.Position != q.Position || p.Rotation != q.Rotation || p.LinearVelocity != q.LinearVelocity || p.AngularVelocity != q.AngularVelocity)
            return false;
    }
    return true;
}
} // namespace
PhysicsCheckpointService::PhysicsCheckpointService(World& w, entt::dispatcher& d, TransportService& t) noexcept
    : m_world(w)
    , m_transport(t)
{
    s_instance = this;
    m_frame = d.sink<NotifyPhysicsFrame>().connect<&PhysicsCheckpointService::OnFrame>(this);
}
bool PhysicsCheckpointService::Begin(uint64_t id, uint64_t nonce, size_t slot, const String& world, const CharacterIdentity& character, const std::filesystem::path& path) noexcept
{
    if (m_phase != Phase::Idle || m_io.valid() || !id || !nonce || slot > 1 || !NativeCheckpoint::IsPaused())
        return false;
    m_id = id;
    m_nonce = nonce;
    m_slot = slot;
    m_generation = m_transport.GetConnectionGeneration();
    m_path = path;
    m_map = {};
    m_map.World = world;
    m_map.Character = character;
    m_map.Checkpoint = id;
    m_offer = {};
    m_offer.Checkpoint = id;
    POINTER_SKYRIMSE(float, inverseScale, 187407);
    m_offer.NativeToWorld = *inverseScale.Get();
    m_canonical = {};
    m_canonical.Checkpoint = id;
    m_canonical.NativeToWorld = m_offer.NativeToWorld;
    m_end = m_failed = m_scanned = false;
    m_sent = m_cell = m_applyCursor = m_remaining = 0;
    m_expected = {};
    m_candidates.clear();
    m_targets.clear();
    m_seen.clear();
    m_scan = {};
    m_probes = {};
    m_jobs = {};
    m_actors.clear();
    return true;
}
bool PhysicsCheckpointService::Capture(
    uint64_t id, uint64_t nonce, size_t slot, const String& world, const CharacterIdentity& character, const std::filesystem::path& path) noexcept
{
    if (!Begin(id, nonce, slot, world, character, path))
        return false;
    m_load = false;
    m_phase = Phase::Capture;
    m_cells = m_world.ctx().at<PlacedPhysicsService>().SnapshotLoadedCells();
    if (m_cells.empty())
        return false;
    for (const auto entity : m_world.view<FormIdComponent, LocalComponent>())
    {
        const auto& local = m_world.get<LocalComponent>(entity);
        m_actors.emplace(m_world.get<FormIdComponent>(entity).Id, std::pair{local.Id, local.OwnershipEpoch});
    }
    for (const auto entity : m_world.view<FormIdComponent, RemoteComponent>())
    {
        const auto& remote = m_world.get<RemoteComponent>(entity);
        m_actors.emplace(m_world.get<FormIdComponent>(entity).Id, std::pair{remote.Id, remote.OwnershipEpoch});
    }
    return true;
}
bool PhysicsCheckpointService::Load(
    uint64_t id, uint64_t nonce, size_t slot, const String& world, const CharacterIdentity& character, const std::filesystem::path& path,
    const PhysicsFrame::Digest& digest) noexcept
{
    if (!Begin(id, nonce, slot, world, character, path))
        return false;
    m_load = true;
    m_expected = digest;
    m_phase = Phase::ReadMap;
    m_io = std::async(
        std::launch::async,
        [path]()
        {
            IOResult result;
            result.Valid = PhysicsSaveMap::Read(path, result.Map);
            return result;
        });
    return true;
}
void PhysicsCheckpointService::Cancel() noexcept
{
    m_phase = Phase::Idle;
    m_candidates.clear();
    m_targets.clear();
    m_probes = {};
    m_jobs = {};
    m_seen.clear();
    m_actors.clear();
    m_offer = {};
    m_canonical = {};
    m_map = {};
    m_end = false;
    PhysicsReplication::Invalidate(); // late jobs/results cannot reach another checkpoint
}
bool PhysicsCheckpointService::Ready(const PhysicsFrame::Digest& digest) const noexcept
{
    return m_phase == Phase::Ready && m_expected == digest;
}
void PhysicsCheckpointService::Fail(const char* reason) noexcept
{
    if (m_failed)
        return;
    m_failed = true;
    m_world.ctx().at<WorldCheckpointService>().PhysicsFailed(reason);
}
bool PhysicsCheckpointService::Describe(uint32_t handle, Target& target) noexcept
{
    ScopedReference reference(handle);
    auto* ref = reference.Get();
    if (!ref || ref->IsDeleted() || (ref->flags & TESForm::DISABLED) || !ref->GetNiNode())
        return false;
    target = {};
    target.Handle = handle;
    target.Form = ref->formID;
    target.State.SourceSlot = static_cast<uint8_t>(m_slot);
    target.State.PresentMask = static_cast<uint8_t>(1 << m_slot);
    if (auto* actor = Cast<Actor>(ref))
    {
        if (!NativeActorPose::IsRagdoll(actor))
            return false;
        const auto found = m_actors.find(ref->formID);
        target.State.Type = PhysicsFrameEntry::Actor;
        if (found == m_actors.end())
            target.Unknown = true;
        else
        {
            target.State.Id = found->second.first;
            target.State.Epoch = found->second.second;
        }
    }
    else if (ref->IsTemporary())
    {
        target.State.Type = PhysicsFrameEntry::Drop;
        if (!m_world.ctx().at<WorldItemService>().PhysicsIdentity(handle, target.State.Id, target.State.Epoch))
            target.Unknown = true;
    }
    else
    {
        target.State.Type = PhysicsFrameEntry::Placed;
        if (!m_world.GetModSystem().GetServerModId(ref->formID, target.State.Reference))
            target.Unknown = true;
        if (const auto* state = m_world.ctx().at<PlacedPhysicsService>().Find(target.State.Reference))
            target.State.Epoch = state->Epoch;
    }
    return true;
}
bool PhysicsCheckpointService::Validate(uint32_t handle, uint64_t cookie) noexcept
{
    auto* self = s_instance;
    if (!self || self->m_phase == Phase::Idle || self->m_phase == Phase::Ready || self->m_failed || !NativeCheckpoint::IsPaused() ||
        !self->m_world.ctx().at<WorldCheckpointService>().IsFrozen() || self->m_generation != self->m_transport.GetConnectionGeneration())
        return false;
    for (const auto& target : self->m_probes)
        if (target.Cookie == cookie && target.Handle == handle)
            return true;
    for (const auto& job : self->m_jobs)
        if (job.Cookie == cookie && job.Target < self->m_targets.size())
        {
            const auto& target = self->m_targets[job.Target];
            ScopedReference ref(handle);
            return target.Handle == handle && ref && ref.Get()->formID == target.Form;
        }
    return false;
}
void PhysicsCheckpointService::Scan() noexcept
{
    if (!m_scanned && m_candidates.size() < 16)
    {
        if (m_cell >= m_cells.size())
            m_scanned = true;
        else
        {
            auto* cell = Cast<TESObjectCELL>(TESForm::GetById(m_world.GetModSystem().GetGameId(m_cells[m_cell])));
            const auto batch = PhysicalCellScan::Next(cell, m_scan);
            if (batch.Invalid)
            {
                Fail("Loaded cell changed during the physical snapshot");
                return;
            }
            for (uint32_t i = 0; i < batch.Count; ++i)
                if (batch.Handles[i])
                    m_candidates.push_back(batch.Handles[i]);
            if (batch.Complete)
            {
                ++m_cell;
                m_scan = {};
            }
        }
    }
    for (auto& probe : m_probes)
    {
        if (probe.Cookie || m_candidates.empty())
            continue;
        const uint32_t handle = m_candidates.front();
        if (!Describe(handle, probe))
        {
            m_candidates.pop_front();
            continue;
        }
        if (m_seen.contains(probe.Form))
        {
            m_candidates.pop_front();
            probe = {};
            continue;
        }
        if (m_seen.size() >= 262144 || m_cookie == UINT64_MAX)
        {
            Fail("Physical checkpoint reference capacity exceeded");
            return;
        }
        const uint64_t cookie = ++m_cookie;
        probe.Cookie = cookie;
        if (!PhysicsReplication::QueueObjectCapture(handle, cookie, 2, &Validate))
        {
            probe = {};
            break;
        }
        m_seen.insert(probe.Form);
        m_candidates.pop_front();
    }
}
void PhysicsCheckpointService::Poll() noexcept
{
    PhysicsReplication::RigResult result;
    while (PhysicsReplication::PollRig(result, 2))
    {
        auto probe = std::find_if(m_probes.begin(), m_probes.end(), [&](const auto& target) { return target.Cookie == result.Cookie; });
        if (probe != m_probes.end())
        {
            Target target = *probe;
            *probe = {};
            if (result.State == PhysicsReplication::Status::FixedBody)
                continue;
            if (result.State != PhysicsReplication::Status::Captured || target.Unknown)
            {
                Fail("A physical object has no verified shared identity or supported body graph");
                return;
            }
            target.Cookie = 0;
            target.State.Rig = result.Rig;
            target.State.Cell = result.Cell;
            target.State.WorldSpace = result.WorldSpace;
            if (!target.State.IsValid() || m_targets.size() >= PhysicsFrame::kMaxEntries)
            {
                Fail("Invalid or oversized physical checkpoint");
                return;
            }
            m_targets.push_back(std::move(target));
            continue;
        }
        auto active = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const auto& job) { return job.Cookie == result.Cookie; });
        if (active == m_jobs.end() || active->Target >= m_targets.size())
            continue;
        auto* target = &m_targets[active->Target];
        *active = {};
        target->Cookie = 0;
        if (m_phase == Phase::Apply && result.State == PhysicsReplication::Status::Applied)
        {
            target->Applied = true;
            --m_remaining;
            ScopedReference ref(target->Handle);
            if (!ref || ref.Get()->formID != target->Form)
            {
                Fail("Physical reference disappeared before native save");
                return;
            }
            ref.Get()->MarkChanged(4);
        }
        else if (m_phase == Phase::Verify && result.State == PhysicsReplication::Status::Captured && Equal(result.Rig, target->State.Rig))
        {
            target->Verified = true;
            --m_remaining;
        }
        else
        {
            Fail("Native physical pose/readback differs; paired save/load cancelled");
            return;
        }
    }
}
void PhysicsCheckpointService::OnFrame(const NotifyPhysicsFrame& message) noexcept
{
    if (!message.IsValid() || m_phase == Phase::Idle || message.Data.Id != m_id || message.Data.Nonce != m_nonce)
        return;
    if (m_phase == Phase::ReadMap)
    {
        Fail("Physical load mapping has not finished validation");
        return;
    }
    if (m_phase != Phase::Await || m_end || message.Data.NativeToWorld != m_offer.NativeToWorld || message.Data.Index != m_canonical.Entries.size())
    {
        Fail("Physical snapshot stream boundary differs");
        return;
    }
    if (message.Action == NotifyPhysicsFrame::Canonical)
    {
        for (const auto& entry : message.Data.Entries)
        {
            if (!m_canonical.Entries.empty() && !PhysicsFrameEntry::Less(m_canonical.Entries.back(), entry))
            {
                Fail("Duplicate or unordered canonical physical key");
                return;
            }
            m_canonical.Entries.push_back(entry);
        }
        return;
    }
    if (m_load && message.Data.Digest != m_expected)
    {
        Fail("Loaded physical snapshot differs from the saved manifest");
        return;
    }
    m_expected = message.Data.Digest;
    m_end = true;
    if (!m_canonical.IsValid())
    {
        Fail("Invalid canonical physical checkpoint");
        return;
    }
    if (m_load)
    {
        m_targets.clear();
        for (const auto& entry : m_map.Entries)
        {
            PhysicsFrameEntry key;
            key.Type = entry.Type;
            key.Reference = entry.Reference;
            key.Id = entry.Id;
            auto state = std::lower_bound(m_canonical.Entries.begin(), m_canonical.Entries.end(), key, &PhysicsFrameEntry::Less);
            if (state == m_canonical.Entries.end() || !state->SameKey(key) || !(state->PresentMask & (1 << m_slot)))
            {
                Fail("Saved local mapping does not match canonical physical keys");
                return;
            }
            Target target;
            target.State = *state;
            target.Form = entry.LocalForm;
            m_targets.push_back(std::move(target));
        }
    }
    else
        for (auto& target : m_targets)
        {
            auto state = std::lower_bound(m_canonical.Entries.begin(), m_canonical.Entries.end(), target.State, &PhysicsFrameEntry::Less);
            if (state == m_canonical.Entries.end() || !state->SameKey(target.State) || !(state->PresentMask & (1 << m_slot)))
            {
                Fail("Canonical snapshot omitted a local physical object");
                return;
            }
            target.State = *state;
        }
    const size_t expected = std::count_if(m_canonical.Entries.begin(), m_canonical.Entries.end(), [&](const auto& s) { return (s.PresentMask & (1 << m_slot)) != 0; });
    if (expected != m_targets.size())
    {
        Fail("Physical snapshot coverage differs between the file and this client");
        return;
    }
    m_generation = m_transport.GetConnectionGeneration();
    m_phase = Phase::Apply;
    m_applyCursor = 0;
    m_remaining = m_targets.size();
}
void PhysicsCheckpointService::Apply() noexcept
{
    size_t queued{};
    for (size_t n = 0; n < std::min(size_t(64), m_targets.size()) && queued < 4; ++n)
    {
        const size_t index = m_applyCursor++ % m_targets.size();
        auto& target = m_targets[index];
        if (target.Cookie || (m_phase == Phase::Apply ? target.Applied : target.Verified))
            continue;
        auto active = std::find_if(m_jobs.begin(), m_jobs.end(), [](const auto& job) { return !job.Cookie; });
        if (active == m_jobs.end())
            break;
        auto* ref = Cast<TESObjectREFR>(TESForm::GetById(target.Form));
        if (!ref || !ref->GetNiNode())
            continue;
        if (target.State.Type == PhysicsFrameEntry::Actor && (!Cast<Actor>(ref) || !NativeActorPose::IsRagdoll(Cast<Actor>(ref))))
            continue;
        if (target.State.Type == PhysicsFrameEntry::Placed)
        {
            GameId id;
            if (!m_world.GetModSystem().GetServerModId(ref->formID, id) || id != target.State.Reference)
            {
                Fail("Saved physical reference resolves to different content");
                return;
            }
        }
        if (m_cookie == UINT64_MAX)
        {
            Fail("Physical operation counter exhausted");
            return;
        }
        target.Handle = ref->GetHandle().handle.iBits;
        const uint64_t cookie = ++m_cookie;
        target.Cookie = cookie;
        const bool ok = m_phase == Phase::Apply
                            ? PhysicsReplication::QueueObjectApply(target.Handle, cookie, 2, target.State.Rig, target.State.Cell, target.State.WorldSpace, &Validate)
                            : PhysicsReplication::QueueObjectCapture(target.Handle, cookie, 2, &Validate);
        if (ok)
        {
            *active = {cookie, index};
            ++queued;
        }
        else
        {
            target.Cookie = 0;
            break;
        }
    }
    if (!m_remaining)
    {
        if (m_phase == Phase::Apply)
        {
            m_phase = Phase::Verify;
            m_applyCursor = 0;
            m_remaining = m_targets.size();
        }
        else
            FinishAsync();
    }
}
void PhysicsCheckpointService::FinishAsync() noexcept
{
    PhysicsSaveMap::Document map = std::move(m_map);
    map.Canonical = m_expected;
    map.Entries.clear();
    const auto expected = m_expected;
    const auto path = m_path;
    const bool load = m_load;
    m_phase = Phase::Finish;
    m_io = std::async(
        std::launch::async,
        [frame = std::move(m_canonical), targets = std::move(m_targets), expected, path, load, map = std::move(map)]() mutable
        {
            IOResult result;
            result.Count = static_cast<uint32_t>(frame.Entries.size());
            for (const auto& target : targets)
                map.Entries.push_back({target.State.Type, target.State.Reference, target.State.Id, target.Form});
            Vector<uint8_t> bytes;
            result.Valid = frame.Encode(bytes);
            if (result.Valid)
            {
                result.Digest = HashPhysicsFrame(bytes);
                result.Valid = result.Digest == expected && (load || PhysicsSaveMap::Write(path, map));
            }
            return result;
        });
}
void PhysicsCheckpointService::Tick() noexcept
{
    if (m_phase != Phase::Idle && !NativeCheckpoint::IsPaused())
        return; // actual native load may briefly hide the pause
    if (m_io.valid() && m_io.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        auto result = m_io.get();
        if (m_phase == Phase::Idle)
            return;
        if (!result.Valid)
        {
            Fail("Physical snapshot mapping/digest could not be read or committed");
            return;
        }
        if (m_phase == Phase::ReadMap)
        {
            if (result.Map.World != m_map.World || result.Map.Character != m_map.Character || result.Map.Checkpoint != m_id || result.Map.Canonical != m_expected)
            {
                Fail("Physical sidecar belongs to a different save/character");
                return;
            }
            m_map = std::move(result.Map);
            m_phase = Phase::Await;
            return;
        }
        if (m_phase == Phase::Finish)
        {
            m_phase = Phase::Ready;
            RequestPhysicsFrame ack;
            ack.Action = RequestPhysicsFrame::Applied;
            ack.Data.Id = m_id;
            ack.Data.Nonce = m_nonce;
            ack.Data.Index = result.Count;
            ack.Data.NativeToWorld = m_offer.NativeToWorld;
            ack.Data.Digest = result.Digest;
            m_transport.Send(ack);
            return;
        }
    }
    if (m_phase == Phase::Idle || m_phase == Phase::Ready || m_phase == Phase::ReadMap || m_phase == Phase::Finish || m_phase == Phase::Await)
        return;
    if (!NativeCheckpoint::IsPaused())
    {
        Fail("Physical checkpoint lost the native pause");
        return;
    }
    Poll();
    if (m_failed || m_phase == Phase::Idle)
        return;
    if (m_phase == Phase::Capture)
    {
        Scan();
        if (m_failed)
            return;
        if (m_scanned && m_candidates.empty() && std::none_of(m_probes.begin(), m_probes.end(), [](const auto& p) { return p.Cookie != 0; }))
        {
            std::sort(m_targets.begin(), m_targets.end(), [](const auto& a, const auto& b) { return PhysicsFrameEntry::Less(a.State, b.State); });
            for (const auto& t : m_targets)
                m_offer.Entries.push_back(t.State);
            if (!m_offer.IsValid())
            {
                Fail("Incomplete or duplicate physical capture");
                return;
            }
            m_phase = Phase::Send;
        }
    }
    if (m_phase == Phase::Send)
    {
        RequestPhysicsFrame packet;
        packet.Data.Id = m_id;
        packet.Data.Nonce = m_nonce;
        packet.Data.Index = static_cast<uint32_t>(m_sent);
        packet.Data.NativeToWorld = m_offer.NativeToWorld;
        if (m_sent < m_offer.Entries.size())
        {
            packet.Action = RequestPhysicsFrame::Offer;
            const size_t end = std::min(m_offer.Entries.size(), m_sent + 8);
            for (; m_sent < end; ++m_sent)
                packet.Data.Entries.push_back(m_offer.Entries[m_sent]);
        }
        else
        {
            packet.Action = RequestPhysicsFrame::End;
            m_phase = Phase::Await;
        }
        m_transport.Send(packet);
    }
    if (m_phase == Phase::Apply || m_phase == Phase::Verify)
        Apply();
    for (const auto& probe : m_probes)
        if (probe.Cookie)
        {
            ScopedReference ref(probe.Handle);
            GameId cell;
            if (ref && ref.Get()->parentCell && m_world.GetModSystem().GetServerModId(ref.Get()->parentCell->formID, cell))
                PhysicsReplication::PumpPaused(cell);
        }
    for (const auto& job : m_jobs)
        if (job.Cookie && job.Target < m_targets.size())
            PhysicsReplication::PumpPaused(m_targets[job.Target].State.Cell);
}
