#pragma once
#include "Message.h"
#include <limits>

// Read-only inbox query. A client can never submit item roles or mint a grant.
struct RequestQuestGrants final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestQuestGrants;
    RequestQuestGrants() : ClientMessage(Opcode) {}
    uint64_t Nonce{};
    uint64_t AfterRevision{};
    bool CursorAvailable{};
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Nonce && AfterRevision <= uint64_t(std::numeric_limits<int64_t>::max()) && (CursorAvailable || !AfterRevision); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
