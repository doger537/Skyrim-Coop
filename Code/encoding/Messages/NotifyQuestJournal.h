#pragma once
#include "Message.h"
#include <Structs/QuestJournal.h>

struct NotifyQuestJournal final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyQuestJournal;
    NotifyQuestJournal() : ServerMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    uint64_t Nonce{};
    uint64_t Revision{};
    QuestJournal Data;
    TiltedPhoques::Vector<uint16_t> ReplayStages;
    bool ReplayLifecycle{};
};
