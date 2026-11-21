#pragma once

#include "QuestJournal.h"

struct LocalQuestState
{
    bool Exists{};
    bool Stopped{};
    TiltedPhoques::Vector<uint16_t> Reached;

    bool operator==(const LocalQuestState&) const noexcept = default;
};

enum class QuestAdmissionProblem : uint8_t
{
    None,
    MissingOrUnsupported,
    LocalOnly,
    Unmapped,
    AcceptedRadiant,
    Decision,
    LocalProgress,
    Lifecycle,
    UnapprovedStart,
    UnapprovedStage,
    UnapprovedStop,
    ChangedDuringAdmission
};

struct QuestAdmissionDiff
{
    static constexpr uint32_t kMaxServerDetails = 64;
    static constexpr size_t kMaxEditorId = 96;

    GameId Id;
    uint32_t LocalForm{};
    uint8_t Type{};
    TiltedPhoques::String EditorId;
    LocalQuestState Local;
    QuestAdmissionProblem Problem{};
    bool Decoded{true};

    bool IsValid() const noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    bool operator==(const QuestAdmissionDiff&) const noexcept = default;
};

// The server supplies its own snapshot. No journal or server revision is trusted
// from the client's observation.
TiltedPhoques::String DescribeQuestAdmission(const QuestAdmissionDiff& acDiff, const QuestJournal* apServer) noexcept;
