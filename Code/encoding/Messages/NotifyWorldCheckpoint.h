#pragma once
#include "Message.h"
#include <Structs/WorldCheckpoint.h>

struct NotifyWorldCheckpoint final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyWorldCheckpoint;
    enum class Phase : uint8_t
    {
        PrepareSave,
        CapturePhysics,
        SaveNow,
        Saved,
        PrepareLoad,
        LoadNow,
        AlignLoad,
        Resume,
        Cancelled
    };
    enum class Error : uint8_t
    {
        None,
        Busy,
        NeedTwoPlayers,
        NotLeader,
        DifferentCharacters,
        Files,
        Timeout,
        Membership,
        Storage
    };
    NotifyWorldCheckpoint()
        : ServerMessage(Opcode)
    {
    }
    Phase State{Phase::PrepareSave};
    Error Failure{Error::None};
    uint64_t Id{};
    uint64_t Nonce{};
    uint64_t PreviousEpoch{};
    String WorldId;
    TimeModel Calendar;
    std::array<uint8_t, 32> PhysicsDigest{};
    std::array<CharacterIdentity, 2> Members;
    WorldCheckpoint Manifest;
    bool Decoded{true};
    bool HasManifest() const noexcept { return State == Phase::Saved || State == Phase::PrepareLoad || State == Phase::LoadNow; }
    bool IsValid() const noexcept;
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
