#include <TiltedOnlinePCH.h>
#include "NativeQuestGate.h"
#include <Misc/BSScript.h>
#include <array>
#include <atomic>
#include <mutex>
#include <limits>
#include <algorithm>

namespace NativeQuestGate
{
namespace
{
constexpr size_t kCapacity = 128;
struct Pending
{
    void* Functor{}; // Native-owned lifetime; destructor unregisters before free.
    Invocation Call;
    Execute Original{};
    Decision Initial{Decision::Wait};
    bool Used{};
    bool Executed{};
    bool Done{};
    bool Success{};
    bool Cancelled{};
    bool Loaded{};
};
std::array<Pending, kCapacity> s_calls;
std::array<Disposal, kCapacity * 2> s_disposals;
size_t s_disposalHead{}, s_disposalCount{};
std::mutex s_mutex;
std::atomic<size_t> s_live{};
std::atomic<bool> s_tainted{};
std::atomic<uint32_t> s_runnerThread{};
uint64_t s_nextCookie{};
Policy s_policy;

// MCP 1419A7F40: preserve the MSVC COL at vtable[-1] and all seven methods.
// Only destructor, Run and WantsRequeue are substituted. Type 14 and native
// save/load methods remain unchanged; a pending operation prevents native save.
struct Family { std::array<uintptr_t, 8> Clone; uintptr_t* Original{}; };
std::array<Family, 2> s_families;
using Destructor = void*(__fastcall*)(void*, uint32_t);

Pending* Find(void* apFunctor) noexcept
{
    for (auto& call : s_calls)
        if (call.Used && call.Functor == apFunctor) return &call;
    return nullptr;
}
BSScript::Variable* None(BSScript::Variable* apResult) noexcept
{
    // Native Run initializes this fresh output before copying the None value.
    // Reset() would incorrectly release an uninitialized scheduler stack slot.
    apResult->type = BSScript::Variable::kEmpty;
    apResult->data = {};
    return apResult;
}
void Finish(Pending& aCall, bool aSuccess) noexcept
{
    aCall.Done = true;
    aCall.Success = aSuccess;
    if (!aSuccess && !aCall.Loaded) s_tainted.store(true, std::memory_order_release);
}
bool __fastcall Requeue(void* apFunctor) noexcept
{
    const std::lock_guard lock(s_mutex);
    const auto* call = Find(apFunctor);
    // Only Run changes Done. A network decision between Run and this call
    // cannot wake the VM before Run actually executes or suppresses the effect.
    return call && !call->Done;
}
template <bool Remove> void* __fastcall Destroy(void* apFunctor, uint32_t aFlags) noexcept
{
    auto* original = s_families[Remove ? 1 : 0].Original;
    {
        const std::lock_guard lock(s_mutex);
        if (auto* call = Find(apFunctor))
        {
            if (!call->Done && !call->Loaded) s_tainted.store(true, std::memory_order_release);
            if (s_disposalCount < s_disposals.size())
            {
                s_disposals[(s_disposalHead + s_disposalCount) % s_disposals.size()] =
                    {call->Call.Cookie, call->Success, call->Executed, call->Loaded};
                ++s_disposalCount;
            }
            else s_tainted.store(true, std::memory_order_release);
            *call = {};
            s_live.fetch_sub(1, std::memory_order_release);
        }
        *static_cast<uintptr_t**>(apFunctor) = original;
    }
    // MCP Add destructor 140A4FA30 frees 28h bytes; Remove 140A4FC00 releases
    // its VM at +28 and frees 30h bytes iff flags&1. Preserve each native
    // destructor/COL family, never substitute a generic C++ delete.
    return reinterpret_cast<Destructor>(original[0])(apFunctor, aFlags);
}
BSScript::Variable* __fastcall Run(void* apFunctor, BSScript::Variable* apResult) noexcept
{
    uint64_t cookie{};
    Policy policy;
    Decision decision{Decision::Wait};
    bool executed{};
    {
        const std::lock_guard lock(s_mutex);
        auto* call = Find(apFunctor);
        if (!call) { s_tainted.store(true, std::memory_order_release); return None(apResult); }
        if (call->Done) return None(apResult);
        if (call->Cancelled) { Finish(*call, false); return None(apResult); }
        if (s_runnerThread.load(std::memory_order_acquire) != GetCurrentThreadId())
        {
            Finish(*call, false);
            return None(apResult);
        }
        cookie = call->Call.Cookie;
        executed = call->Executed;
        decision = call->Initial;
        call->Initial = Decision::Wait;
        policy = s_policy;
    }
    if (decision == Decision::Wait)
        decision = policy.Poll ? policy.Poll(cookie) : Decision::Reject;
    if (decision == Decision::Wait || (decision == Decision::Execute && executed)) return None(apResult);
    if (decision == Decision::Execute)
    {
        Invocation invocation;
        Execute original{};
        {
            const std::lock_guard lock(s_mutex);
            auto* call = Find(apFunctor);
            if (!call || call->Cancelled) { if (call) Finish(*call, false); return None(apResult); }
            invocation = call->Call;
            original = call->Original;
            if (!original) { Finish(*call, false); return None(apResult); }
            call->Executed = true;
        }
        auto* result = original(apFunctor, apResult, invocation.Source);
        if (policy.Executed) policy.Executed(invocation.Cookie);
        // Still requeue until the policy has durably resolved both deliveries.
        return result;
    }
    const bool success = decision == Decision::Complete;
    {
        const std::lock_guard lock(s_mutex);
        if (auto* call = Find(apFunctor)) Finish(*call, success && !call->Cancelled);
    }
    if (!success && policy.Failed) policy.Failed(cookie);
    return None(apResult);
}
bool Initialize(bool aRemove) noexcept
{
    auto& family = s_families[aRemove ? 1 : 0];
    if (family.Original) return true;
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0") return false;
    POINTER_SKYRIMSE(uintptr_t, table, 218372);
    POINTER_SKYRIMSE(void, run, 56095);
    POINTER_SKYRIMSE(void, destroy, 56348);
    POINTER_SKYRIMSE(uintptr_t, removeTable, 218376);
    POINTER_SKYRIMSE(void, removeRun, 56098);
    POINTER_SKYRIMSE(void, removeDestroy, 56354);
    auto* original = aRemove ? removeTable.Get() : table.Get();
    if (!original || original[0] != reinterpret_cast<uintptr_t>(aRemove ? removeDestroy.Get() : destroy.Get()) || original[1] != reinterpret_cast<uintptr_t>(aRemove ? removeRun.Get() : run.Get()))
        return false;
    std::copy_n(original - 1, family.Clone.size(), family.Clone.begin());
    family.Clone[1] = aRemove ? reinterpret_cast<uintptr_t>(&Destroy<true>) : reinterpret_cast<uintptr_t>(&Destroy<false>);
    family.Clone[2] = reinterpret_cast<uintptr_t>(&Run);
    family.Clone[4] = reinterpret_cast<uintptr_t>(&Requeue);
    family.Original = original;
    return true;
}
}

void Configure(Policy aPolicy) noexcept
{
    const std::lock_guard lock(s_mutex);
    for (auto& call : s_calls)
        if (call.Used && !call.Done) { call.Cancelled = true; s_tainted.store(true, std::memory_order_release); }
    s_policy = aPolicy;
}
void SetRunnerThread(uint32_t aThread) noexcept { s_runnerThread.store(aThread, std::memory_order_release); }
BSScript::Variable* Invoke(void* apFunctor, BSScript::Variable* apResult, const QuestItemScriptOrigin& acSource,
    uint32_t aItemFormId, uint32_t aCount, bool aSilent, Execute aOriginal, bool aRemove) noexcept
{
    Invocation invocation;
    invocation.Source = acSource; invocation.ItemFormId = aItemFormId; invocation.Count = aCount; invocation.Silent = aSilent;
    invocation.Remove = aRemove;
    Policy policy;
    {
        const std::lock_guard lock(s_mutex);
        policy = s_policy;
        if (policy.Begin && s_nextCookie != std::numeric_limits<uint64_t>::max()) invocation.Cookie = ++s_nextCookie;
    }
    if (!policy.Begin) return aOriginal(apFunctor, apResult, acSource);
    if (s_runnerThread.load(std::memory_order_acquire) != GetCurrentThreadId())
    {
        s_tainted.store(true, std::memory_order_release);
        return None(apResult);
    }
    if (!invocation.Cookie) { s_tainted.store(true, std::memory_order_release); if (policy.Failed) policy.Failed(0); return None(apResult); }
    invocation.Source.Operation = invocation.Cookie;
    const Decision initial = policy.Begin(invocation);
    if (initial == Decision::PassThrough) return aOriginal(apFunctor, apResult, acSource);
    bool registered{};
    {
        const std::lock_guard lock(s_mutex);
        auto& family = s_families[aRemove ? 1 : 0];
        if (Initialize(aRemove) && *static_cast<uintptr_t**>(apFunctor) == family.Original)
            for (auto& call : s_calls)
                if (!call.Used)
                {
                    call = {};
                    call.Functor = apFunctor; call.Call = std::move(invocation); call.Original = aOriginal;
                    call.Used = true; call.Initial = initial;
                    s_live.fetch_add(1, std::memory_order_release);
                    *static_cast<uintptr_t**>(apFunctor) = family.Clone.data() + 1;
                    registered = true;
                    break;
                }
    }
    if (!registered)
    {
        s_tainted.store(true, std::memory_order_release);
        if (policy.Failed) policy.Failed(invocation.Cookie);
        return None(apResult);
    }
    return Run(apFunctor, apResult);
}
bool HasLive() noexcept { return s_live.load(std::memory_order_acquire) != 0; }
bool HasPending() noexcept { return HasLive() || NeedsReload(); }
bool NeedsReload() noexcept { return s_tainted.load(std::memory_order_acquire); }
void CancelPending() noexcept
{
    const std::lock_guard lock(s_mutex);
    for (auto& call : s_calls)
        if (call.Used && !call.Done)
        {
            call.Cancelled = true;
            s_tainted.store(true, std::memory_order_release);
        }
}
void AfterLoad() noexcept
{
    const std::lock_guard lock(s_mutex);
    for (auto& call : s_calls)
        if (call.Used) { call.Cancelled = true; call.Loaded = true; }
    s_tainted.store(false, std::memory_order_release);
}
bool PollDisposal(Disposal& aDisposal) noexcept
{
    const std::lock_guard lock(s_mutex);
    if (!s_disposalCount) return false;
    aDisposal = s_disposals[s_disposalHead];
    s_disposalHead = (s_disposalHead + 1) % s_disposals.size();
    --s_disposalCount;
    return true;
}
}
