#pragma once
#include "Message.h"
#include <Structs/QuestJournal.h>

struct NotifyCampaignCheckpoint final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCampaignCheckpoint;
    NotifyCampaignCheckpoint() : ServerMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    enum Phase : uint8_t { Begin, End, Accepted };
    uint8_t State{};
    TiltedPhoques::String WorldId;
    uint64_t Nonce{};
    uint64_t Revision{};
    uint32_t Count{};
    bool SeedOwner{};
    bool BaselinePending{};
    bool AllowMisc{};
    TiltedPhoques::Vector<GameId> MiscQuests;
    bool Decoded{true};
};
