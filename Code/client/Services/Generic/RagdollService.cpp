#include <Services/RagdollService.h>
#include <Services/WorldCheckpointService.h>
#include <World.h>
#include <Components.h>
#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Messages/RequestRagdollPose.h>
#include <Messages/NotifyRagdollPose.h>
#include <Havok/PhysicsReplication.h>
#include <AI/NativeActorPose.h>
#include <Systems/OwnershipSystem.h>
#include <Actor.h>
#include <ScopedReference.h>
#include <limits>
#include <cmath>

RagdollService::RagdollService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    m_poseConnection = aDispatcher.sink<NotifyRagdollPose>().connect<&RagdollService::OnPose>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&RagdollService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&RagdollService::OnDisconnected>(this);
}
uint64_t RagdollService::Cookie() noexcept { return m_cookie == std::numeric_limits<uint64_t>::max() ? 0 : ++m_cookie; }
void RagdollService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_world.clear<RagdollComponent>();
    m_captureCursor = m_applyCursor = m_inputCursor = entt::null;
    PhysicsReplication::Reset();
}
void RagdollService::OnPose(const NotifyRagdollPose& acMessage) noexcept
{
    const auto& state = acMessage.State;
    if (!m_transport.IsConnected() || !state.IsValid() || m_world.ctx().at<WorldCheckpointService>().IsFrozen() ||
        m_transport.GetLastPollTime() - m_transport.GetMessageReceiveTime() > 500.) return;
    if (state.Interaction)
    {
        auto localView = m_world.view<LocalComponent, FormIdComponent>();
        for (const auto entity : localView)
        {
            const auto& local = localView.get<LocalComponent>(entity);
            if (local.Id != state.ServerId || local.OwnershipEpoch != state.OwnershipEpoch) continue;
            auto& rig = m_world.get_or_emplace<RagdollComponent>(entity);
            if (rig.OwnershipEpoch != local.OwnershipEpoch) { rig = {}; rig.OwnershipEpoch = local.OwnershipEpoch; }
            if (state.Tick <= rig.InputTick) return;
            rig.Input = state; rig.InputAt = GetTickCount64(); rig.InputTick = state.Tick;
            return;
        }
        return;
    }
    const auto entity = OwnershipSystem::AcceptRemoteUpdate(m_world, state.ServerId, state.OwnershipEpoch);
    if (!entity) return;
    auto& rig = m_world.get_or_emplace<RagdollComponent>(*entity);
    if (rig.OwnershipEpoch != state.OwnershipEpoch) rig = {};
    if (rig.Sequence && (state.Sequence <= rig.Sequence || state.Tick <= rig.Tick)) return;
    // A changed body graph starts a fresh interpolation history, never a blend
    // between unrelated skeletons. The native adapter validates the local graph.
    if (rig.Count && !rig.Frames[rig.Count - 1].Rig.SameGraph(state.Rig)) { rig.Count = 0; rig.Clock = {}; }
    if (!rig.Clock.AddSample(m_transport.GetMessageReceiveTime(), state.Tick)) return;
    if (rig.Count == rig.Frames.size()) { for (size_t i = 1; i < rig.Count; ++i) rig.Frames[i - 1] = rig.Frames[i]; --rig.Count; }
    rig.Frames[rig.Count++] = state;
    rig.OwnershipEpoch = state.OwnershipEpoch; rig.Sequence = state.Sequence; rig.Tick = state.Tick; rig.ReceivedAt = GetTickCount64();
}
void RagdollService::Poll() noexcept
{
    PhysicsReplication::RigResult result;
    while (PhysicsReplication::PollRig(result))
    {
        const auto entity = static_cast<entt::entity>(result.Entity);
        if (!m_world.valid(entity)) continue;
        auto* rig = m_world.try_get<RagdollComponent>(entity);
        if (!rig || rig->OwnershipEpoch != result.OwnershipEpoch) continue;
        const uint64_t now = GetTickCount64();
        if (rig->ApplyCookie == result.Cookie)
        {
            rig->ApplyCookie = 0;
            if (result.State == PhysicsReplication::Status::UnsupportedBody) rig->NextApply = now + 1000;
            continue;
        }
        if (rig->CaptureCookie != result.Cookie) continue;
        rig->CaptureCookie = 0;
        const auto* local = m_world.try_get<LocalComponent>(entity);
        const auto* remote = m_world.try_get<RemoteComponent>(entity);
        if (result.Interaction ? !remote || remote->OwnershipEpoch != result.OwnershipEpoch : !local || local->OwnershipEpoch != result.OwnershipEpoch) continue;
        if (result.State != PhysicsReplication::Status::Captured)
        { rig->NextCapture = now + (result.State == PhysicsReplication::Status::UnsupportedBody ? 1000 : 100); continue; }
        const uint64_t elapsed = now >= result.CapturedAt ? now - result.CapturedAt : 1000;
        const uint64_t tick = m_transport.GetClock().GetCurrentTick();
        if (elapsed > 150 || tick <= elapsed || m_sequence == std::numeric_limits<uint64_t>::max()) continue;
        RequestRagdollPose message;
        message.State.ServerId = result.Interaction ? remote->Id : local->Id; message.State.OwnershipEpoch = result.OwnershipEpoch;
        message.State.Interaction = result.Interaction;
        message.State.Sequence = ++m_sequence; message.State.Tick = tick - elapsed;
        message.State.Cell = result.Cell; message.State.WorldSpace = result.WorldSpace; message.State.Rig = result.Rig;
        m_transport.Send(message, TiltedPhoques::kUnreliableNoNagle);
    }
}
void RagdollService::Capture(uint64_t aNow) noexcept
{
    auto view = m_world.view<LocalComponent, FormIdComponent>();
    if (view.begin() == view.end()) return;
    auto it = view.find(m_captureCursor);
    if (it != view.end()) ++it;
    if (it == view.end()) it = view.begin();
    const auto start = it;
    size_t queued{};
    for (size_t visited = 0; visited < 24; ++visited)
    {
        const auto entity = *it; m_captureCursor = entity;
        const auto& local = view.get<LocalComponent>(entity);
        auto* actor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (actor && NativeActorPose::IsRagdoll(actor))
        {
            auto& rig = m_world.get_or_emplace<RagdollComponent>(entity);
            if (rig.OwnershipEpoch != local.OwnershipEpoch) { rig = {}; rig.OwnershipEpoch = local.OwnershipEpoch; }
            if (rig.CaptureCookie && aNow > rig.CaptureDeadline) rig.CaptureCookie = 0;
            if (!rig.CaptureCookie && !rig.ApplyCookie && aNow >= rig.NextCapture)
            {
                const uint64_t cookie = Cookie();
                if (cookie && PhysicsReplication::QueueRigCapture(actor->GetHandle().handle.iBits, cookie, static_cast<uint32_t>(entity), local.OwnershipEpoch))
                { rig.CaptureCookie = cookie; rig.CaptureDeadline = aNow + 300; rig.NextCapture = aNow + 100; ++queued; }
            }
        }
        if (queued == 2) break;
        if (++it == view.end()) it = view.begin();
        if (it == start) break;
    }
}
void RagdollService::CaptureHeld(uint64_t aNow) noexcept
{
    const uint32_t handle = NativeActorPose::GrabbedHandle();
    if (!handle) return;
    ScopedReference grabbed(handle);
    auto* actor = grabbed ? Cast<Actor>(grabbed.Get()) : nullptr;
    if (!actor || !NativeActorPose::IsRagdoll(actor)) return;
    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    for (const auto entity : view)
    {
        if (view.get<FormIdComponent>(entity).Id != actor->formID) continue;
        const auto& remote = view.get<RemoteComponent>(entity);
        auto& rig = m_world.get_or_emplace<RagdollComponent>(entity);
        if (rig.OwnershipEpoch != remote.OwnershipEpoch) { rig = {}; rig.OwnershipEpoch = remote.OwnershipEpoch; }
        if (rig.CaptureCookie && aNow > rig.CaptureDeadline) rig.CaptureCookie = 0;
        if (!rig.CaptureCookie && aNow >= rig.NextCapture)
        {
            const uint64_t cookie = Cookie();
            if (cookie && PhysicsReplication::QueueRigInteractionCapture(handle, cookie, static_cast<uint32_t>(entity), remote.OwnershipEpoch))
            { rig.CaptureCookie = cookie; rig.CaptureDeadline = aNow + 300; rig.NextCapture = aNow + 100; }
        }
        return;
    }
}
void RagdollService::ApplyInputs(uint64_t aNow) noexcept
{
    auto view = m_world.view<LocalComponent, RagdollComponent, FormIdComponent>();
    if (view.begin() == view.end()) return;
    auto it = view.find(m_inputCursor);
    if (it != view.end()) ++it;
    if (it == view.end()) it = view.begin();
    const auto start = it;
    size_t queued{};
    for (size_t visited = 0; visited < 24; ++visited)
    {
        const auto entity = *it; m_inputCursor = entity;
        auto& rig = view.get<RagdollComponent>(entity);
        if (rig.ApplyCookie && aNow > rig.ApplyDeadline) rig.ApplyCookie = 0;
        if (rig.Input && !rig.ApplyCookie)
        {
            const auto state = *rig.Input; rig.Input.reset();
            const auto& local = view.get<LocalComponent>(entity);
            if (aNow <= rig.InputAt + 150 && local.OwnershipEpoch == state.OwnershipEpoch)
            {
                auto* actor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
                const uint64_t cookie = Cookie();
                if (actor && cookie && PhysicsReplication::QueueRigAuthoritativeInput(actor->GetHandle().handle.iBits, cookie, state.Rig,
                    static_cast<uint32_t>(entity), local.OwnershipEpoch, state.Cell, state.WorldSpace))
                { rig.ApplyCookie = cookie; rig.ApplyDeadline = aNow + 300; if (++queued == 2) break; }
            }
        }
        if (++it == view.end()) it = view.begin();
        if (it == start) break;
    }
}
void RagdollService::Apply(uint64_t aNow) noexcept
{
    auto view = m_world.view<RemoteComponent, RagdollComponent, FormIdComponent>();
    if (view.begin() == view.end()) return;
    auto it = view.find(m_applyCursor);
    if (it != view.end()) ++it;
    if (it == view.end()) it = view.begin();
    const auto start = it;
    size_t queued{};
    for (size_t visited = 0; visited < 24; ++visited)
    {
        const auto entity = *it; m_applyCursor = entity;
        auto& rig = view.get<RagdollComponent>(entity);
        const auto& remote = view.get<RemoteComponent>(entity);
        if (rig.ApplyCookie && aNow > rig.ApplyDeadline) rig.ApplyCookie = 0;
        if (rig.Count && remote.OwnershipEpoch == rig.OwnershipEpoch && aNow <= rig.ReceivedAt + 500 && !rig.ApplyCookie && aNow >= rig.NextApply)
        {
            rig.Clock.Advance(m_transport.GetLastPollTime());
            const auto* interpolation = m_world.try_get<InterpolationComponent>(entity);
            const double tick = interpolation && interpolation->Clock.IsRunning() ? interpolation->Clock.GetRenderTick() : rig.Clock.GetRenderTick();
            while (rig.Count > 2 && tick >= double(rig.Frames[1].Tick)) { for (size_t i = 1; i < rig.Count; ++i) rig.Frames[i - 1] = rig.Frames[i]; --rig.Count; }
            const auto& first = rig.Frames[0];
            const auto& second = rig.Frames[rig.Count > 1 ? 1 : 0];
            const bool sameCell = first.Cell == second.Cell && first.WorldSpace == second.WorldSpace;
            const float t = second.Tick > first.Tick ? static_cast<float>(std::clamp((tick - double(first.Tick)) / double(second.Tick - first.Tick), 0., 1.)) : 1.f;
            RigidBodyRig pose;
            bool blend = sameCell && RigidBodyRig::Blend(first.Rig, second.Rig, t, pose);
            if (tick > double(second.Tick))
            {
                const double late = (tick - double(second.Tick)) / 1000.;
                // After the bounded prediction window, leave the native solver
                // running with its last velocity; do not pin to an old pose.
                if (late > .1) { if (++it == view.end()) it = view.begin(); if (it == start) break; continue; }
                blend = RigidBodyRig::Predict(second.Rig, static_cast<float>(late), pose);
            }
            const auto& location = tick < double(second.Tick) ? first : second;
            auto* actor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
            const uint64_t cookie = Cookie();
            if (actor && cookie && NativeActorPose::GrabbedHandle() != actor->GetHandle().handle.iBits && NativeActorPose::IsRagdoll(actor) && PhysicsReplication::QueueRigApply(actor->GetHandle().handle.iBits, cookie,
                blend ? pose : location.Rig, static_cast<uint32_t>(entity), remote.OwnershipEpoch, location.Cell, location.WorldSpace))
            { rig.ApplyCookie = cookie; rig.ApplyDeadline = aNow + 300; rig.NextApply = aNow + 30; ++queued; }
        }
        if (queued == 2) break;
        if (++it == view.end()) it = view.begin();
        if (it == start) break;
    }
}
void RagdollService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsConnected() || m_world.ctx().at<WorldCheckpointService>().IsFrozen()) return;
    const uint64_t generation = m_transport.GetConnectionGeneration();
    if (m_generation != generation) { OnDisconnected(DisconnectedEvent{}); m_generation = generation; }
    Poll();
    const uint64_t now = GetTickCount64();
    ApplyInputs(now); Capture(now); CaptureHeld(now); Apply(now);
}
