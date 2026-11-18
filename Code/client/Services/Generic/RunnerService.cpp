#include <TiltedOnlinePCH.h>

#include <Services/RunnerService.h>

#include <Events/UpdateEvent.h>
#include <Havok/PhysicsReplication.h>
#include <AI/NativeActorPose.h>
#include <Systems/AnimationSystem.h>

RunnerService::RunnerService(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
{
}

void RunnerService::Queue(std::function<void()> aFunctor) noexcept
{
    m_runner.Add(std::move(aFunctor));
}

void RunnerService::QueueGameplay(std::function<void()> aFunctor) noexcept
{
    const uint64_t generation = m_gameplayGeneration.load(std::memory_order_relaxed);
    m_runner.Add([this, generation, functor = std::move(aFunctor)]()
    {
        if (m_gameplayGeneration.load(std::memory_order_relaxed) == generation)
            functor();
    });
}

void RunnerService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    PhysicsReplication::BeginRunnerFrame();
    NativeActorPose::BeginRunnerFrame();
    AnimationSystem::BeginRunnerFrame();
    m_runner.Drain();
}
