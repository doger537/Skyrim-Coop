#include <TiltedOnlinePCH.h>
#include "PhysicsReplication.h"
#include <BSCore/BSAtomic.h>
#include <ScopedReference.h>
#include <TESObjectREFR.h>
#include <Actor.h>
#include <NetImmerse/NiNode.h>
#include <AI/NativeActorPose.h>
#include <Games/ActorExtension.h>
#include <Misc/BSFixedString.h>
#include <World.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace PhysicsReplication
{
namespace
{
// Local views of fields verified through Ghidra MCP, Skyrim PE 1.7.104.
// See physics-method-results, physics-transform-methods-results and
// deep-physics-results. They are never constructed or copied into game memory.
struct PhysicsWorldView
{
    uint8_t Unknown[0xC598];
    BSReadWriteLock Lock;
};
struct HavokWorldView
{
    uint8_t Unknown[0x430];
    PhysicsWorldView* Wrapper;
};
struct RigidBodyView
{
    uint8_t Unknown00[0x10];
    HavokWorldView* World;
    uint8_t Unknown18[0x160 - 0x18];
    uint8_t MotionType;
    uint8_t Unknown161[0x230 - 0x161];
    std::array<float, 4> LinearVelocity;
    std::array<float, 4> AngularVelocity;
};
struct BodyWrapperView
{
    uint8_t Unknown[0x10];
    RigidBodyView* Body;
};
struct CollisionView
{
    uint8_t Unknown[0x20];
    NiObject* Body;
};
struct SceneView
{
    uint8_t Unknown[0x40];
    NiObject* Collision;
};
static_assert(offsetof(PhysicsWorldView, Lock) == 0xC598);
static_assert(offsetof(HavokWorldView, Wrapper) == 0x430);
static_assert(offsetof(RigidBodyView, World) == 0x10);
static_assert(offsetof(RigidBodyView, MotionType) == 0x160);
static_assert(offsetof(RigidBodyView, LinearVelocity) == 0x230);
static_assert(offsetof(RigidBodyView, AngularVelocity) == 0x240);
static_assert(offsetof(BodyWrapperView, Body) == 0x10);
static_assert(offsetof(CollisionView, Body) == 0x20);
static_assert(offsetof(SceneView, Collision) == 0x40);
static_assert(offsetof(NiNode, children) == 0x110);
static_assert(offsetof(NiTObjectArray<NiAVObject*>, data) == 8);
static_assert(offsetof(NiTObjectArray<NiAVObject*>, unk1) == 0x12);

struct alignas(16) Vector4 { std::array<float, 4> Values{}; };
using Clock = std::chrono::steady_clock;
using TUnmark = void(PhysicsWorldView*);
TUnmark* RealUnmark{};
std::atomic<uint32_t> s_mainThread{};
std::atomic<bool> s_enabled{};
std::atomic<bool> s_pending{};
std::atomic<uint64_t> s_generation{1};
thread_local bool s_flushing{};
constexpr size_t kCapacity = 64;
constexpr size_t kMaxNodes = 256;
constexpr auto kFrameBudget = std::chrono::microseconds(500);
constexpr auto kLifetime = std::chrono::seconds(2);

struct Job
{
    Validator Validate{};
    Result Data;
    Clock::time_point Queued;
    uint8_t MotionType{4};
    bool Apply{};
    bool Active{};
    uint64_t Generation{};
};
std::array<Job, kCapacity> s_jobs{};
std::array<Result, kCapacity> s_results{};
constexpr size_t kRigCapacity = 8;
struct RigJob { RigResult Data; Clock::time_point Queued; uint64_t Generation{}; bool Apply{}; bool Active{}; };
std::array<RigJob, kRigCapacity> s_rigJobs{};
std::array<RigResult, kRigCapacity> s_rigResults{};
size_t s_rigHead{}, s_rigCount{}, s_nextRig{};
size_t s_resultHead{};
size_t s_resultCount{};
size_t s_nextJob{};
Clock::duration s_frameSpent{};

template <class T> T Virtual(void* apObject, size_t aSlot) noexcept
{
    return reinterpret_cast<T>((*reinterpret_cast<void***>(apObject))[aSlot]);
}

bool MainThread() noexcept { return s_mainThread.load() == GetCurrentThreadId(); }

bool ValidPose(const Pose& acPose) noexcept
{
    double norm{};
    for (float value : acPose.Rotation)
    {
        if (!std::isfinite(value))
            return false;
        norm += double(value) * value;
    }
    if (std::abs(norm - 1.0) > 0.01)
        return false;
    for (size_t i = 0; i < 3; ++i)
        if (!std::isfinite(acPose.Position[i]) || std::abs(acPose.Position[i]) > 1048575.f ||
            !std::isfinite(acPose.LinearVelocity[i]) || std::abs(acPose.LinearVelocity[i]) > 65536.f ||
            !std::isfinite(acPose.AngularVelocity[i]) || std::abs(acPose.AngularVelocity[i]) > 65536.f)
            return false;
    return true;
}

// MCP checked AsNode 03, AsBhkNiCollisionObject 12 and AsBhkRigidBody 15,
// including the default null implementations. Child arrays can contain holes.
bool SingleBody(NiAVObject* apRoot, NiAVObject*& aNode, BodyWrapperView*& aBody) noexcept
{
    std::array<NiAVObject*, kMaxNodes> stack{};
    size_t pending = 1;
    size_t visited{};
    stack[0] = apRoot;
    while (pending != 0)
    {
        if (++visited > kMaxNodes)
            return false;
        auto* object = stack[--pending];
        auto* collision = reinterpret_cast<SceneView*>(object)->Collision;
        if (collision)
        {
            using TCast = void*(void*);
            auto* view = static_cast<CollisionView*>(Virtual<TCast*>(collision, 0x12)(collision));
            if (view && view->Body)
            {
                auto* body = static_cast<BodyWrapperView*>(Virtual<TCast*>(view->Body, 0x15)(view->Body));
                if (body && body->Body)
                {
                    if (aBody && aBody != body)
                        return false;
                    aNode = object;
                    aBody = body;
                }
            }
        }
        using TAsNode = NiNode*(NiAVObject*);
        auto* node = Virtual<TAsNode*>(object, 3)(object);
        if (!node)
            continue;
        const auto& children = node->children;
        if (children.unk1 > children.length || children.unk1 > kMaxNodes || (children.unk1 && !children.data))
            return false;
        for (size_t i = 0; i < children.unk1; ++i)
            if (children.data[i])
            {
                if (pending == stack.size())
                    return false;
                stack[pending++] = children.data[i];
            }
    }
    return aBody != nullptr;
}

bool Execute(Job& aJob, PhysicsWorldView* apLockedWorld) noexcept
{
    if (aJob.Validate && !aJob.Validate(aJob.Data.ReferenceHandle, aJob.Data.Cookie)) { aJob.Data.State = Status::Expired; return true; }
    ScopedReference reference(aJob.Data.ReferenceHandle);
    if (!reference || reference.Get()->IsDeleted())
    {
        aJob.Data.State = Status::MissingReference;
        return true;
    }
    if (Cast<Actor>(reference.Get()))
    {
        aJob.Data.State = Status::UnsupportedBody;
        return true;
    }
    NiAVObject* root = reference.Get()->GetNiNode();
    if (!root)
    {
        aJob.Data.State = Status::NotLoaded;
        return true;
    }
    root->IncRef();
    struct Release { NiAVObject* Object; ~Release() { Object->DecRef(); } } release{root};
    NiAVObject* node{};
    BodyWrapperView* wrapper{};
    if (!SingleBody(root, node, wrapper))
    {
        aJob.Data.State = Status::UnsupportedBody;
        return true;
    }
    auto* body = wrapper->Body;
    if (!body->World || body->World->Wrapper != apLockedWorld)
        return false; // Keep pending until this body's own world is write-owned.
    aJob.Data.PreviousMotionType = body->MotionType;
    POINTER_SKYRIMSE(float, inverseScale, 187407);
    POINTER_SKYRIMSE(float, worldScale, 188105);
    if (body->MotionType < 1 || body->MotionType > 7)
    {
        aJob.Data.State = Status::UnsupportedBody;
        return true;
    }
    if (aJob.Apply)
    {
        if (body->MotionType == 5)
        {
            aJob.Data.State = Status::FixedBody;
            return true;
        }
        if (aJob.MotionType && body->MotionType != aJob.MotionType)
        {
            using TSetMotion = bool(NiAVObject*, uint32_t, bool, bool, bool);
            POINTER_SKYRIMSE(TSetMotion, setMotion, 77866);
            if (!setMotion.Get()(node, aJob.MotionType, false, true, false) || !wrapper->Body ||
                wrapper->Body->MotionType != aJob.MotionType)
            {
                aJob.Data.State = Status::UnsupportedBody;
                return true;
            }
        }
        const auto& pose = aJob.Data.Transform;
        const float scale = *worldScale.Get();
        Vector4 position{{pose.Position[0] * scale, pose.Position[1] * scale, pose.Position[2] * scale, 0.f}};
        Vector4 rotation{{pose.Rotation[1], pose.Rotation[2], pose.Rotation[3], pose.Rotation[0]}};
        using TSetPose = void(BodyWrapperView*, const Vector4*, const Vector4*);
        Virtual<TSetPose*>(wrapper, 0x37)(wrapper, &position, &rotation);
        Vector4 linear{{pose.LinearVelocity[0] * scale, pose.LinearVelocity[1] * scale, pose.LinearVelocity[2] * scale, 0.f}};
        Vector4 angular{{pose.AngularVelocity[0], pose.AngularVelocity[1], pose.AngularVelocity[2], 0.f}};
        using TSetVelocity = void(BodyWrapperView*, const Vector4*);
        POINTER_SKYRIMSE(TSetVelocity, setLinear, 78089);
        POINTER_SKYRIMSE(TSetVelocity, setAngular, 78090);
        setLinear.Get()(wrapper, &linear);
        setAngular.Get()(wrapper, &angular);
        aJob.Data.State = Status::Applied;
    }
    else
    {
        Vector4 position, rotation;
        using TGet = const Vector4*(BodyWrapperView*, Vector4*);
        const auto* actualPosition = Virtual<TGet*>(wrapper, 0x33)(wrapper, &position);
        const auto* actualRotation = Virtual<TGet*>(wrapper, 0x34)(wrapper, &rotation);
        if (!actualPosition || !actualRotation)
        {
            aJob.Data.State = Status::InvalidPose;
            return true;
        }
        auto& pose = aJob.Data.Transform;
        const float scale = *inverseScale.Get();
        for (size_t i = 0; i < 3; ++i)
        {
            pose.Position[i] = actualPosition->Values[i] * scale;
            pose.LinearVelocity[i] = body->LinearVelocity[i] * scale;
            pose.AngularVelocity[i] = body->AngularVelocity[i];
        }
        pose.Rotation = {actualRotation->Values[3], actualRotation->Values[0], actualRotation->Values[1], actualRotation->Values[2]};
        aJob.Data.CapturedAt = GetTickCount64();
        aJob.Data.State = ValidPose(pose) ? Status::Captured : Status::InvalidPose;
    }
    return true;
}

struct RigNode { uint64_t Key{}; NiAVObject* Node{}; BodyWrapperView* Wrapper{}; };
struct RigGraph { std::array<RigNode, RigidBodyRig::kMaxBodies> Bodies{}; size_t Count{}; };
bool CollectRig(NiAVObject* apRoot, RigGraph& aGraph) noexcept
{
    struct Node { NiAVObject* Object{}; uint64_t Parent{}; uint16_t Index{}; };
    std::array<Node, 512> stack{};
    size_t pending = 1, visited{};
    stack[0] = {apRoot, 14695981039346656037ull, 0};
    while (pending)
    {
        if (++visited > stack.size()) return false;
        const auto current = stack[--pending];
        uint64_t key = current.Parent;
        // MCP NiNode::GetByName 140EE23A0 -> 140EE0A40 compares Name at +10.
        const auto* name = reinterpret_cast<const BSFixedString*>(reinterpret_cast<const uint8_t*>(current.Object) + 0x10)->AsAscii();
        size_t length{};
        if (name) for (; length < 128 && name[length]; ++length) { key ^= static_cast<uint8_t>(name[length]); key *= 1099511628211ull; }
        if (length == 128) return false;
        if (!length) { key ^= current.Index; key *= 1099511628211ull; }
        key ^= 0xFF; key *= 1099511628211ull; // path separator
        auto* collision = reinterpret_cast<SceneView*>(current.Object)->Collision;
        if (collision)
        {
            using TCast = void*(void*);
            auto* view = static_cast<CollisionView*>(Virtual<TCast*>(collision, 0x12)(collision));
            auto* body = view && view->Body ? static_cast<BodyWrapperView*>(Virtual<TCast*>(view->Body, 0x15)(view->Body)) : nullptr;
            if (body && body->Body && std::none_of(aGraph.Bodies.begin(), aGraph.Bodies.begin() + aGraph.Count,
                [body](const auto& value) { return value.Wrapper == body; }))
            {
                if (!key || aGraph.Count == aGraph.Bodies.size()) return false;
                aGraph.Bodies[aGraph.Count++] = {key, current.Object, body};
            }
        }
        using TAsNode = NiNode*(NiAVObject*);
        auto* node = Virtual<TAsNode*>(current.Object, 3)(current.Object);
        if (!node) continue;
        const auto& children = node->children;
        if (children.unk1 > children.length || children.unk1 > stack.size() || (children.unk1 && !children.data)) return false;
        for (size_t i = 0; i < children.unk1; ++i) if (children.data[i])
        {
            if (pending == stack.size()) return false;
            stack[pending++] = {children.data[i], key, static_cast<uint16_t>(i)};
        }
    }
    std::sort(aGraph.Bodies.begin(), aGraph.Bodies.begin() + aGraph.Count, [](const auto& a, const auto& b) { return a.Key < b.Key; });
    for (size_t i = 1; i < aGraph.Count; ++i) if (aGraph.Bodies[i - 1].Key == aGraph.Bodies[i].Key) return false;
    return aGraph.Count > 0;
}
bool ReadBody(BodyWrapperView* apWrapper, Pose& aPose) noexcept
{
    Vector4 position, rotation;
    using TGet = const Vector4*(BodyWrapperView*, Vector4*);
    const auto* p = Virtual<TGet*>(apWrapper, 0x33)(apWrapper, &position);
    const auto* q = Virtual<TGet*>(apWrapper, 0x34)(apWrapper, &rotation);
    if (!p || !q) return false;
    POINTER_SKYRIMSE(float, inverseScale, 187407);
    const float scale = *inverseScale.Get();
    for (size_t i = 0; i < 3; ++i)
    {
        aPose.Position[i] = p->Values[i] * scale;
        aPose.LinearVelocity[i] = apWrapper->Body->LinearVelocity[i] * scale;
        aPose.AngularVelocity[i] = apWrapper->Body->AngularVelocity[i];
    }
    aPose.Rotation = {q->Values[3], q->Values[0], q->Values[1], q->Values[2]};
    return aPose.IsValid();
}
void WriteBody(BodyWrapperView* apWrapper, const Pose& acPose) noexcept
{
    POINTER_SKYRIMSE(float, worldScale, 188105);
    const float scale = *worldScale.Get();
    Vector4 position{{acPose.Position[0] * scale, acPose.Position[1] * scale, acPose.Position[2] * scale, 0.f}};
    Vector4 rotation{{acPose.Rotation[1], acPose.Rotation[2], acPose.Rotation[3], acPose.Rotation[0]}};
    using TSetPose = void(BodyWrapperView*, const Vector4*, const Vector4*);
    Virtual<TSetPose*>(apWrapper, 0x37)(apWrapper, &position, &rotation);
    Vector4 linear{{acPose.LinearVelocity[0] * scale, acPose.LinearVelocity[1] * scale, acPose.LinearVelocity[2] * scale, 0.f}};
    Vector4 angular{{acPose.AngularVelocity[0], acPose.AngularVelocity[1], acPose.AngularVelocity[2], 0.f}};
    using TSetVelocity = void(BodyWrapperView*, const Vector4*);
    POINTER_SKYRIMSE(TSetVelocity, setLinear, 78089);
    POINTER_SKYRIMSE(TSetVelocity, setAngular, 78090);
    setLinear.Get()(apWrapper, &linear); setAngular.Get()(apWrapper, &angular);
}
bool ExecuteRig(RigJob& aJob, PhysicsWorldView* apLockedWorld) noexcept
{
    auto& world = World::Get();
    const auto entity = static_cast<entt::entity>(aJob.Data.Entity);
    if (!world.valid(entity)) { aJob.Data.State = Status::Expired; return true; }
    const auto* form = world.try_get<FormIdComponent>(entity);
    const auto* remote = world.try_get<RemoteComponent>(entity);
    const auto* local = world.try_get<LocalComponent>(entity);
    const bool wantsRemote = aJob.Apply != aJob.Data.Interaction;
    if (!form || (wantsRemote ? !remote || remote->OwnershipEpoch != aJob.Data.OwnershipEpoch : !local || local->OwnershipEpoch != aJob.Data.OwnershipEpoch))
    { aJob.Data.State = Status::Expired; return true; }
    ScopedReference reference(aJob.Data.ReferenceHandle);
    auto* actor = reference ? Cast<Actor>(reference.Get()) : nullptr;
    if (!actor || actor->IsDeleted() || actor->formID != form->Id) { aJob.Data.State = Status::MissingReference; return true; }
    if (!NativeActorPose::IsRagdoll(actor) || actor->GetExtension()->IsRemote() != wantsRemote)
    { aJob.Data.State = Status::UnsupportedBody; return true; }
    if (aJob.Data.Interaction && (aJob.Apply ? NativeActorPose::GrabbedHandle() == aJob.Data.ReferenceHandle : NativeActorPose::GrabbedHandle() != aJob.Data.ReferenceHandle))
    { aJob.Data.State = Status::Expired; return true; }
    if (aJob.Apply && !aJob.Data.Interaction && NativeActorPose::GrabbedHandle() == aJob.Data.ReferenceHandle)
    { aJob.Data.State = Status::Expired; return true; }
    GameId cell, worldSpace;
    if (!actor->parentCell || !world.GetModSystem().GetServerModId(actor->parentCell->formID, cell) ||
        (actor->GetWorldSpace() && !world.GetModSystem().GetServerModId(actor->GetWorldSpace()->formID, worldSpace)))
    { aJob.Data.State = Status::NotLoaded; return true; }
    if (aJob.Apply && (cell != aJob.Data.Cell || worldSpace != aJob.Data.WorldSpace))
    { aJob.Data.State = Status::Expired; return true; }
    aJob.Data.Cell = cell; aJob.Data.WorldSpace = worldSpace;
    auto* root = actor->GetNiNode();
    if (!root) { aJob.Data.State = Status::NotLoaded; return true; }
    root->IncRef();
    struct Release { NiAVObject* Object; ~Release() { Object->DecRef(); } } release{root};
    RigGraph graph;
    if (!CollectRig(root, graph)) { aJob.Data.State = Status::UnsupportedBody; return true; }
    auto* firstWorld = graph.Bodies[0].Wrapper->Body->World;
    if (!firstWorld || firstWorld->Wrapper != apLockedWorld) return false;
    // Preflight ALL bodies before the first write. Mixed worlds, a changed
    // graph or a fixed body cannot leave half a corpse in the received pose.
    for (size_t i = 0; i < graph.Count; ++i)
    {
        const auto* body = graph.Bodies[i].Wrapper->Body;
        if (!body->World || body->World != firstWorld || body->MotionType < 1 || body->MotionType > 7 || body->MotionType == 5)
        { aJob.Data.State = Status::UnsupportedBody; return true; }
    }
    if (aJob.Apply)
    {
        if (graph.Count != aJob.Data.Rig.Count) { aJob.Data.State = Status::UnsupportedBody; return true; }
        for (size_t i = 0; i < graph.Count; ++i) if (graph.Bodies[i].Key != aJob.Data.Rig.Bodies[i].Key)
        { aJob.Data.State = Status::UnsupportedBody; return true; }
        for (size_t i = 0; i < graph.Count; ++i) WriteBody(graph.Bodies[i].Wrapper, aJob.Data.Rig.Bodies[i].Pose);
        aJob.Data.State = Status::Applied;
    }
    else
    {
        auto& rig = aJob.Data.Rig;
        rig.Count = static_cast<uint8_t>(graph.Count);
        for (size_t i = 0; i < graph.Count; ++i)
        {
            rig.Bodies[i].Key = graph.Bodies[i].Key;
            if (!ReadBody(graph.Bodies[i].Wrapper, rig.Bodies[i].Pose)) { aJob.Data.State = Status::InvalidPose; return true; }
        }
        aJob.Data.CapturedAt = GetTickCount64();
        aJob.Data.State = rig.IsValid() ? Status::Captured : Status::InvalidPose;
    }
    return true;
}

void HookUnmark(PhysicsWorldView* apWorld)
{
    // Never acquire/try/spin on an engine lock. Work only at the outermost
    // release of a write lock already held by the runner's engine thread.
    if (s_enabled.load() && s_pending.load() && MainThread() && !s_flushing && apWorld &&
        apWorld->Lock.uiWriterThread == GetCurrentThreadId() && apWorld->Lock.uiLock == 0x80000001u && s_frameSpent < kFrameBudget)
    {
        s_flushing = true;
        const auto start = Clock::now();
        // One complete rig per release; shares the existing soft frame budget.
        for (size_t count = 0; count < kRigCapacity && s_rigCount < kRigCapacity; ++count)
        {
            if (Clock::now() - start + s_frameSpent >= kFrameBudget) break;
            auto& job = s_rigJobs[s_nextRig]; s_nextRig = (s_nextRig + 1) % kRigCapacity;
            if (!job.Active) continue;
            bool done{};
            if (job.Generation != s_generation.load() || Clock::now() - job.Queued >= std::chrono::milliseconds(150)) { job.Data.State = Status::Expired; done = true; }
            else done = ExecuteRig(job, apWorld);
            if (done) { s_rigResults[(s_rigHead + s_rigCount++) % kRigCapacity] = job.Data; job.Active = false; }
            break;
        }
        for (size_t count = 0; count < kCapacity && s_resultCount < kCapacity; ++count)
        {
            if (Clock::now() - start + s_frameSpent >= kFrameBudget)
                break;
            auto& job = s_jobs[s_nextJob];
            s_nextJob = (s_nextJob + 1) % kCapacity;
            if (!job.Active)
                continue;
            bool done{};
            if (job.Generation != s_generation.load() || Clock::now() - job.Queued >= kLifetime)
            {
                job.Data.State = Status::Expired;
                done = true;
            }
            else
                done = Execute(job, apWorld);
            if (done)
            {
                s_results[(s_resultHead + s_resultCount++) % kCapacity] = job.Data;
                job.Active = false;
            }
        }
        s_frameSpent += Clock::now() - start;
        s_pending.store(std::any_of(s_jobs.begin(), s_jobs.end(), [](const auto& job) { return job.Active; }) ||
            std::any_of(s_rigJobs.begin(), s_rigJobs.end(), [](const auto& job) { return job.Active; }));
        s_flushing = false;
    }
    RealUnmark(apWorld);
}

bool Queue(uint32_t aHandle, uint64_t aCookie, const Pose& acPose, bool aApply, uint8_t aMotionType, Validator aValidate) noexcept
{
    if (!s_enabled.load() || !MainThread() || aHandle == 0 || aCookie == 0 ||
        (aApply && (!ValidPose(acPose) || aMotionType > 7)))
        return false;
    for (auto& job : s_jobs)
        if (!job.Active)
        {
            job = {};
            job.Validate = aValidate;
            job.Data.Cookie = aCookie;
            job.Data.ReferenceHandle = aHandle;
            job.Data.Transform = acPose;
            job.MotionType = aMotionType;
            job.Apply = aApply;
            job.Queued = Clock::now();
            job.Generation = s_generation.load();
            job.Active = true;
            s_pending.store(true);
            return true;
        }
    return false;
}
} // namespace

void BeginRunnerFrame() noexcept
{
    const uint32_t thread = GetCurrentThreadId();
    uint32_t expected{};
    if (!s_mainThread.compare_exchange_strong(expected, thread) && expected != thread)
    {
        s_enabled.store(false);
        return;
    }
    s_frameSpent = {};
}

bool QueueCapture(uint32_t aReferenceHandle, uint64_t aCookie, Validator aValidate) noexcept { return Queue(aReferenceHandle, aCookie, {}, false, 4, aValidate); }
bool QueueApply(uint32_t aReferenceHandle, uint64_t aCookie, const Pose& acPose, uint8_t aMotionType, Validator aValidate) noexcept
{
    return Queue(aReferenceHandle, aCookie, acPose, true, aMotionType, aValidate);
}
bool Poll(Result& aResult) noexcept
{
    if (!MainThread() || s_resultCount == 0)
        return false;
    aResult = s_results[s_resultHead];
    s_resultHead = (s_resultHead + 1) % kCapacity;
    --s_resultCount;
    return true;
}
namespace
{
bool QueueRig(uint32_t aHandle, uint64_t aCookie, const RigidBodyRig* apRig, uint32_t aEntity, uint32_t aEpoch, GameId aCell = {}, GameId aWorld = {}, bool aInteraction = false) noexcept
{
    if (!s_enabled.load() || !MainThread() || !aHandle || !aCookie || !aEpoch || (apRig && !apRig->IsValid())) return false;
    RigJob* slot{};
    for (auto& job : s_rigJobs)
    {
        if (apRig && job.Active && job.Apply && job.Data.ReferenceHandle == aHandle) { slot = &job; break; }
        if (!job.Active && !slot) slot = &job;
    }
    if (!slot) return false;
    *slot = {}; slot->Data.Cookie = aCookie; slot->Data.ReferenceHandle = aHandle;
    slot->Data.Entity = aEntity; slot->Data.OwnershipEpoch = aEpoch; slot->Generation = s_generation.load();
    slot->Data.Cell = aCell; slot->Data.WorldSpace = aWorld;
    slot->Data.Interaction = aInteraction;
    if (apRig) slot->Data.Rig = *apRig;
    slot->Apply = apRig != nullptr; slot->Active = true; slot->Queued = Clock::now();
    s_pending.store(true);
    return true;
}
}
bool QueueRigCapture(uint32_t aReferenceHandle, uint64_t aCookie, uint32_t aEntity, uint32_t aEpoch) noexcept { return QueueRig(aReferenceHandle, aCookie, nullptr, aEntity, aEpoch); }
bool QueueRigApply(uint32_t aReferenceHandle, uint64_t aCookie, const RigidBodyRig& acRig, uint32_t aEntity, uint32_t aEpoch, GameId aCell, GameId aWorld) noexcept { return QueueRig(aReferenceHandle, aCookie, &acRig, aEntity, aEpoch, aCell, aWorld); }
bool QueueRigInteractionCapture(uint32_t aReferenceHandle, uint64_t aCookie, uint32_t aEntity, uint32_t aEpoch) noexcept { return QueueRig(aReferenceHandle, aCookie, nullptr, aEntity, aEpoch, {}, {}, true); }
bool QueueRigAuthoritativeInput(uint32_t aReferenceHandle, uint64_t aCookie, const RigidBodyRig& acRig, uint32_t aEntity, uint32_t aEpoch, GameId aCell, GameId aWorld) noexcept { return QueueRig(aReferenceHandle, aCookie, &acRig, aEntity, aEpoch, aCell, aWorld, true); }
bool PollRig(RigResult& aResult) noexcept
{
    if (!MainThread() || !s_rigCount) return false;
    aResult = s_rigResults[s_rigHead]; s_rigHead = (s_rigHead + 1) % kRigCapacity; --s_rigCount;
    return true;
}
void Reset() noexcept
{
    Invalidate();
    if (!MainThread())
        return;
    for (auto& job : s_jobs)
        job.Active = false;
    for (auto& job : s_rigJobs) job.Active = false;
    s_rigHead = s_rigCount = s_nextRig = 0;
    s_resultHead = s_resultCount = s_nextJob = 0;
    s_pending.store(false);
}
void Invalidate() noexcept { s_generation.fetch_add(1, std::memory_order_release); }

static TiltedPhoques::Initializer s_physicsHooks([]()
{
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0")
        return;
    POINTER_SKYRIMSE(TUnmark, unmark, 77929);
    RealUnmark = unmark.Get();
    TP_HOOK(&RealUnmark, HookUnmark);
    s_enabled.store(true);
});
} // namespace PhysicsReplication
