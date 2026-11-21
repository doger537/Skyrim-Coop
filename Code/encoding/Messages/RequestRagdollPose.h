#pragma once
#include "Message.h"
#include <Structs/RagdollSnapshot.h>
struct RequestRagdollPose final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestRagdollPose;
    RequestRagdollPose() : ClientMessage(Opcode) {}
    RagdollSnapshot State;
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { State.Serialize(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { State.Deserialize(aReader); }
};
