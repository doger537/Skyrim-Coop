#include <World.h>
#include <GameServer.h>
#include <Components.h>
#include <Services/PlacedPhysicsService.h>
#include <Services/WorldCheckpointService.h>
#include <Events/UpdateEvent.h>
#include <algorithm>
#include <limits>

PlacedPhysicsService::PlacedPhysicsService(World& w, entt::dispatcher& d) noexcept
    : m_world(w)
{
    m_packet = d.sink<PacketEvent<RequestPlacedPhysics>>().connect<&PlacedPhysicsService::OnPacket>(this);
    m_update = d.sink<UpdateEvent>().connect<&PlacedPhysicsService::OnUpdate>(this);
}
bool PlacedPhysicsService::Initialize() noexcept
{
    auto& campaign = m_world.GetCampaign();
    if (!campaign.IsReady())
        return false;
    if (m_epoch == campaign.WorldEpoch())
        return true;
    Vector<PlacedPhysicsState> states;
    if (!campaign.LoadPlacedPhysics(states))
    {
        GameServer::Get()->StopForWorldError(campaign.Error());
        return false;
    }
    m_records.clear();
    m_scopes.clear();
    m_epoch = campaign.WorldEpoch();
    for (auto& state : states)
    {
        m_nextEpoch = std::max(m_nextEpoch, state.Epoch);
        Record record;
        record.State = std::move(state);
        record.Received = 0;
        m_records.push_back(std::move(record));
    }
    return true;
}
bool PlacedPhysicsService::Covers(uint32_t player, GameId cell) const noexcept
{
    const uint64_t now = GameServer::Get()->GetTick();
    for (const auto& scope : m_scopes)
        if (scope.Player == player && now <= scope.At + 6000)
        {
            const auto* p = m_world.GetPlayerManager().GetById(player);
            return p && p->IsCampaignReady() && p->GetCampaignNonce() == scope.Admission && std::find(scope.Cells.begin(), scope.Cells.end(), cell) != scope.Cells.end();
        }
    return false;
}
Player* PlacedPhysicsService::Choose(GameId cell) noexcept
{
    Player* fallback{};
    for (auto* p : m_world.GetPlayerManager())
        if (Covers(p->GetId(), cell))
        {
            if (m_world.GetPartyService().IsPlayerLeader(p))
                return p;
            fallback = p;
        }
    return fallback;
}
void PlacedPhysicsService::Publish(const Record& record, bool reliable) noexcept
{
    NotifyPlacedPhysics message;
    message.Action = reliable ? NotifyPlacedPhysics::Authority : NotifyPlacedPhysics::Motion;
    message.State = record.State;
    for (auto* p : m_world.GetPlayerManager())
        if (p->IsCampaignReady() && Covers(p->GetId(), record.State.Cell))
            p->Send(message, reliable ? TiltedPhoques::kReliable : TiltedPhoques::kUnreliableNoNagle);
}
void PlacedPhysicsService::Reassign(Record& record, Player* owner) noexcept
{
    if (!owner)
    {
        record.Received = 0;
        return;
    }
    if (record.State.Owner == owner->GetId() && record.Received)
        return;
    if (m_nextEpoch == UINT64_MAX)
    {
        GameServer::Get()->StopForWorldError("Placed-physics ownership epoch exhausted");
        return;
    }
    record.State.Owner = owner->GetId();
    record.State.Epoch = ++m_nextEpoch;
    record.State.Sequence = 1;
    record.State.Tick = GameServer::Get()->GetTick();
    record.Received = record.State.Tick;
    record.Interactor = 0;
    record.InputSequence = 0;
    record.Dirty = true;
    Publish(record, true);
}
const PlacedPhysicsState* PlacedPhysicsService::Find(GameId id) const noexcept
{
    for (const auto& record : m_records)
        if (record.State.Reference == id)
            return &record.State;
    return nullptr;
}
bool PlacedPhysicsService::Authority(GameId id, uint32_t player, uint64_t epoch) const noexcept
{
    const auto* state = Find(id);
    return state && !state->Retired && state->Owner == player && state->Epoch == epoch;
}
void PlacedPhysicsService::OnPacket(const PacketEvent<RequestPlacedPhysics>& event) noexcept
{
    const auto& request = event.Packet;
    auto& p = *event.pPlayer;
    if (!request.IsValid() || !p.IsCampaignReady() || !p.GetCharacter() || m_world.GetPlayerManager().Count() != 2 || m_world.ctx().at<WorldCheckpointService>().IsFrozen() ||
        !Initialize())
        return;
    const auto* party = m_world.GetPartyService().GetPlayerParty(&p);
    if (!party || party->Members.size() != 2)
        return;
    const uint64_t now = GameServer::Get()->GetTick();
    if (request.Action == RequestPlacedPhysics::Coverage)
    {
        if (std::any_of(request.Cells.begin(), request.Cells.end(), [&](GameId id) { return !m_world.GetCampaign().ValidForm(id); }))
            return;
        auto scope = std::find_if(m_scopes.begin(), m_scopes.end(), [&](const auto& s) { return s.Player == p.GetId(); });
        if (scope == m_scopes.end())
        {
            m_scopes.push_back({p.GetId(), p.GetCampaignNonce(), now, request.Cells, 0});
        }
        else
        {
            if (scope->Cells != request.Cells || scope->Admission != p.GetCampaignNonce())
                scope->Seed = 0;
            scope->Admission = p.GetCampaignNonce();
            scope->Cells = request.Cells;
            scope->At = now;
        }
        return;
    }
    const auto& state = request.State;
    if (!Covers(p.GetId(), state.Cell) || !m_world.GetCampaign().ValidForm(state.Reference) || !m_world.GetCampaign().ValidForm(state.Cell) ||
        !m_world.GetCampaign().ValidForm(state.WorldSpace, true))
        return;
    auto found = std::find_if(m_records.begin(), m_records.end(), [&](const auto& record) { return record.State.Reference == state.Reference; });
    if (request.Action == RequestPlacedPhysics::Observe)
    {
        if (found == m_records.end())
        {
            if (m_records.size() >= PlacedPhysicsState::kMaxRecords)
            {
                GameServer::Get()->RejectCampaign(p.GetConnectionId(), "Loaded placed-physics registry capacity exceeded");
                return;
            }
            Record record;
            record.State = state;
            record.Received = 0;
            m_records.push_back(std::move(record));
            found = m_records.end() - 1;
        }
        // Only a server-selected simulation lease authorizes subsequent poses.
        // Prefer the host whenever its reported loaded scope includes the cell.
        Reassign(*found, Choose(found->State.Cell));
        NotifyPlacedPhysics reply;
        reply.Action = NotifyPlacedPhysics::Authority;
        reply.State = found->State;
        p.Send(reply);
        return;
    }
    if (found == m_records.end() || found->State.Retired || state.Epoch != found->State.Epoch || state.Owner != found->State.Owner)
        return;
    auto& current = *found;
    if (request.Action == RequestPlacedPhysics::Interaction)
    {
        const auto* move = m_world.try_get<MovementComponent>(*p.GetCharacter());
        auto* owner = m_world.GetPlayerManager().GetById(state.Owner);
        if (!owner || !owner->IsCampaignReady() || owner == &p || !move || !current.Received || now > current.Received + 1000 || !current.State.Rig.SameGraph(state.Rig) ||
            (current.Interactor && current.Interactor != p.GetId() && now < current.InteractionAt + 500) ||
            (current.Interactor == p.GetId() && (state.Sequence <= current.InputSequence || now < current.InteractionAt + 40)))
            return;
        double nearest = std::numeric_limits<double>::max();
        for (size_t i = 0; i < state.Rig.Count; ++i)
        {
            double distance{}, delta{}, speed{}, spin{};
            for (int j = 0; j < 3; ++j)
            {
                const double d = double(current.State.Rig.Bodies[i].Pose.Position[j]) - move->Position[j];
                distance += d * d;
                const double x = double(state.Rig.Bodies[i].Pose.Position[j]) - current.State.Rig.Bodies[i].Pose.Position[j];
                delta += x * x;
                const double v = state.Rig.Bodies[i].Pose.LinearVelocity[j], w = state.Rig.Bodies[i].Pose.AngularVelocity[j];
                speed += v * v;
                spin += w * w;
            }
            if (delta > 256. * 256. || speed > 4096. * 4096. || spin > 32. * 32.)
                return;
            nearest = std::min(nearest, distance);
        }
        if (nearest > 400. * 400.)
            return;
        current.Interactor = p.GetId();
        current.InputSequence = state.Sequence;
        current.InteractionAt = now;
        NotifyPlacedPhysics input;
        input.Action = NotifyPlacedPhysics::Interaction;
        input.State = state;
        input.State.Tick = now;
        owner->Send(input, TiltedPhoques::kUnreliableNoNagle);
        return;
    }
    if (state.Owner != p.GetId() || (state.Tick > now ? state.Tick - now > 1000 : now - state.Tick > 1000))
        return;
    if (request.Action == RequestPlacedPhysics::Gone)
    {
        current.State.Retired = true;
        current.Dirty = true;
        Publish(current, true);
        return;
    }
    if (state.Sequence <= current.State.Sequence || now < current.Received + 40)
        return;
    current.State = state;
    current.State.Tick = now;
    current.Received = now;
    current.Dirty = true;
    Publish(current, false);
}
void PlacedPhysicsService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_world.ctx().at<WorldCheckpointService>().IsFrozen() || !Initialize())
        return;
    std::erase_if(
        m_scopes,
        [&](const auto& s)
        {
            const auto* p = m_world.GetPlayerManager().GetById(s.Player);
            return !p || p->GetCampaignNonce() != s.Admission;
        });
    for (auto& scope : m_scopes)
    {
        auto* p = m_world.GetPlayerManager().GetById(scope.Player);
        if (!p)
            continue;
        size_t sent{}, visited{};
        while (scope.Seed < m_records.size() && sent < 16 && visited++ < 64)
        {
            const auto& record = m_records[scope.Seed++];
            if (Covers(p->GetId(), record.State.Cell))
            {
                NotifyPlacedPhysics message;
                message.Action = NotifyPlacedPhysics::Authority;
                message.State = record.State;
                p->Send(message);
                ++sent;
            }
        }
    }
    // Reassignment is amortized too; no all-world physics work on one tick.
    for (size_t n = 0; n < 16 && !m_records.empty(); ++n)
    {
        auto& record = m_records[m_cursor++ % m_records.size()];
        if (!record.State.Retired)
            Reassign(record, Choose(record.State.Cell));
    }
}
bool PlacedPhysicsService::Flush() noexcept
{
    if (!Initialize())
        return false;
    Vector<PlacedPhysicsState> dirty;
    for (const auto& record : m_records)
        if (record.Dirty)
            dirty.push_back(record.State);
    if (!m_world.GetCampaign().SavePlacedPhysics(dirty))
    {
        GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error());
        return false;
    }
    for (auto& record : m_records)
        record.Dirty = false;
    return true;
}
bool PlacedPhysicsService::CommitPhysics(const PhysicsFrame& frame) noexcept
{
    if (!Initialize())
        return false;
    for (const auto& entry : frame.Entries)
        if (entry.Type == PhysicsFrameEntry::Placed)
        {
            auto found = std::find_if(m_records.begin(), m_records.end(), [&](const auto& record) { return record.State.Reference == entry.Reference; });
            if (found == m_records.end())
            {
                if (m_records.size() >= PlacedPhysicsState::kMaxRecords || m_nextEpoch == UINT64_MAX)
                    return false;
                Record record;
                record.State.Reference = entry.Reference;
                record.State.Epoch = ++m_nextEpoch;
                record.State.Sequence = 1;
                m_records.push_back(std::move(record));
                found = m_records.end() - 1;
            }
            auto& state = found->State;
            state.Cell = entry.Cell;
            state.WorldSpace = entry.WorldSpace;
            state.Rig = entry.Rig;
            state.Retired = false;
            if (!state.Owner)
            {
                for (auto* p : m_world.GetPlayerManager())
                    if (p->IsCampaignReady() && m_world.GetPartyService().IsPlayerLeader(p))
                        state.Owner = p->GetId();
                if (!state.Owner)
                    return false;
            }
            if (state.Sequence == UINT64_MAX)
                return false;
            ++state.Sequence;
            state.Tick = GameServer::Get()->GetTick();
            for (size_t i = 0; i < state.Rig.Count; ++i)
                for (int j = 0; j < 3; ++j)
                {
                    state.Rig.Bodies[i].Pose.Position[j] *= frame.NativeToWorld;
                    state.Rig.Bodies[i].Pose.LinearVelocity[j] *= frame.NativeToWorld;
                }
            if (!state.IsValid())
                return false;
            found->Dirty = true;
        }
    return Flush();
}
