#pragma once
#include "Message.h"
#include <Structs/WorldCheckpoint.h>

struct RequestWorldCheckpoint final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestWorldCheckpoint;
    enum class Kind : uint8_t { Save, Load, Prepared, Saved, Failed };
    RequestWorldCheckpoint() : ClientMessage(Opcode) {}
    Kind Action{Kind::Save};
    uint64_t Id{};
    uint64_t Nonce{};
    CheckpointPlayer Contribution;
    bool Decoded{true};
    bool IsValid() const noexcept;
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
