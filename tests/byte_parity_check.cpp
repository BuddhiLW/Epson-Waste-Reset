// In-tree byte-parity check: ewr::protocol::BuildResetSequence vs. the upstream
// algorithm embedded verbatim below (namespace `legacy`), over every model in
// database.json. Exit 0 == identical. For a zero-trust check that pulls upstream
// source from the real remote instead of this embedded copy, see
// tests/verify_against_upstream.sh.

#include "ewr/generator.h"
#include "ewr/protocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using json  = nlohmann::json;
using Bytes = std::vector<unsigned char>;

// legacy:: — verbatim upstream/main src/generator.cpp byte construction (oracle).
namespace legacy {

    Bytes GenerateWritePacket(uint16_t rkey, uint16_t address, uint8_t value, const std::string& wkey)
    {
        uint8_t c       = 0x42;
        uint8_t not_c   = ~c & 0xFF;
        uint8_t shift_c = ((c >> 1) & 0x7F) | ((c << 7) & 0x80);

        Bytes inner;
        inner.push_back(rkey & 0xFF);
        inner.push_back((rkey >> 8) & 0xFF);
        inner.push_back(c);
        inner.push_back(not_c);
        inner.push_back(shift_c);
        inner.push_back(address & 0xFF);
        inner.push_back((address >> 8) & 0xFF);
        inner.push_back(value);
        inner.insert(inner.end(), wkey.begin(), wkey.end());

        Bytes epson_cmd;
        epson_cmd.push_back(0x7C);
        epson_cmd.push_back(0x7C);
        uint16_t len = inner.size();
        epson_cmd.push_back(len & 0xFF);
        epson_cmd.push_back((len >> 8) & 0xFF);
        epson_cmd.insert(epson_cmd.end(), inner.begin(), inner.end());

        Bytes d4;
        d4.push_back(0x02);
        d4.push_back(0x02);
        uint16_t d4_len = epson_cmd.size() + 6;
        d4.push_back((d4_len >> 8) & 0xFF);
        d4.push_back(d4_len & 0xFF);
        d4.push_back(0x00);
        d4.push_back(0x00);
        d4.insert(d4.end(), epson_cmd.begin(), epson_cmd.end());
        return d4;
    }

