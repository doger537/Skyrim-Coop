#pragma once

#include "Message.h"
#include <Structs/Inventory.h>

struct RequestInventoryChanges final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestInventoryChanges;

    RequestInventoryChanges()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestInventoryChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestInventoryChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OperationId == acRhs.OperationId && OwnershipEpoch == acRhs.OwnershipEpoch && Item == acRhs.Item &&
               Drop == acRhs.Drop && UpdateClients == acRhs.UpdateClients;
    }

    uint32_t ServerId{};
    uint64_t OperationId{};
    uint32_t OwnershipEpoch{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
};
