#include <TiltedOnlinePCH.h>

#include <Systems/InterpolationSystem.h>
#include <Systems/MovementPlayback.h>
#include <Components.h>

#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>

#include <Games/References.h>
#include <World.h>

namespace
{
// Larger corrections snap instead of sliding the actor into place
constexpr float cMaxPositionCorrection = 256.f;
// Time constant of the correction fading out, ~95% of it is gone after three times this
constexpr double cPositionCorrectionTime = 50.0;
constexpr size_t cMaxTimePoints = 64;
} // namespace

void InterpolationSystem::Update(Actor* apActor, InterpolationComponent& aInterpolationComponent, const double aNow) noexcept
{
    auto& clock = aInterpolationComponent.Clock;
    const double cDeltaTime = clock.Advance(aNow);

    auto& movements = aInterpolationComponent.TimePoints;

    if (movements.size() < 2)
        return;

    const double cRenderTick = clock.GetRenderTick();

    while (movements.size() > 2)
    {
        const auto& second = *(++movements.begin());
        if (cRenderTick >= static_cast<double>(second.Tick))
            movements.pop_front();
        else
            break;
    }

    const auto& first = *(movements.begin());
    const auto& second = *(++movements.begin());

    const auto sample = MovementPlayback::Sample(first.Position, second.Position, first.Tick, second.Tick, cRenderTick);
    const auto& position = sample.Position;
    const float delta = sample.Delta;

    auto& positionError = aInterpolationComponent.PositionError;

    // The late snapshot arrived, so the actor is no longer where extrapolation put it
    if (aInterpolationComponent.HasNewTimePoint && aInterpolationComponent.IsExtrapolating)
        positionError = aInterpolationComponent.Position - position;

    if (sample.IsTeleport || !MovementPlayback::IsFinite(positionError) || glm::length(positionError) > cMaxPositionCorrection)
        positionError = {};
    else
        positionError *= static_cast<float>(std::exp(-cDeltaTime / cPositionCorrectionTime));

    aInterpolationComponent.HasNewTimePoint = false;
    aInterpolationComponent.IsExtrapolating = sample.IsExtrapolating;
    aInterpolationComponent.Position = position + positionError;

    // Don't try to move a null actor
    if (!apActor)
        return;

    apActor->ForcePosition(aInterpolationComponent.Position);
    apActor->LoadAnimationVariables(second.Variables);

    if (apActor->currentProcess && apActor->currentProcess->middleProcess)
    {
        apActor->currentProcess->middleProcess->direction = second.Direction;
    }

    auto rotA = first.Rotation;
    auto rotB = second.Rotation;

    const auto deltaX = TiltedPhoques::DeltaAngle(rotA.x, rotB.x, true) * delta;
    const auto deltaY = TiltedPhoques::DeltaAngle(rotA.y, rotB.y, true) * delta;
    const auto deltaZ = TiltedPhoques::DeltaAngle(rotA.z, rotB.z, true) * delta;

    auto finalX = TiltedPhoques::Mod(rotA.x + deltaX, float(TiltedPhoques::Pi * 2));
    if (finalX > 0.f && finalX > float(TiltedPhoques::Pi / 2))
        finalX -= TiltedPhoques::Pi * 2;

    const auto finalY = TiltedPhoques::Mod(rotA.y + deltaY, float(TiltedPhoques::Pi * 2));
    const auto finalZ = TiltedPhoques::Mod(rotA.z + deltaZ, float(TiltedPhoques::Pi * 2));

    apActor->SetRotation(finalX, finalY, finalZ);

    auto* pProcess = apActor->currentProcess;
    if (!pProcess || !pProcess->middleProcess)
        return;

    pProcess->middleProcess->pitch = -finalX;

    if (apActor->GetExtension()->IsRemotePlayer())
    {
        if (second.HasLookAt)
        {
            const NiPoint3 cTarget = apActor->GetLookingAtLocation() + second.LookOffset;
            pProcess->SetHeadTrackTarget(apActor, cTarget);
        }
        else
        {
            pProcess->middleProcess->lookAt = false;
        }
    }
}

void InterpolationSystem::AddPoint(InterpolationComponent& aInterpolationComponent, InterpolationComponent::TimePoint aPoint, const double aArrivalTime) noexcept
{
    // Validate before adding a timing sample or exposing values to the actor's animation graph.
    if (!MovementPlayback::IsFinite(aPoint.Position) || !MovementPlayback::IsFinite(aPoint.Rotation) || !std::isfinite(aPoint.Direction) ||
        (aPoint.HasLookAt && !MovementPlayback::IsFinite(aPoint.LookOffset)) ||
        !std::all_of(aPoint.Variables.Floats.begin(), aPoint.Variables.Floats.end(), [](float aValue) { return std::isfinite(aValue); }))
        return;

    if (!aInterpolationComponent.Clock.AddSample(aArrivalTime, aPoint.Tick))
        return;

    auto& timePoints = aInterpolationComponent.TimePoints;

    // Snapshots are unreliable and can arrive out of order
    auto itor = std::begin(timePoints);
    while (itor != std::end(timePoints) && itor->Tick < aPoint.Tick)
        ++itor;

    timePoints.insert(itor, std::move(aPoint));
    aInterpolationComponent.HasNewTimePoint = true;

    while (timePoints.size() > cMaxTimePoints)
        timePoints.pop_front();
}

uint64_t InterpolationSystem::GetPlaybackTick(const InterpolationComponent& acInterpolationComponent) noexcept
{
    // The render tick stays at 0 until the first snapshot arrives
    return static_cast<uint64_t>(std::max(acInterpolationComponent.Clock.GetRenderTick(), 0.0));
}

InterpolationComponent& InterpolationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    return aWorld.emplace_or_replace<InterpolationComponent>(aEntity);
}

void InterpolationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<InterpolationComponent>(aEntity))
        aWorld.remove<InterpolationComponent>(aEntity);
}
