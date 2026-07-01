#pragma once
#include "ewr/domain.h"
#include <string_view>

// CALCULATION stratum: pure, effect-free protocol logic. No I/O, no logging,
// no globals — directly unit-testable against golden bytes and properties.

namespace ewr { struct DbPrinterModel; } // fwd-decl (defined in ewr/generator.h)

namespace ewr::protocol {

    // Build one |B EEPROM-write D4 packet for a single address/value cell.
    D4Packet BuildWritePacket(ResetKey rkey, EepromWrite cell, std::string_view wkey);

    // Build the full reset sequence: EJL/D4 prologue then a (credit_grant,
    // credit_request, write) triple per address. Pads a short reset[] with 0x00.
    PayloadSequence BuildResetSequence(const DbPrinterModel& model);

    // Pure Wireshark "Export as C arrays" parser over already-read text.
    // Strips the 27-byte USBPcap header when a packet is >=27 bytes and begins
    // 0x1B 0x00; drops packets that become empty after stripping.
    PayloadSequence ParseWiresharkText(std::string_view content);

    // True iff the packet is a |B EEPROM-write frame (7C 7C ... 42 BD 21).
    bool IsWritePacket(const D4Packet& packet);

    // Classify a drained device response. The real per-write reply carries the
    // ASCII token ":42:OK;" (accepted) or ":42:NG;" (rejected). Handshake replies
    // carry neither, so both return false for them.
    bool IsWriteAcknowledged(const D4Packet& response);
    bool IsWriteRejected(const D4Packet& response);

} // namespace ewr::protocol
