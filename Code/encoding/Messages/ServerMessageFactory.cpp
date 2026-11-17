#pragma once

#include <Messages/ServerMessageFactory.h>

static std::function<UniquePtr<ServerMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_serverMessageExtractor[kServerOpcodeMax];

namespace details
{
static struct ServerMessageFactoryInit
{
    ServerMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_serverMessageExtractor[T::Opcode] = [](TiltedPhoques::Buffer::Reader& aReader)
            {
                auto ptr = TiltedPhoques::MakeUnique<T>();
                ptr->DeserializeRaw(aReader);
                return TiltedPhoques::CastUnique<ServerMessage>(std::move(ptr));
            };

            return false;
        };

        ServerMessageFactory::Visit(extractor);
    }
} s_ServerMessageFactoryInit;
} // namespace details

UniquePtr<ServerMessage> ServerMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader, uint64_t aExpectedEpoch) const noexcept
{
    uint64_t data{};
    uint64_t epoch{};
    if (!aReader.ReadBits(data, sizeof(ServerOpcode) * 8) || data >= kServerOpcodeMax || !aReader.ReadBits(epoch, 64)) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<ServerOpcode>(data);
    if (aExpectedEpoch != 0 && opcode != kAuthenticationResponse && opcode != kNotifyWorldCheckpoint && epoch != aExpectedEpoch)
        return {nullptr};
    auto message = s_serverMessageExtractor[opcode](aReader);
    message->WorldEpoch = epoch;
    if (opcode == kNotifyWorldCheckpoint && aExpectedEpoch != 0 && epoch != aExpectedEpoch)
    {
        const auto& checkpoint = *static_cast<const NotifyWorldCheckpoint*>(message.get());
        if (!checkpoint.IsValid() || checkpoint.State != NotifyWorldCheckpoint::Phase::LoadNow || checkpoint.PreviousEpoch != aExpectedEpoch || epoch <= aExpectedEpoch)
            return {nullptr};
    }
    return message;
}
