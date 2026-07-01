#pragma once
#include <memory>
#include <vector>

// BOUNDARY seam: the byte pipe to the printer. One bulk Send, one Drain of the
// IN endpoint. Platform back-ends (libusb / SetupAPI) implement it; the shared
// ProtocolExecutor and unit-test FakeTransport depend only on this interface.

namespace ewr {

    struct ITransport
    {
        virtual ~ITransport() = default;

        // Write one framed packet. Returns false on transport error.
        virtual bool Send(const std::vector<unsigned char>& packet) = 0;

        // Drain the IN endpoint until it goes quiet; empty vector = no reply.
        virtual std::vector<unsigned char> Drain() = 0;
    };

    // Scan USB, claim the Epson maintenance interface, and return an owning
    // transport whose destructor releases the device (RAII). nullptr on failure.
    std::unique_ptr<ITransport> ConnectEpsonPrinter();

} // namespace ewr
