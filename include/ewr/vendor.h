#pragma once
#include "ewr/domain.h"

// VENDOR seam (DIP / OCP): ProtocolExecutor drives any printer whose reset is a
// stream of packets with an interpretable per-write reply. Epson's IEEE-1284.4
// (D4) protocol is ONE concrete implementation (EpsonD4Protocol) — a new vendor
// is a new class implementing this interface, with no change to the executor.
//
// Kept deliberately small (ISP): it is exactly what the executor needs to gate
// success — which outbound packets are writes, and how to read the reply. How a
// sequence is *built* (from a DB model vs. a replay dump) is a separate concern
// and stays out of this interface.

namespace ewr {

    // Verdict for a drained device reply to a write packet.
    enum class Ack
    {
        Acknowledged, // the device confirmed the write
        Rejected,     // the device explicitly refused the write
        None,         // neither (a handshake / empty reply) — not a confirmation
    };

    class IResetProtocol
    {
    public:
        virtual ~IResetProtocol() = default;

        // True iff this outbound packet is a write whose reply gates success.
        virtual bool IsWritePacket(const D4Packet& packet) const = 0;

        // Interpret the reply drained immediately after that write packet.
        virtual Ack ClassifyReply(const D4Packet& reply) const = 0;
    };

    // Epson IEEE-1284.4 (D4) reference implementation. Delegates to the pure
    // ewr::protocol core: a |B write frame (7C 7C ... 42 BD 21), acknowledged by
    // ":42:OK;" and rejected by ":42:NG;".
    class EpsonD4Protocol final : public IResetProtocol
    {
    public:
        bool IsWritePacket(const D4Packet& packet) const override;
        Ack ClassifyReply(const D4Packet& reply) const override;
    };

} // namespace ewr
