#pragma once

#include <Misc/GameVM.h>
#include <atomic>
#include <mutex>
#include <string_view>
#include <type_traits>

struct TESForm;
struct TESObjectREFR;

/**
 * @brief Handles registering and executing Papyrus functions.
 */
struct PapyrusService
{
    PapyrusService() noexcept = default;
    ~PapyrusService() noexcept = default;

    TP_NOCOPYMOVE(PapyrusService);

    static const void* Find(std::string_view acNamespace, std::string_view acFunction) noexcept;
    const void* Get(std::string_view acNamespace, std::string_view acFunction) const noexcept;
    const void* GetDoorOpenStateFunction() const noexcept { return m_doorOpenState.load(std::memory_order_acquire); }
    void Register(const char* acpNamespace, const char* acpFunction, const void* apAddress) noexcept;

private:
    mutable std::mutex m_functionsLock;
    Map<String, const void*> m_functions;
    // Door activation reads this directly on AI workers.
    std::atomic<const void*> m_doorOpenState{};
};

template <class Return, class Type, class... Args> struct PapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const Type*, Args...);

    PapyrusFunction(const void* apAddress)
        : m_pFunction(reinterpret_cast<TFunction>(apAddress))
    {
    }

    PapyrusFunction(const char* acpNamespace, const char* acpName) noexcept
        : m_cpNamespace(acpNamespace)
        , m_cpName(acpName)
    {
    }

    Return operator()(const Type* apThis, Args... args) const noexcept
    {
        auto pFunction = m_pFunction.load(std::memory_order_acquire);
        if (!pFunction && m_cpName)
        {
            pFunction = reinterpret_cast<TFunction>(PapyrusService::Find(m_cpNamespace, m_cpName));
            if (pFunction)
                m_pFunction.store(pFunction, std::memory_order_release);
            else if (!m_missingLogged.exchange(true, std::memory_order_relaxed))
                spdlog::warn("Papyrus native {}.{} is not registered yet", m_cpNamespace, m_cpName);
        }

        const auto* pVM = GameVM::Get();
        if (!pFunction || !pVM || !pVM->virtualMachine || !apThis)
        {
            if constexpr (std::is_void_v<Return>)
                return;
            else
                return Return{};
        }
        return pFunction(pVM->virtualMachine, 0, apThis, std::forward<Args>(args)...);
    }

private:
    mutable std::atomic<TFunction> m_pFunction{};
    mutable std::atomic<bool> m_missingLogged{};
    const char* m_cpNamespace{};
    const char* m_cpName{};
};

template <class Return, class... Args> struct GlobalPapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, Args...);

    GlobalPapyrusFunction(const void* apAddress)
        : m_pFunction(reinterpret_cast<TFunction>(apAddress))
    {
    }

    Return operator()(Args... args) const noexcept { return m_pFunction(GameVM::Get()->virtualMachine, std::forward<Args>(args)...); }

private:
    TFunction m_pFunction;
};

struct RefrOrInventoryObj
{
    const TESObjectREFR* pRefr;
    TESForm* pInventoryForm;
    uint16_t itemCount;
};

template <class Return, class Type, class... Args> struct LatentPapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const RefrOrInventoryObj&, Args...);

    LatentPapyrusFunction(const void* apAddress)
        : m_pFunction(reinterpret_cast<TFunction>(apAddress))
    {
    }

    Return operator()(const Type* apThis, Args... args) const noexcept
    {
        RefrOrInventoryObj self{apThis, nullptr, 0};

        return m_pFunction(GameVM::Get()->virtualMachine, 0, self, std::forward<Args>(args)...);
    }

private:
    TFunction m_pFunction;
};

#define PAPYRUS_FUNCTION(returnType, scope, name, ...) static PapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope, #name);
#define GLOBAL_PAPYRUS_FUNCTION(returnType, scope, name, ...) static GlobalPapyrusFunction<returnType, __VA_ARGS__> s_p##name(World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
#define LATENT_PAPYRUS_FUNCTION(returnType, scope, name, ...) static LatentPapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
