#include "ewr/protocol.h"
#include "ewr/proto.h"
#include "ewr/generator.h" // full definition of DbPrinterModel

#include <algorithm>
#include <regex>
#include <string>
#include <utility>

namespace ewr::protocol {

    static void pushLe16(D4Packet& v, uint16_t x)
    {
        v.push_back(x & 0xFF);
        v.push_back((x >> 8) & 0xFF);
    }

    static void pushBe16(D4Packet& v, uint16_t x)
    {
        v.push_back((x >> 8) & 0xFF);
        v.push_back(x & 0xFF);
    }

    D4Packet BuildWritePacket(ResetKey rkey, EepromWrite cell, std::string_view wkey)
    {
        const uint8_t c       = proto::WRITE_CMD;                                             // 0x42
        const uint8_t not_c   = static_cast<uint8_t>(~c & 0xFF);                              // 0xBD
        const uint8_t shift_c = static_cast<uint8_t>(((c >> 1) & 0x7F) | ((c << 7) & 0x80));  // rotr1 -> 0x21

        D4Packet inner;
        pushLe16(inner, static_cast<uint16_t>(rkey));
        inner.push_back(c);
        inner.push_back(not_c);
        inner.push_back(shift_c);
        pushLe16(inner, static_cast<uint16_t>(cell.addr));
        inner.push_back(cell.value);
        inner.insert(inner.end(), wkey.begin(), wkey.end());

        D4Packet epson;
        epson.push_back(proto::FRAME);
        epson.push_back(proto::FRAME);
        pushLe16(epson, static_cast<uint16_t>(inner.size()));
        epson.insert(epson.end(), inner.begin(), inner.end());

        D4Packet d4;
        d4.push_back(proto::D4_PSID);
        d4.push_back(proto::D4_SSID);
        pushBe16(d4, static_cast<uint16_t>(epson.size() + proto::D4_HEADER_LEN));
        d4.push_back(proto::D4_CREDIT_NONE);
        d4.push_back(proto::D4_CONTROL);
        d4.insert(d4.end(), epson.begin(), epson.end());
        return d4;
    }

    PayloadSequence BuildResetSequence(const DbPrinterModel& model)
    {
        PayloadSequence seq;
        seq.emplace_back(proto::EJL_PACKET_MODE_INIT.begin(), proto::EJL_PACKET_MODE_INIT.end());
        seq.emplace_back(proto::D4_INIT.begin(), proto::D4_INIT.end());
        seq.emplace_back(proto::D4_OPEN_CHANNEL.begin(), proto::D4_OPEN_CHANNEL.end());

        for (size_t i = 0; i < model.addresses.size(); ++i)
        {
            // Local OOB guard: never index past a short reset[] (padding also happens in the loader).
            const uint8_t value = (i < model.reset_values.size()) ? model.reset_values[i] : 0x00;

            seq.emplace_back(proto::D4_CREDIT_GRANT.begin(), proto::D4_CREDIT_GRANT.end());
            seq.emplace_back(proto::D4_CREDIT_REQUEST.begin(), proto::D4_CREDIT_REQUEST.end());

            D4Packet write = BuildWritePacket(
                static_cast<ResetKey>(model.rkey),
                EepromWrite{static_cast<EepromAddress>(model.addresses[i]), value},
                model.wkey);
            seq.push_back(std::move(write));
        }
        return seq;
    }

    static bool containsSubseq(const D4Packet& hay, const unsigned char* needle, size_t n)
    {
        // i + n <= hay.size() keeps [i, i+n) in bounds, so std::equal never reads
        // past the buffer even for the last candidate offset.
        for (size_t i = 0; i + n <= hay.size(); ++i)
            if (std::equal(needle, needle + n, hay.begin() + i))
                return true;
        return false;
    }

    bool IsWritePacket(const D4Packet& p)
    {
        const uint8_t not_c   = static_cast<uint8_t>(~proto::WRITE_CMD & 0xFF);
        const uint8_t shift_c = static_cast<uint8_t>(((proto::WRITE_CMD >> 1) & 0x7F) | ((proto::WRITE_CMD << 7) & 0x80));
        return p.size() >= 15
            && p[6] == proto::FRAME && p[7] == proto::FRAME
            && p[12] == proto::WRITE_CMD
            && p[13] == not_c
            && p[14] == shift_c;
    }

    bool IsWriteAcknowledged(const D4Packet& response)
    {
        static const unsigned char OK[] = {0x3A, 0x34, 0x32, 0x3A, 0x4F, 0x4B, 0x3B}; // ":42:OK;"
        return containsSubseq(response, OK, sizeof(OK));
    }

    bool IsWriteRejected(const D4Packet& response)
    {
        static const unsigned char NG[] = {0x3A, 0x34, 0x32, 0x3A, 0x4E, 0x47, 0x3B}; // ":42:NG;"
        return containsSubseq(response, NG, sizeof(NG));
    }

    PayloadSequence ParseWiresharkText(std::string_view content)
    {
        PayloadSequence all;
        std::string text(content);

        std::regex arrayRegex(R"(\{([^}]+)\})");
        std::regex byteRegex(R"(0x[0-9a-fA-F]{1,2})");
        auto array_end = std::sregex_iterator();

        for (auto it = std::sregex_iterator(text.begin(), text.end(), arrayRegex); it != array_end; ++it)
        {
            std::string arrayContent = it->str(1);
            D4Packet pkt;

            for (auto b = std::sregex_iterator(arrayContent.begin(), arrayContent.end(), byteRegex); b != array_end; ++b)
            {
                unsigned char byte = static_cast<unsigned char>(std::stoul(b->str(), nullptr, 16));
                pkt.push_back(byte);
            }

            if (pkt.size() >= 27 && pkt[0] == 0x1B && pkt[1] == 0x00)
                pkt.erase(pkt.begin(), pkt.begin() + 27);

            if (!pkt.empty())
                all.push_back(std::move(pkt));
        }
        return all;
    }

} // namespace ewr::protocol
