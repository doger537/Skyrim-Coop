#pragma once
#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

struct NotifyPartyLocations final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyLocations;
    static constexpr size_t kMaxPlayers = 8;
    static constexpr uint64_t kMaxAgeMs = 5000;
    struct Location
    {
        uint32_t PlayerId{};
        uint32_t ServerId{};
        uint32_t OwnershipEpoch{};
        uint64_t ReceivedTick{}; // server's last accepted observation, not a client-supplied time
        GameId Cell{};
        GameId WorldSpace{};
        Vector3_NetQuantize Position{};
        bool IsValid(uint64_t aSnapshotTick) const noexcept;
    };
    NotifyPartyLocations() : ServerMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    bool IsValid() const noexcept;
    uint32_t LeaderPlayerId{};
    uint64_t Sequence{};
    uint64_t Tick{};
    TiltedPhoques::Vector<Location> Players;
    bool Decoded{true};
};
