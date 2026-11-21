#pragma once
#include <cstdint>

struct TESObjectREFR;

// Stack-only engine reference for one runner operation. A queued job carries a
// handle and acquires its reference when executed, never a borrowed game pointer.
class ScopedReference
{
public:
    explicit ScopedReference(uint32_t aHandle) noexcept;
    ~ScopedReference() noexcept;
    ScopedReference(const ScopedReference&) = delete;
    ScopedReference& operator=(const ScopedReference&) = delete;
    ScopedReference(ScopedReference&&) = delete;
    ScopedReference& operator=(ScopedReference&&) = delete;

    TESObjectREFR* Get() const noexcept { return m_pReference; }
    explicit operator bool() const noexcept { return m_pReference != nullptr; }

private:
    TESObjectREFR* m_pReference{};
};
