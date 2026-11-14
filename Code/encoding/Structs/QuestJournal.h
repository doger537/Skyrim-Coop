#pragma once
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <Structs/GameId.h>

// Shared campaign data, independent of the engine and the SQLite store.
struct QuestJournal
{
    static constexpr size_t kMaxEvents = 4096;
    struct Event
    {
        uint8_t Status{};
        uint16_t Stage{};
        bool Bootstrap{}; // imported completed-stage set; its original script order is unknown
        bool operator==(const Event&) const noexcept = default;
    };
    struct Decision
    {
        TiltedPhoques::String Group;
        uint16_t Stage{};
        bool operator==(const Decision&) const noexcept = default;
    };
    GameId Id{};
    uint32_t Epoch{1};
    uint8_t Type{};
    bool Started{};
    bool Stopped{};
    TiltedPhoques::Vector<Event> Events;
    TiltedPhoques::Vector<Decision> Decisions;
    bool Decoded{true};

    bool operator==(const QuestJournal&) const noexcept = default;
    bool IsValid() const noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
