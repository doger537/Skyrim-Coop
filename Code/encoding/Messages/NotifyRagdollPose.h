#pragma once
#include "Message.h"
#include <Structs/RagdollSnapshot.h>
struct NotifyRagdollPose final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyRagdollPose;
    NotifyRagdollPose() : ServerMessage(Opcode) {}
    RagdollSnapshot State;
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { State.Serialize(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { State.Deserialize(aReader); }
};
