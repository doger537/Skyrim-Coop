#include <TiltedOnlinePCH.h>

#include <Services/PapyrusService.h>
#include <World.h>

namespace
{
String MakeFunctionKey(std::string_view acNamespace, std::string_view acFunction) noexcept
{
    String key;
    key.reserve(acNamespace.size() + 2 + acFunction.size());
    key.append(acNamespace);
    key.append("::");
    key.append(acFunction);
    for (char& c : key)
    {
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    }
    return key;
}
}

const void* PapyrusService::Get(std::string_view acNamespace, std::string_view acFunction) const noexcept
{
    const auto key = MakeFunctionKey(acNamespace, acFunction);
    const std::scoped_lock lock(m_functionsLock);
    const auto iterator = m_functions.find(key);
    return iterator == m_functions.end() ? nullptr : iterator->second;
}

const void* PapyrusService::Find(std::string_view acNamespace, std::string_view acFunction) noexcept
{
    return World::Get().ctx().at<PapyrusService>().Get(acNamespace, acFunction);
}

void PapyrusService::Register(const char* acpNamespace, const char* acpFunction, const void* apAddress) noexcept
{
    if (!acpNamespace || !acpFunction || !apAddress)
        return;

    const auto key = MakeFunctionKey(acpNamespace, acpFunction);
    const std::scoped_lock lock(m_functionsLock);
    m_functions[key] = apAddress;
    if (key == "objectreference::getopenstate")
        m_doorOpenState.store(apAddress, std::memory_order_release);
}
