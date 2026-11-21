#include "QuestAdmissionDiff.h"
#include "QuestSyncPolicy.h"
#include <TiltedCore/Serialization.hpp>
#include <algorithm>
#include <cstdio>
#include <iterator>

using TiltedPhoques::Serialization;
using TiltedPhoques::String;
using TiltedPhoques::Vector;

bool QuestAdmissionDiff::IsValid() const noexcept
{
    return Decoded && Type <= 11 && EditorId.size() <= kMaxEditorId && Local.Reached.size() <= QuestJournal::kMaxEvents &&
           (Local.Exists || Local.Reached.empty()) && Problem > QuestAdmissionProblem::None && Problem <= QuestAdmissionProblem::ChangedDuringAdmission;
}

void QuestAdmissionDiff::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, IsValid());
    if (!IsValid())
        return;
    Id.Serialize(aWriter);
    aWriter.WriteBits(LocalForm, 32);
    aWriter.WriteBits(Type, 8);
    aWriter.WriteBits(static_cast<uint8_t>(Problem), 8);
    aWriter.WriteBits(EditorId.size(), 7);
    for (unsigned char c : EditorId)
        aWriter.WriteBits(c, 8);
    Serialization::WriteBool(aWriter, Local.Exists);
    Serialization::WriteBool(aWriter, Local.Stopped);
    aWriter.WriteBits(Local.Reached.size(), 13);
    for (uint16_t stage : Local.Reached)
        aWriter.WriteBits(stage, 16);
}

void QuestAdmissionDiff::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    Decoded = Serialization::ReadBool(aReader);
    if (!Decoded)
        return;
    Id.Deserialize(aReader);
    uint64_t data{};
    aReader.ReadBits(data, 32);
    LocalForm = static_cast<uint32_t>(data);
    aReader.ReadBits(data, 8);
    Type = static_cast<uint8_t>(data);
    aReader.ReadBits(data, 8);
    Problem = static_cast<QuestAdmissionProblem>(data);
    aReader.ReadBits(data, 7);
    if (data > kMaxEditorId)
    {
        Decoded = false;
        return;
    }
    EditorId.resize(data);
    for (char& c : EditorId)
    {
        aReader.ReadBits(data, 8);
        c = static_cast<char>(data);
    }
    Local.Exists = Serialization::ReadBool(aReader);
    Local.Stopped = Serialization::ReadBool(aReader);
    aReader.ReadBits(data, 13);
    if (data > QuestJournal::kMaxEvents)
    {
        Decoded = false;
        return;
    }
    Local.Reached.resize(data);
    for (uint16_t& stage : Local.Reached)
    {
        aReader.ReadBits(data, 16);
        stage = static_cast<uint16_t>(data);
    }
    Decoded = IsValid();
}

namespace
{
void SortStages(Vector<uint16_t>& aStages)
{
    std::sort(aStages.begin(), aStages.end());
    aStages.erase(std::unique(aStages.begin(), aStages.end()), aStages.end());
}

String List(const Vector<uint16_t>& acStages)
{
    String result("[");
    for (uint16_t stage : acStages)
    {
        if (result.size() > 1)
            result += ',';
        result += std::to_string(stage).c_str();
    }
    return result + ']';
}

const char* Reason(QuestAdmissionProblem aProblem) noexcept
{
    using enum QuestAdmissionProblem;
    switch (aProblem)
    {
    case MissingOrUnsupported: return "missing_or_unsupported";
    case LocalOnly: return "local_progress_has_no_journal";
    case Unmapped: return "unmapped_local_form";
    case AcceptedRadiant: return "accepted_radiant_missing";
    case Decision: return "committed_decision_missing";
    case LocalProgress: return "extra_local_stages";
    case Lifecycle: return "lifecycle_differs";
    case UnapprovedStart: return "start_replay_unapproved";
    case UnapprovedStage: return "stage_replay_unapproved";
    case UnapprovedStop: return "stop_replay_unapproved";
    case ChangedDuringAdmission: return "changed_during_admission";
    default: return "unspecified";
    }
}
} // namespace

String DescribeQuestAdmission(const QuestAdmissionDiff& acDiff, const QuestJournal* apServer) noexcept
{
    Vector<uint16_t> expected, bootstrap, local(acDiff.Local.Reached), both, localOnly, serverOnly;
    if (apServer)
        for (const auto& event : apServer->Events)
            if (event.Status == 0 || (event.Status == 1 && event.Stage != 0))
            {
                expected.push_back(event.Stage);
                if (event.Bootstrap)
                    bootstrap.push_back(event.Stage);
            }
    SortStages(expected);
    SortStages(bootstrap);
    SortStages(local);
    std::set_intersection(expected.begin(), expected.end(), local.begin(), local.end(), std::back_inserter(both));
    std::set_difference(local.begin(), local.end(), expected.begin(), expected.end(), std::back_inserter(localOnly));
    std::set_difference(expected.begin(), expected.end(), local.begin(), local.end(), std::back_inserter(serverOnly));

    char ids[80];
    std::snprintf(ids, sizeof(ids), "QuestID=%08X:%08X LocalFormID=%08X", acDiff.Id.ModId, acDiff.Id.BaseId, acDiff.LocalForm);
    String editor = acDiff.EditorId;
    for (char& c : editor)
        if (c <= ' ' || c >= 127)
            c = '_';

    String result(ids);
    result += " EditorID=" + (editor.empty() ? String("unresolved") : editor);
    result += " Type=" + String(std::to_string(acDiff.Type).c_str()) + " (" +
        QuestSyncPolicy::CategoryName(QuestSyncPolicy::Classify(acDiff.Id, acDiff.Type).Type) + ")";
    result += " Reason=" + String(Reason(acDiff.Problem));
    result += " Server=[";
    if (apServer)
        result += "Stages=" + List(expected) + " Bootstrap=" + List(bootstrap) + " Started=" + (apServer->Started ? "true" : "false") +
                  " Stopped=" + (apServer->Stopped ? "true" : "false") + " Epoch=" + std::to_string(apServer->Epoch).c_str();
    else
        result += "absent";
    result += "] Local=[";
    if (acDiff.Local.Exists)
        result += "Stages=" + List(local) + " Started=" + (!acDiff.Local.Stopped || !local.empty() ? "true" : "false") +
                  " (inferred) Stopped=" + (acDiff.Local.Stopped ? "true" : "false") + " Epoch=unknown";
    else
        result += "missing";
    result += "] Both=" + List(both) + " LocalOnly=" + List(localOnly) + " ServerOnly=" + List(serverOnly);
    return result;
}
