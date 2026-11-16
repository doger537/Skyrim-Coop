#pragma once
#include <Structs/WorldItemState.h>
#include <TiltedCore/Stl.hpp>
#include <array>

// Receiver-side motion history. It stores no inventory vectors or game pointers.
// Only reliable lifecycle packets can introduce an ID or retire it.
class WorldItemTimeline
{
public:
    static constexpr size_t kMaxItems = 8192;
    static constexpr size_t kHistory = 16;
    static constexpr uint64_t kMaxExtrapolationMs = 100;
    enum class Result { Created, Applied, Removed, Duplicate, Unknown, Stale, Invalid, Capacity };

    void Reset(uint64_t aWorldEpoch, uint64_t aHostEpoch) noexcept;
    Result Lifecycle(const WorldItemState& acState) noexcept;
    Result Motion(const WorldItemMotion& acMotion) noexcept;
    bool Sample(uint64_t aId, uint64_t aRenderTick, WorldItemMotion& aMotion) const noexcept;
    const WorldItemMotion* Latest(uint64_t aId) const noexcept;
    size_t Size() const noexcept { return m_items.size(); }

private:
    struct Track
    {
        WorldItemMotion Current;
        std::array<WorldItemMotion, kHistory> Samples;
        size_t Start{};
        size_t Count{};
        bool Deleted{};
        const WorldItemMotion& At(size_t aIndex) const noexcept { return Samples[(Start + aIndex) % kHistory]; }
        void Rebase(const WorldItemMotion& acMotion) noexcept;
        void Append(const WorldItemMotion& acMotion) noexcept;
    };
    bool CurrentEpoch(const WorldItemMotion& acMotion) const noexcept;
    uint64_t m_worldEpoch{};
    uint64_t m_hostEpoch{};
    TiltedPhoques::Map<uint64_t, Track> m_items;
};
