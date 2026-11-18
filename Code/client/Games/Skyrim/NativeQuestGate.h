#pragma once
#include <Events/QuestItemScriptOrigin.h>
#include <Misc/BSScript.h>

// Runner-side adapter for the verified AddItem delay functor. The policy sees
// owned values only. Native functor addresses never leave this module/call.
namespace NativeQuestGate
{
enum class Decision : uint8_t { PassThrough, Wait, Execute, Complete, Reject };
struct Invocation
{
    uint64_t Cookie{};
    QuestItemScriptOrigin Source;
    uint32_t ItemFormId{};
    uint32_t Count{};
    bool Silent{};
};
struct Policy
{
    Decision (*Begin)(const Invocation&) noexcept{};
    Decision (*Poll)(uint64_t) noexcept{};
    void (*Executed)(uint64_t) noexcept{};
    void (*Failed)(uint64_t) noexcept{};
};
struct Disposal
{
    uint64_t Cookie{};
    bool Completed{};
    bool Executed{};
    bool Loaded{};
};
using Execute = BSScript::Variable* (*)(void*, BSScript::Variable*, const QuestItemScriptOrigin&);

// Configure on the runner before admission; a missing policy passes through.
void Configure(Policy aPolicy) noexcept;
void SetRunnerThread(uint32_t aThread) noexcept;
BSScript::Variable* Invoke(void* apFunctor, BSScript::Variable* apResult, const QuestItemScriptOrigin& acSource,
    uint32_t aItemFormId, uint32_t aCount, bool aSilent, Execute aOriginal) noexcept;
bool HasPending() noexcept;
bool HasLive() noexcept;
bool NeedsReload() noexcept;
void CancelPending() noexcept;
// Called only for an actual TESLoadGameEvent, not a failed load request.
void AfterLoad() noexcept;
bool PollDisposal(Disposal& aDisposal) noexcept;
}
