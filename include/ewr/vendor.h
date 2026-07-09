#pragma once
#include "ewr/domain.h"

namespace ewr {

    enum class Ack
    {
        Acknowledged,
        Rejected,
        None,
    };

    class IResetProtocol
    {
    public:
        virtual ~IResetProtocol() = default;

        virtual bool IsWritePacket(const D4Packet& packet) const = 0;

        virtual Ack ClassifyReply(const D4Packet& reply) const = 0;
    };

    class EpsonD4Protocol final : public IResetProtocol
    {
    public:
        bool IsWritePacket(const D4Packet& packet) const override;
        Ack ClassifyReply(const D4Packet& reply) const override;
    };

} // namespace ewr
