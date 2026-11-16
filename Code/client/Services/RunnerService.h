#pragma once

#include <TiltedCore/TaskQueue.hpp>
#include <atomic>

struct UpdateEvent;

/**
 * @brief Dispatches events.
 */
struct RunnerService
{
    RunnerService(entt::dispatcher& aDispatcher) noexcept;
    ~RunnerService() noexcept = default;

    TP_NOCOPYMOVE(RunnerService);

    /**
     * @brief Executes all queued events every frame.
     */
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;

    /**
     * @brief Queues an event for OnUpdate() to execute.
     */
    template <class T> void Trigger(T acEvent)
    {
        QueueGameplay([event = std::move(acEvent), this]() { m_dispatcher.trigger(std::move(event)); });
    }

    /**
     * @brief Queues a lambda for OnUpdate() to execute.
     */
    void Queue(std::function<void()> aFunctor) noexcept;

    // Unlike UI/connect/cleanup work, a delayed gameplay event belongs to one
    // playable world. Authentication, disconnect and future rollback invalidate it.
    void QueueGameplay(std::function<void()> aFunctor) noexcept;
    void InvalidateGameplay() noexcept { m_gameplayGeneration.fetch_add(1, std::memory_order_relaxed); }

private:
    entt::dispatcher& m_dispatcher;
    TiltedPhoques::TaskQueue m_runner;
    std::atomic<uint64_t> m_gameplayGeneration{1};
};
