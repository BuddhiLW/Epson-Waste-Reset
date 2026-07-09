#pragma once
#include "ewr/domain.h"
#include <string_view>

namespace ewr { struct DbPrinterModel; }

namespace ewr::protocol {

    D4Packet BuildWritePacket(ResetKey rkey, EepromWrite cell, std::string_view wkey);

    PayloadSequence BuildResetSequence(const DbPrinterModel& model);

    PayloadSequence ParseWiresharkText(std::string_view content);

    bool IsWritePacket(const D4Packet& packet);

    bool IsWriteAcknowledged(const D4Packet& response);
    bool IsWriteRejected(const D4Packet& response);

} // namespace ewr::protocol
