#include "ewr/vendor.h"
#include "ewr/protocol.h"

namespace ewr {

    bool EpsonD4Protocol::IsWritePacket(const D4Packet& packet) const
    {
        return protocol::IsWritePacket(packet);
    }

    Ack EpsonD4Protocol::ClassifyReply(const D4Packet& reply) const
    {
        if (protocol::IsWriteRejected(reply))
            return Ack::Rejected;
        if (protocol::IsWriteAcknowledged(reply))
            return Ack::Acknowledged;
        return Ack::None;
    }

} // namespace ewr
