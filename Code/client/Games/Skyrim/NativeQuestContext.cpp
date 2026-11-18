#include <TiltedOnlinePCH.h>
#include "NativeQuestContext.h"
#include "NativeQuestGate.h"
#include <Misc/BSScript.h>
#include <Forms/TESQuest.h>
#include <ScopedReference.h>
#include <TESObjectREFR.h>
#include <atomic>
#include <cstring>

namespace NativeQuestContext
{
namespace
{
struct Frame
{
    void* Parent;
    Frame* Previous;
    BSScript::IFunction* Function;
    const void* ObjectType;
    uint32_t Instruction;
    uint32_t Padding;
    BSScript::Variable Self;
};
static_assert(offsetof(Frame, Previous) == 8);
static_assert(offsetof(Frame, Function) == 0x10);
static_assert(offsetof(Frame, ObjectType) == 0x18);
static_assert(offsetof(Frame, Instruction) == 0x20);
static_assert(offsetof(Frame, Self) == 0x28);
static_assert(sizeof(Frame) == 0x38);
struct StackView
{
    volatile LONG References;
    uint8_t Padding[0x58 - sizeof(LONG)];
    uint32_t FrameCount;
    uint32_t Padding5C;
    Frame* Top;
};
static_assert(offsetof(StackView, FrameCount) == 0x58);
static_assert(offsetof(StackView, Top) == 0x60);
struct AddItemFunctor
{
    void* Vtable;
    uint32_t References;
    uint32_t StackId;
    uint32_t TargetHandle;
    uint32_t ItemFormId;
    uint32_t Count;
    bool Silent;
    uint8_t Padding[3];
    BSScript::IVirtualMachine* Vm;
};
static_assert(offsetof(AddItemFunctor, StackId) == 0xC);
static_assert(offsetof(AddItemFunctor, TargetHandle) == 0x10);
static_assert(offsetof(AddItemFunctor, Vm) == 0x20);
static_assert(sizeof(AddItemFunctor) == 0x28);
using Execute = BSScript::Variable*(__fastcall)(AddItemFunctor*, BSScript::Variable*);
Execute* s_execute{};
struct RemoveItemFunctor
{
    void* Vtable; uint32_t References, StackId, TargetHandle, DestinationHandle, ItemFormId, Count;
    bool Silent; uint8_t Padding[7]; BSScript::IVirtualMachine* Vm;
};
static_assert(offsetof(RemoveItemFunctor, ItemFormId) == 0x18 && offsetof(RemoveItemFunctor, Vm) == 0x28 && sizeof(RemoveItemFunctor) == 0x30);
using RemoveExecute = BSScript::Variable*(__fastcall)(RemoveItemFunctor*, BSScript::Variable*);
RemoveExecute* s_removeExecute{};
std::atomic<DWORD> s_runnerThread{};
thread_local const QuestItemScriptOrigin* s_origin{};

class StackReference
{
public:
    StackReference(BSScript::IVirtualMachine* apVm, uint32_t aId) noexcept
    {
        // MCP: this is IVMSaveLoadInterface (primary VM +18), NOT the
        // primary VM vtable's slot 0F. The lookup retains its result.
        using Lookup = bool(__fastcall)(void*, uint32_t, StackView**);
        POINTER_SKYRIMSE(Lookup, lookup, 104842);
        if (apVm && aId)
            lookup.Get()(reinterpret_cast<uint8_t*>(apVm) + 0x18, aId, &m_stack);
    }
    ~StackReference() noexcept
    {
        if (m_stack && InterlockedDecrement(&m_stack->References) == 0)
        {
            // Match the VM's own retained-stack release, including last-ref
            // destruction. A raw decrement would leak pages/objects on zero.
            using Destroy = void(__fastcall)(void*);
            using Free = void(__fastcall)(void*, size_t);
            POINTER_SKYRIMSE(Destroy, destroy, 104480);
            POINTER_SKYRIMSE(Free, release, 36098);
            destroy.Get()(m_stack);
            release.Get()(m_stack, 0xA0);
        }
    }
    StackReference(const StackReference&) = delete;
    StackReference& operator=(const StackReference&) = delete;
    StackView* Get() const noexcept { return m_stack; }
private:
    StackView* m_stack{};
};

bool CopyName(const char* apText, String& aCopy) noexcept
{
    constexpr size_t kMaxName = 128;
    const size_t size = apText ? strnlen_s(apText, kMaxName + 1) : 0;
    if (!size || size > kMaxName)
        return false;
    aCopy.assign(apText, size);
    return true;
}

TESQuest* Owner(BSScript::IObjectHandlePolicy& aPolicy, uint64_t aHandle, BSScript::IVirtualMachine* apVm,
    QuestItemScriptOrigin::Source& aKind) noexcept
{
    if (!aPolicy.IsHandleObjectAvailable(aHandle))
        return nullptr;
    TESQuest* quest{};
    if (aPolicy.HandleIsType(77, aHandle))
    {
        aKind = QuestItemScriptOrigin::Source::Quest;
        quest = static_cast<TESQuest*>(aPolicy.GetObjectForHandle(77, aHandle));
    }
    else
    {
        // Use the verified engine GetOwningQuest callbacks for each VM type;
        // do not treat an alias handle as a raw quest/runtime form ID.
        using OwningQuest = TESQuest*(__fastcall)(BSScript::IVirtualMachine*, uint32_t, const void*);
        const void* object{};
        OwningQuest* callback{};
        if (aPolicy.HandleIsType(139, aHandle))
        {
            POINTER_SKYRIMSE(OwningQuest, alias, 55155);
            aKind = QuestItemScriptOrigin::Source::Alias;
            object = aPolicy.GetObjectForHandle(139, aHandle); callback = alias.Get();
        }
        else if (aPolicy.HandleIsType(76, aHandle))
        {
            POINTER_SKYRIMSE(OwningQuest, info, 56765);
            aKind = QuestItemScriptOrigin::Source::TopicInfo;
            object = aPolicy.GetObjectForHandle(76, aHandle); callback = info.Get();
        }
        else if (aPolicy.HandleIsType(122, aHandle))
        {
            POINTER_SKYRIMSE(OwningQuest, scene, 55338);
            aKind = QuestItemScriptOrigin::Source::Scene;
            object = aPolicy.GetObjectForHandle(122, aHandle); callback = scene.Get();
        }
        if (object && callback)
            quest = callback(apVm, 0, object);
    }
    return quest && quest->formType == FormType::QuestItem && quest->formID && !(quest->TESForm::flags & TESForm::STILL_LOADING) ? quest : nullptr;
}

std::optional<QuestItemScriptOrigin> Read(BSScript::IVirtualMachine* apVm, uint32_t aStack) noexcept
{
    StackReference reference(apVm, aStack);
    auto* stack = reference.Get();
    auto* policy = BSScript::IObjectHandlePolicy::Get();
    if (!stack || !policy || !stack->FrameCount)
        return {};
    auto* frame = stack->Top;
    const uint32_t count = std::min(stack->FrameCount, 64u);
    for (uint32_t i = 0; frame && i < count; ++i, frame = frame->Previous)
    {
        if (!frame->Function || !frame->ObjectType)
            continue;
        BSScript::Variable self;
        self.type = BSScript::Variable::kEmpty; self.data = {};
        using CopyVariable = void(__fastcall)(BSScript::Variable*, const BSScript::Variable*);
        POINTER_SKYRIMSE(CopyVariable, copy, 104297);
        copy.Get()(&self, &frame->Self);
        auto* object = self.GetObject();
        if (!object)
            continue;
        QuestItemScriptOrigin origin;
        auto* quest = Owner(*policy, object->GetHandle(), apVm, origin.Kind);
        if (!quest)
            continue;
        // MCP's own stack formatter reads type-name +8 and GetName slot 1.
        const char* name = *reinterpret_cast<const char* const*>(static_cast<const uint8_t*>(frame->ObjectType) + 8);
        if (!CopyName(name, origin.Script) || !CopyName(frame->Function->GetName().AsAscii(), origin.Function))
            return {};
        origin.QuestFormId = quest->formID;
        origin.StackId = aStack;
        origin.InstructionOffset = frame->Instruction;
        return origin;
    }
    return {};
}

BSScript::Variable* ExecuteWithOrigin(void* apFunctor, BSScript::Variable* apResult, const QuestItemScriptOrigin& acSource)
{
    struct Restore
    {
        const QuestItemScriptOrigin* Previous{s_origin};
        ~Restore() { s_origin = Previous; }
    } restore;
    s_origin = &acSource;
    return s_execute(static_cast<AddItemFunctor*>(apFunctor), apResult);
}
BSScript::Variable* ExecuteHook(AddItemFunctor* apFunctor, BSScript::Variable* apResult)
{
    const DWORD runner = s_runnerThread.load(std::memory_order_acquire);
    if (AuditEnabled() && apFunctor)
        spdlog::info("Quest AddItem audit: runner={} executing={} stack={} item={:X} count={}",
            runner, GetCurrentThreadId(), apFunctor->StackId, apFunctor->ItemFormId, apFunctor->Count);
    if (!runner || GetCurrentThreadId() != runner || !apFunctor || !apFunctor->Vm)
        return s_execute(apFunctor, apResult);
    const ScopedReference target(apFunctor->TargetHandle);
    if (!target || target.Get()->formID != 0x14)
        return s_execute(apFunctor, apResult);
    const auto origin = Read(apFunctor->Vm, apFunctor->StackId);
    if (origin)
        return NativeQuestGate::Invoke(apFunctor, apResult, *origin, apFunctor->ItemFormId, apFunctor->Count, apFunctor->Silent, ExecuteWithOrigin);
    struct Restore
    {
        const QuestItemScriptOrigin* Previous{s_origin};
        ~Restore() { s_origin = Previous; }
    } restore;
    s_origin = nullptr;
    // An unknown source retains its original engine behavior.
    return s_execute(apFunctor, apResult);
}
BSScript::Variable* RemoveWithOrigin(void* apFunctor, BSScript::Variable* apResult, const QuestItemScriptOrigin& acSource)
{
    struct Restore { const QuestItemScriptOrigin* Previous{s_origin}; ~Restore() { s_origin = Previous; } } restore;
    s_origin = &acSource;
    return s_removeExecute(static_cast<RemoveItemFunctor*>(apFunctor), apResult);
}
BSScript::Variable* RemoveHook(RemoveItemFunctor* apFunctor, BSScript::Variable* apResult)
{
    // MCP Run 140A418E0: target +10, destination +14, form +18, count
    // +1C, silent +20, retained VM +28; native result is a fresh Variable*.
    if (!apFunctor || !apFunctor->Vm || s_runnerThread.load(std::memory_order_acquire) != GetCurrentThreadId() || apFunctor->DestinationHandle)
        return s_removeExecute(apFunctor, apResult);
    ScopedReference target(apFunctor->TargetHandle);
    if (!target || target.Get()->formID != 0x14) return s_removeExecute(apFunctor, apResult);
    const auto origin = Read(apFunctor->Vm, apFunctor->StackId);
    if (origin) return NativeQuestGate::Invoke(apFunctor, apResult, *origin, apFunctor->ItemFormId, apFunctor->Count, apFunctor->Silent, RemoveWithOrigin, true);
    return s_removeExecute(apFunctor, apResult);
}
}

bool AuditEnabled() noexcept
{
#if IS_MASTER
    return false;
#else
    static const bool enabled = []()
    {
        char value[2]{};
        return GetEnvironmentVariableA("TRUECOOP_QUEST_AUDIT", value, sizeof(value)) == 1 && value[0] == '1';
    }();
    return enabled;
#endif
}
void SetEnabled(bool aEnabled) noexcept
{
    s_runnerThread.store(aEnabled ? GetCurrentThreadId() : 0, std::memory_order_release);
    NativeQuestGate::SetRunnerThread(aEnabled ? GetCurrentThreadId() : 0);
    if (!aEnabled) NativeQuestGate::CancelPending();
}
std::optional<QuestItemScriptOrigin> Current() noexcept
{
    return s_origin ? std::optional<QuestItemScriptOrigin>(*s_origin) : std::nullopt;
}
static TiltedPhoques::Initializer s_hooks([]()
{
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0")
        return;
    POINTER_SKYRIMSE(Execute, execute, 56095);
    s_execute = execute.Get();
    TP_HOOK(&s_execute, ExecuteHook);
    POINTER_SKYRIMSE(RemoveExecute, remove, 56098);
    s_removeExecute = remove.Get();
    TP_HOOK(&s_removeExecute, RemoveHook);
});
}