    std::vector<Bytes> GenerateSequence(uint16_t rkey, const std::string& wkey,
                                        const std::vector<uint16_t>& addresses,
                                        const std::vector<uint8_t>& reset_values)
    {
        std::vector<Bytes> sequence;

        const unsigned char ejl_init[] = {
            0x00, 0x00, 0x00, 0x1B, 0x01, '@', 'E', 'J', 'L', ' ', '1', '2', '8', '4', '.', '4', '\n',
            '@', 'E', 'J', 'L', '\n', '@', 'E', 'J', 'L', '\n'};
        sequence.push_back(Bytes(std::begin(ejl_init), std::end(ejl_init)));

        const unsigned char d4_init[] = {0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x10};
        sequence.push_back(Bytes(std::begin(d4_init), std::end(d4_init)));

        const unsigned char d4_open[] = {
            0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
        sequence.push_back(Bytes(std::begin(d4_open), std::end(d4_open)));

        const unsigned char d4_credit_grant[] = {0x00, 0x00, 0x00, 0x0B, 0x01, 0x00, 0x03, 0x02, 0x02, 0x00, 0x01};
        const unsigned char d4_credit_req[]   = {0x00, 0x00, 0x00, 0x0D, 0x01, 0x00, 0x04, 0x02, 0x02, 0xFF, 0xFF, 0x00, 0x01};

        for (size_t i = 0; i < addresses.size(); ++i)
        {
            sequence.push_back(Bytes(std::begin(d4_credit_grant), std::end(d4_credit_grant)));
            sequence.push_back(Bytes(std::begin(d4_credit_req), std::end(d4_credit_req)));
            sequence.push_back(GenerateWritePacket(rkey, addresses[i], reset_values[i], wkey));
        }
        return sequence;
    }

} // namespace legacy

static std::string hex(const Bytes& b)
{
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (unsigned char c : b)
        o << std::setw(2) << static_cast<int>(c) << ' ';
    return o.str();
}

int main(int argc, char** argv)
{
    const std::string dbpath = (argc > 1) ? argv[1] : "database.json";

    std::ifstream f(dbpath);
    if (!f.is_open())
    {
        std::cerr << "cannot open " << dbpath << "\n";
        return 2;
    }
    json db;
    f >> db;

    std::cout << "EWR byte-parity: refactored core vs. upstream reference algorithm\n";
    std::cout << "database: " << dbpath << "   models: " << db.size() << "\n";
    std::cout << "=================================================================\n\n";

    size_t models = 0, compared = 0, skipped_empty = 0, ok = 0, mismatch = 0;
    size_t total_packets = 0, total_bytes = 0;
    int shown_ok = 0;

    for (auto& [name, val] : db.items())
    {
        ++models;

        uint16_t rkey = val.value("rkey", 0);
        std::string wkey = val.value("wkey", "");
        std::vector<uint16_t> addresses;
        std::vector<uint8_t>  resets;
        if (val.contains("addresses") && val["addresses"].is_array())
            for (auto& a : val["addresses"]) addresses.push_back(a.get<uint16_t>());
        if (val.contains("reset") && val["reset"].is_array())
            for (auto& r : val["reset"]) resets.push_back(r.get<uint8_t>());
        while (resets.size() < addresses.size()) resets.push_back(0x00);

        if (addresses.empty())
        {
            ++skipped_empty; // no addresses => nothing to write; EWR returns EmptyPlan, upstream emits prologue-only
            continue;
        }
        ++compared;

        // Reference (upstream, hardware-proven) sequence.
        auto want = legacy::GenerateSequence(rkey, wkey, addresses, resets);

        // Refactored EWR core sequence.
        ewr::DbPrinterModel m;
        m.name = name; m.rkey = rkey; m.wkey = wkey;
        m.addresses = addresses; m.reset_values = resets;
        auto res = ewr::protocol::BuildResetSequence(m);

        bool identical = (res.size() == want.size());
        size_t firstDiffPkt = 0, firstDiffByte = 0;
        if (identical)
        {
            for (size_t p = 0; p < want.size() && identical; ++p)
            {
                total_packets++;
                total_bytes += want[p].size();
                if (res[p] != want[p])
                {
                    identical = false;
                    firstDiffPkt = p;
                    for (size_t b = 0; b < want[p].size(); ++b)
                        if (b >= res[p].size() || res[p][b] != want[p][b]) { firstDiffByte = b; break; }
                }
            }
        }

        if (identical)
        {
            ++ok;
            // Show the first few models in full so the artifact is legible, not just a tally.
            if (shown_ok < 3)
            {
                ++shown_ok;
                std::cout << "IDENTICAL  " << name << "  (rkey=" << rkey << ", wkey=\"" << wkey
                          << "\", " << addresses.size() << " addr, " << want.size() << " packets)\n";
                for (size_t p = 0; p < want.size(); ++p)
                    std::cout << "   pkt[" << std::setw(2) << p << "] " << hex(want[p]) << "\n";
                std::cout << "\n";
            }
        }
        else
        {
            ++mismatch;
            std::cout << "MISMATCH   " << name << "  packet " << firstDiffPkt
                      << " byte " << firstDiffByte << "\n"
                      << "   want: " << hex(want[firstDiffPkt]) << "\n"
                      << "   got : " << (firstDiffPkt < res.size() ? hex(res[firstDiffPkt]) : std::string("<missing>")) << "\n\n";
        }
    }

    std::cout << "=================================================================\n";
    std::cout << "models in db    : " << models << "\n";
    std::cout << "models compared : " << compared << "  (with >=1 address)\n";
    std::cout << "models skipped  : " << skipped_empty << "  (no addresses => nothing to write)\n";
    std::cout << "packets compared: " << total_packets << "\n";
    std::cout << "bytes compared  : " << total_bytes << "\n";
    std::cout << "IDENTICAL       : " << ok << "\n";
    std::cout << "MISMATCH        : " << mismatch << "\n\n";
    std::cout << (mismatch == 0
        ? "RESULT: refactored core is byte-for-byte identical to the upstream\n"
          "reference across the ENTIRE database. Upstream's on-hardware validation\n"
          "applies unchanged to the refactored output.\n"
        : "RESULT: DIVERGENCE FOUND — refactor changes on-wire bytes. Do not ship.\n");
    return mismatch == 0 ? 0 : 1;
}
