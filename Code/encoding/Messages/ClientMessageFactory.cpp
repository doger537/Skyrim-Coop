#pragma once

#include <Messages/ClientMessageFactory.h>

static std::function<UniquePtr<ClientMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_clientMessageExtractor[kClientOpcodeMax];

namespace details
{
static struct ClientMessageFactoryInit
{
    ClientMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_clientMessageExtractor[T::Opcode] = [](TiltedPhoques::Buffer::Reader& aReader)
            {
                auto ptr = TiltedPhoques::MakeUnique<T>();
                ptr->DeserializeRaw(aReader);
                return TiltedPhoques::CastUnique<ClientMessage>(std::move(ptr));
            };

            return false;
        };

        ClientMessageFactory::Visit(extractor);
    }
} s_ClientMessageFactoryInit;
} // namespace details

UniquePtr<ClientMessage> ClientMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader, uint64_t aExpectedEpoch) const noexcept
{
    uint64_t data{};
    uint64_t epoch{};
    if (!aReader.ReadBits(data, sizeof(ClientOpcode) * 8) || data >= kClientOpcodeMax || !aReader.ReadBits(epoch, 64)) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<ClientOpcode>(data);
    if (aExpectedEpoch != 0 && opcode != kAuthenticationRequest && epoch != aExpectedEpoch)
        return {nullptr};
    auto message = s_clientMessageExtractor[opcode](aReader);
    message->WorldEpoch = epoch;
    return message;
}
