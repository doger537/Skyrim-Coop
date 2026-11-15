#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

struct NotifyActorTeleport final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActorTeleport;

    NotifyActorTeleport()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyActorTeleport& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OwnershipEpoch == acRhs.OwnershipEpoch && FormId == acRhs.FormId &&
               WorldSpaceId == acRhs.WorldSpaceId && CellId == acRhs.CellId && Position == acRhs.Position;
    }

    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    GameId FormId{};
    GameId WorldSpaceId{};
    GameId CellId{};
    Vector3_NetQuantize Position{};
};
