#pragma once
#include "Message.h"
#include <limits>

struct NotifyQuestGrant final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyQuestGrant;
    static constexpr size_t kMaxBytes = 64 * 1024;
    NotifyQuestGrant() : ServerMessage(Opcode) {}
    uint64_t Nonce{};
    uint64_t AfterRevision{};
    uint64_t HeadRevision{};
    uint64_t ReceiptRevision{}; // zero and empty Payload means caught up
    TiltedPhoques::Vector<uint8_t> Payload;
    bool Decoded{true};
    bool IsValid() const noexcept
    {
        return Decoded && Nonce && AfterRevision <= HeadRevision && HeadRevision <= uint64_t(std::numeric_limits<int64_t>::max()) &&
            Payload.size() <= kMaxBytes && (ReceiptRevision ? ReceiptRevision > AfterRevision && ReceiptRevision <= HeadRevision && !Payload.empty() : Payload.empty());
    }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
