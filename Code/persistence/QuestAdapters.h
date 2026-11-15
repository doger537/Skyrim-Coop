#pragma once
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <Structs/Mods.h>
#include <string_view>

namespace Campaign
{
struct Branch
{
    TiltedPhoques::String Id;
    TiltedPhoques::Vector<uint16_t> Stages;
};

struct QuestAdapter
{
    TiltedPhoques::String Plugin;
    std::array<uint8_t, 32> Digest{};
    uint32_t Form{};
    bool AllowMisc{};
    bool AllowRestart{};
    bool ReplayLifecycle{};
    TiltedPhoques::Vector<uint16_t> Stages;
    TiltedPhoques::Vector<uint16_t> ReplayStages;
    TiltedPhoques::Vector<Branch> Branches;
    TiltedPhoques::String Evidence;
};

class QuestAdapters
{
public:
    static constexpr const char* kEmpty = "{\"schema\":1,\"adapters\":[]}";
    bool Load(std::string_view acJson) noexcept;
    bool Validate(const Mods& acContent) noexcept;
    const QuestAdapter* Find(const TiltedPhoques::String& acPlugin, uint32_t aForm) const noexcept;
    const TiltedPhoques::String& Canonical() const noexcept { return m_canonical; }
    const TiltedPhoques::String& Error() const noexcept { return m_error; }
    const TiltedPhoques::Vector<QuestAdapter>& Entries() const noexcept { return m_adapters; }

private:
    TiltedPhoques::Vector<QuestAdapter> m_adapters;
    TiltedPhoques::String m_canonical;
    TiltedPhoques::String m_error;
};
} // namespace Campaign
