#include "ewr/executor.h"
#include "ewr/vendor.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace ewr {

    namespace {

        std::string HexDump(const unsigned char* data, size_t size)
        {
            if (size == 0)
                return "    (Empty)\n";

            std::ostringstream oss;
            for (size_t i = 0; i < size; ++i)
            {
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
                if ((i + 1) % 16 == 0 || i == size - 1)
                {
                    if (i == size - 1 && (i + 1) % 16 != 0)
                    {
                        for (size_t p = 0; p < 16 - ((i + 1) % 16); ++p)
                            oss << "   ";
                    }

                    oss << " | ";
                    size_t start = (i / 16) * 16;
                    for (size_t j = start; j <= i; ++j)
                        oss << (char)((data[j] >= 32 && data[j] <= 126) ? data[j] : '.');

                    oss << "\n";
                }
            }
            return oss.str();
        }

    } // namespace

    ProtocolExecutor::ProtocolExecutor(ITransport& transport, const IResetProtocol& protocol, std::ostream* trace, std::chrono::milliseconds interPacketDelay)
        : transport_(transport), protocol_(protocol), trace_(trace), delay_(interPacketDelay)
    {
    }

    ExecutionResult ProtocolExecutor::Run(const PayloadSequence& sequence)
    {
        ExecutionResult result;
        bool allWritesAcked = true;

        for (size_t i = 0; i < sequence.size(); ++i)
        {
            const D4Packet& packet = sequence[i];

            if (trace_ && *trace_)
                *trace_ << "[OUT] Packet " << i + 1 << " (" << packet.size() << " bytes):\n"
                        << HexDump(packet.data(), packet.size()) << "\n";

            if (!transport_.Send(packet))
            {
                std::cerr << "Failed to send packet " << i + 1 << std::endl;
                if (trace_ && *trace_)
                    *trace_ << "[!] WRITE FAILED on Packet " << i + 1 << "\n\n";
                result.sendError = true;
                result.success = false;
                return result;
            }
            ++result.packetsSent;

            std::this_thread::sleep_for(delay_);

            std::vector<unsigned char> response = transport_.Drain();

            if (trace_ && *trace_)
                *trace_ << "[IN]  ACK (" << response.size() << " bytes):\n"
                        << HexDump(response.data(), response.size()) << "\n";

            if (!response.empty())
                std::cout << "-> Packet " << i + 1 << " / " << sequence.size()
                          << " | Triggered ACK: Cleared " << response.size() << " bytes." << std::endl;
            else
                std::cout << "-> Packet " << i + 1 << " / " << sequence.size()
                          << " | Sent. (No ACK)" << std::endl;

            if (protocol_.IsWritePacket(packet))
            {
                ++result.writesTotal;
                switch (protocol_.ClassifyReply(response))
                {
                    case Ack::Rejected:
                        ++result.writesRejected;
                        allWritesAcked = false;
                        break;
                    case Ack::Acknowledged:
                        ++result.writesAcked;
                        break;
                    case Ack::None:
                        allWritesAcked = false;
                        break;
                }
            }
        }

        result.success = (result.writesTotal > 0) && allWritesAcked && (result.writesRejected == 0);
        return result;
    }

} // namespace ewr
