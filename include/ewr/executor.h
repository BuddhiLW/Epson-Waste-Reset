#pragma once
#include "ewr/transport.h"
#include "ewr/domain.h"
#include "ewr/vendor.h"
#include <chrono>
#include <cstddef>
#include <ostream>

// APPLICATION stratum: the platform-independent send -> settle -> drain -> gate
// loop. Success is declared only when every EEPROM-write packet is acknowledged
// with ":42:OK;" (see protocol::IsWriteAcknowledged) — not on any inbound byte.

namespace ewr {

    struct ExecutionResult
    {
        bool   success        = false;
        size_t packetsSent    = 0;
        size_t writesTotal    = 0;
        size_t writesAcked    = 0;
        size_t writesRejected = 0;
        bool   sendError      = false;
    };

    class ProtocolExecutor
    {
    public:
        ProtocolExecutor(ITransport& transport,
                         const IResetProtocol& protocol,
                         std::ostream* trace = nullptr,
                         std::chrono::milliseconds interPacketDelay = std::chrono::milliseconds(100));

        ExecutionResult Run(const PayloadSequence& sequence);

    private:
        ITransport&               transport_;
        const IResetProtocol&     protocol_;
        std::ostream*             trace_;
        std::chrono::milliseconds delay_;
    };

} // namespace ewr
