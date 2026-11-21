#include <Services/PlacedPhysicsService.h>
#include <Services/WorldCheckpointService.h>
#include <World.h>
#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <PlayerCharacter.h>
#include <Actor.h>
#include <ScopedReference.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Games/TES.h>
#include <AI/NativeActorPose.h>
#include <Structs/GridCellCoords.h>
#include <algorithm>

PlacedPhysicsService* PlacedPhysicsService::s_instance{};
PlacedPhysicsService::PlacedPhysicsService(World& w, entt::dispatcher& d, TransportService& t) noexcept
    : m_world(w)
    , m_transport(t)
{
    s_instance = this;
    m_pose = d.sink<NotifyPlacedPhysics>().connect<&PlacedPhysicsService::OnPose>(this);
    m_update = d.sink<UpdateEvent>().connect<&PlacedPhysicsService::OnUpdate>(this);
    m_disconnect = d.sink<DisconnectedEvent>().connect<&PlacedPhysicsService::OnDisconnected>(this);
}
void PlacedPhysicsService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_entries.clear();
    m_jobs = {};
    m_cells.clear();
    m_candidates.clear();
    m_scan = {};
    m_cellCursor = 0;
    m_generation = 0;
    m_nextScope = 0;
}
const PlacedPhysicsState* PlacedPhysicsService::Find(GameId id) const noexcept
{
    const auto it = m_entries.find(id);
    return it == m_entries.end() || !it->second.Known ? nullptr : &it->second.State;
}
bool PlacedPhysicsService::Authority(GameId id, uint64_t& epoch) const noexcept
{
    const auto* state = Find(id);
    if (!state || state->Retired || state->Owner != m_transport.GetLocalPlayerId())
        return false;
    epoch = state->Epoch;
    return true;
}
bool PlacedPhysicsService::HasPendingOwnership() const noexcept
{
    for (const auto& pair : m_entries)
        if (!pair.second.Known || pair.second.Seed)
        {
            auto* ref = Cast<TESObjectREFR>(TESForm::GetById(m_world.GetModSystem().GetGameId(pair.first)));
            if (ref && ref->GetNiNode() && ref->parentCell && ref->parentCell->IsAttached())
                return true;
        }
    return false;
}
Vector<GameId> PlacedPhysicsService::SnapshotLoadedCells() noexcept
{
    m_nextScope = 0;
    Scope(GetTickCount64(), false);
    return m_cells;
}
void PlacedPhysicsService::Scope(uint64_t now, bool report) noexcept
{
    if (now < m_nextScope)
        return;
    m_nextScope = now + 2000;
    Vector<GameId> cells;
    auto* player = PlayerCharacter::Get();
    if (!player || !player->parentCell)
        return;
    auto& mods = m_world.GetModSystem();
    auto* world = player->GetWorldSpace();
    if (world)
    {
        const auto center = GridCellCoords::CalculateGridCellCoords(player->position.x, player->position.y);
        const int radius = std::clamp(GridCellCoords::m_gridsToLoad / 2, 0, 5);
        for (int x = -radius; x <= radius; ++x)
            for (int y = -radius; y <= radius; ++y)
            {
                auto* cell = ModManager::Get()->GetCellFromCoordinates(center.X + x, center.Y + y, world, false);
                GameId id;
                if (cell && cell->IsAttached() && mods.GetServerModId(cell->formID, id))
                    cells.push_back(id);
            }
    }
    else
    {
        GameId id;
        if (player->parentCell->IsAttached() && mods.GetServerModId(player->parentCell->formID, id))
            cells.push_back(id);
    }
    if (cells != m_cells)
    {
        m_cells = std::move(cells);
        m_scan = {};
        m_cellCursor = 0;
        m_candidates.clear();
    }
    if (report)
    {
        RequestPlacedPhysics packet;
        packet.Action = RequestPlacedPhysics::Coverage;
        packet.Cells = m_cells;
        m_transport.Send(packet);
    }
}
void PlacedPhysicsService::Discover() noexcept
{
    if (m_cells.empty() || m_candidates.size() > 16)
        return;
    auto* cell = Cast<TESObjectCELL>(TESForm::GetById(m_world.GetModSystem().GetGameId(m_cells[m_cellCursor % m_cells.size()])));
    const auto batch = PhysicalCellScan::Next(cell, m_scan);
    if (batch.Invalid || batch.Complete)
    {
        ++m_cellCursor;
        m_scan = {};
    }
    for (uint32_t i = 0; i < batch.Count; ++i)
        if (batch.Handles[i])
            m_candidates.push_back(batch.Handles[i]);
}
bool PlacedPhysicsService::Queue(GameId id, uint32_t handle, Work mode, const RigidBodyRig* rig) noexcept
{
    auto slot = std::find_if(m_jobs.begin(), m_jobs.end(), [](const auto& job) { return !job.Cookie; });
    if (slot == m_jobs.end() || m_cookie == UINT64_MAX)
        return false;
    const uint64_t cookie = ++m_cookie;
    const auto found = m_entries.find(id);
    const uint64_t epoch = found == m_entries.end() ? 0 : found->second.State.Epoch;
    const bool queued =
        rig ? found != m_entries.end() && PhysicsReplication::QueueObjectApply(handle, cookie, 1, *rig, found->second.State.Cell, found->second.State.WorldSpace, &Validate)
            : PhysicsReplication::QueueObjectCapture(handle, cookie, 1, &Validate);
    if (!queued)
        return false;
    *slot = {cookie, epoch, GetTickCount64() + 500, id, handle, mode};
    return true;
}
bool PlacedPhysicsService::Validate(uint32_t handle, uint64_t cookie) noexcept
{
    auto* self = s_instance;
    if (!self || !self->m_transport.IsConnected() || self->m_generation != self->m_transport.GetConnectionGeneration() ||
        self->m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return false;
    const auto job = std::find_if(self->m_jobs.begin(), self->m_jobs.end(), [&](const auto& item) { return item.Cookie == cookie && item.Handle == handle; });
    if (job == self->m_jobs.end())
        return false;
    ScopedReference ref(handle);
    GameId id;
    if (!ref || ref.Get()->IsTemporary() || Cast<Actor>(ref.Get()) || !self->m_world.GetModSystem().GetServerModId(ref.Get()->formID, id) || id != job->Id)
        return false;
    if (job->Mode == Work::Probe)
        return true;
    const auto entry = self->m_entries.find(id);
    if (entry == self->m_entries.end() || !entry->second.Known || entry->second.State.Retired || entry->second.State.Epoch != job->Epoch)
        return false;
    const bool owner = entry->second.State.Owner == self->m_transport.GetLocalPlayerId();
    const uint32_t held = NativeActorPose::GrabbedHandle();
    switch (job->Mode)
    {
    case Work::Owner: return owner && !entry->second.Seed;
    case Work::Held: return !owner && held == handle;
    case Work::Remote: return !owner && held != handle;
    case Work::Seed: return owner && entry->second.Seed;
    case Work::Input: return owner && held != handle;
    default: return false;
    }
}
void PlacedPhysicsService::Poll(uint64_t now) noexcept
{
    PhysicsReplication::RigResult result;
    while (PhysicsReplication::PollRig(result, 1))
    {
        auto job = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const auto& value) { return value.Cookie == result.Cookie; });
        if (job == m_jobs.end())
            continue;
        const Job completed = *job;
        *job = {};
        auto entry = m_entries.find(completed.Id);
        if (completed.Mode == Work::Probe)
        {
            if (result.State != PhysicsReplication::Status::Captured || entry != m_entries.end())
                continue;
            if (m_entries.size() >= PlacedPhysicsState::kMaxRecords)
            {
                m_transport.FailCampaign("Placed-physics reference capacity exceeded");
                return;
            }
            Entry created;
            created.Handle = completed.Handle;
            created.State.Reference = completed.Id;
            created.State.Cell = result.Cell;
            created.State.WorldSpace = result.WorldSpace;
            created.State.Rig = result.Rig;
            created.State.Owner = m_transport.GetLocalPlayerId();
            created.State.Epoch = created.State.Sequence = 1;
            created.State.Tick = m_transport.GetClock().GetCurrentTick();
            created.Retry = now;
            m_entries.emplace(completed.Id, std::move(created));
            continue;
        }
        if (entry == m_entries.end() || entry->second.State.Epoch != completed.Epoch)
            continue;
        auto& item = entry.value();
        if (completed.Mode == Work::Remote || completed.Mode == Work::Input || completed.Mode == Work::Seed)
        {
            if (result.State == PhysicsReplication::Status::Applied)
            {
                if (completed.Mode == Work::Seed)
                    item.Seed = false;
            }
            else
                item.Next = now + 500;
            continue;
        }
        if (result.State != PhysicsReplication::Status::Captured)
        {
            item.Next = now + 1000;
            // Detaching a cell/3D is not deletion. Keep the server's last pose
            // for the next observer instead of permanently retiring the ref.
            continue;
        }
        if (now < result.CapturedAt || now - result.CapturedAt > 150 || m_sequence == UINT64_MAX)
            continue;
        RequestPlacedPhysics request;
        request.Action = completed.Mode == Work::Held ? RequestPlacedPhysics::Interaction : RequestPlacedPhysics::Motion;
        request.State = item.State;
        request.State.Rig = result.Rig;
        request.State.Cell = result.Cell;
        request.State.WorldSpace = result.WorldSpace;
        request.State.Sequence = std::max(++m_sequence, item.State.Sequence + 1);
        m_sequence = request.State.Sequence;
        request.State.Tick = m_transport.GetClock().GetCurrentTick();
        m_transport.Send(request, TiltedPhoques::kUnreliableNoNagle);
        if (completed.Mode == Work::Owner)
            item.State = request.State;
    }
    for (auto& job : m_jobs)
        if (job.Cookie && now > job.Deadline)
            job = {};
}
void PlacedPhysicsService::OnPose(const NotifyPlacedPhysics& message) noexcept
{
    if (!message.IsValid() || !m_transport.IsConnected() || m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return;
    const auto& state = message.State;
    auto found = m_entries.find(state.Reference);
    if (found == m_entries.end())
    {
        if (m_entries.size() >= PlacedPhysicsState::kMaxRecords)
            return;
        Entry entry;
        entry.State = state;
        found = m_entries.emplace(state.Reference, std::move(entry)).first;
    }
    auto& entry = found.value();
    if (message.Action == NotifyPlacedPhysics::Interaction)
    {
        if (!entry.Known || state.Epoch != entry.State.Epoch || entry.State.Owner != m_transport.GetLocalPlayerId() || state.Tick <= entry.LastInput)
            return;
        entry.Input = state;
        entry.LastInput = state.Tick;
        return;
    }
    if (entry.Known && (state.Epoch < entry.State.Epoch || (state.Epoch == entry.State.Epoch && state.Sequence < entry.State.Sequence)))
        return;
    const bool newEpoch = !entry.Known || state.Epoch != entry.State.Epoch;
    if (newEpoch)
    {
        entry.Seed = state.Owner == m_transport.GetLocalPlayerId();
        entry.Input.reset();
        for (auto& job : m_jobs)
            if (job.Id == state.Reference)
                job = {};
    }
    entry.Previous = newEpoch ? state : entry.State;
    entry.State = state;
    entry.Known = true;
    entry.Received = GetTickCount64();
    entry.Next = 0;
}
void PlacedPhysicsService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsConnected() || !NativeActorPose::OnRunner() || VersionDb::Get().GetLoadedVersionString() != "1.7.104.0" ||
        m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return;
    if (m_generation != m_transport.GetConnectionGeneration())
    {
        OnDisconnected(DisconnectedEvent{});
        m_generation = m_transport.GetConnectionGeneration();
    }
    if (!m_world.GetPartyService().IsInParty() || m_world.GetPartyService().GetPartyMembers().size() != 2)
        return;
    const uint64_t now = GetTickCount64();
    Scope(now);
    Poll(now);
    if (!m_transport.IsConnected())
        return;
    const uint32_t held = NativeActorPose::GrabbedHandle();
    if (held)
    {
        ScopedReference ref(held);
        GameId id;
        if (ref && !ref.Get()->IsTemporary() && !Cast<Actor>(ref.Get()) && m_world.GetModSystem().GetServerModId(ref.Get()->formID, id) && !m_entries.contains(id))
            if (std::none_of(m_jobs.begin(), m_jobs.end(), [&](const auto& j) { return j.Handle == held && j.Cookie; }))
                Queue(id, held, Work::Probe);
    }
    auto it = m_entries.find(m_entryCursor);
    if (it != m_entries.end())
        ++it;
    if (it == m_entries.end())
        it = m_entries.begin();
    size_t queued{}, visited{};
    while (it != m_entries.end() && visited++ < 32 && queued < 2)
    {
        auto& item = it.value();
        const GameId id = it->first;
        m_entryCursor = id;
        ++it;
        if (item.State.Retired || now < item.Next || std::any_of(m_jobs.begin(), m_jobs.end(), [&](const auto& job) { return job.Cookie && job.Id == id; }))
            continue;
        const uint32_t form = m_world.GetModSystem().GetGameId(id);
        auto* ref = Cast<TESObjectREFR>(TESForm::GetById(form));
        if (!ref || !ref->GetNiNode() || ref->IsDeleted() || (ref->flags & TESForm::DISABLED))
            continue;
        item.Handle = ref->GetHandle().handle.iBits;
        if (!item.Known)
        {
            if (now >= item.Retry)
            {
                RequestPlacedPhysics observe;
                observe.Action = RequestPlacedPhysics::Observe;
                observe.State = item.State;
                m_transport.Send(observe);
                item.Retry = now + 1000;
            }
            continue;
        }
        const bool owner = item.State.Owner == m_transport.GetLocalPlayerId();
        bool ok{};
        if (owner && item.Seed)
            ok = Queue(id, item.Handle, Work::Seed, &item.State.Rig);
        else if (owner && item.Input && m_transport.GetClock().GetCurrentTick() <= item.Input->Tick + 200 && held != item.Handle)
        {
            ok = Queue(id, item.Handle, Work::Input, &item.Input->Rig);
            item.Input.reset();
        }
        else if (owner || held == item.Handle)
            ok = Queue(id, item.Handle, owner ? Work::Owner : Work::Held);
        else if (item.Received && now <= item.Received + 500)
        {
            const uint64_t tick = m_transport.GetClock().GetCurrentTick();
            RigidBodyRig pose;
            bool valid{};
            const auto& first = item.Previous;
            const auto& last = item.State;
            if (tick > last.Tick + 100 && tick <= last.Tick + 200)
                valid = RigidBodyRig::Predict(last.Rig, float(tick - last.Tick - 100) / 1000.f, pose);
            else if (tick <= last.Tick + 100)
                valid = RigidBodyRig::Blend(
                    first.Rig, last.Rig,
                    last.Tick > first.Tick
                        ? float(std::clamp(double(tick > 100 ? tick - 100 : 0) - first.Tick, 0., double(last.Tick - first.Tick)) / double(last.Tick - first.Tick))
                        : 1.f,
                    pose);
            if (valid)
                ok = Queue(id, item.Handle, Work::Remote, &pose);
        }
        if (ok)
        {
            item.Next = now + (owner || held == item.Handle ? 100 : 30);
            ++queued;
        }
    }
    Discover();
    for (size_t n = 0; n < 2 && !m_candidates.empty(); ++n)
    {
        const uint32_t handle = m_candidates.front();
        m_candidates.pop_front();
        ScopedReference ref(handle);
        GameId id;
        if (!ref || ref.Get()->IsTemporary() || Cast<Actor>(ref.Get()) || !ref.Get()->GetNiNode() || !m_world.GetModSystem().GetServerModId(ref.Get()->formID, id) ||
            m_entries.contains(id))
            continue;
        if (!Queue(id, handle, Work::Probe))
        {
            m_candidates.push_front(handle);
            break;
        }
    }
}
