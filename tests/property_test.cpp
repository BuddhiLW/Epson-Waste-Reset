// EWR property-based tests over the pure protocol core.
//
// Instead of pinning fixed vectors (that is golden_test's job), these assert
// STRUCTURAL INVARIANTS that must hold for randomized inputs, and round-trip
// every generated write packet back to its inputs. Deterministic PRNG (fixed
// seed) so failures are reproducible. No framework, no I/O, no hardware.

#include "ewr/generator.h"
#include "ewr/parser.h"
#include "ewr/protocol.h"
#include "ewr/proto.h"
#include "ewr/domain.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

static int         g_fail = 0;
static const char* g_case = "";

struct Rng
{
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9e3779b9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    uint32_t upto(uint32_t n) { return n ? next() % n : 0; } // [0, n)
};

static void fail(const std::string& msg)
{
    ++g_fail;
    std::cout << "  FAIL  [" << g_case << "] " << msg << "\n";
}

static uint16_t le16(const Bytes& p, size_t i) { return static_cast<uint16_t>(p[i] | (p[i + 1] << 8)); }
static uint16_t be16(const Bytes& p, size_t i) { return static_cast<uint16_t>((p[i] << 8) | p[i + 1]); }

static bool eq(const Bytes& a, const unsigned char* b, size_t n)
{
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// A write packet must decode back to its inputs and satisfy every framing invariant.
static void checkWritePacket(const Bytes& p, uint16_t rkey, uint16_t addr, uint8_t value, const std::string& wkey)
{
    auto req = [&](bool c, const char* m) { if (!c) fail(m); };
    if (p.size() < 18) { fail("write packet shorter than 18 bytes"); return; }
    req(p[0] == 0x02 && p[1] == 0x02, "D4 psid/ssid != 02 02");
    req(be16(p, 2) == p.size(), "D4 length (BE) != total packet size");
    req(p[4] == 0x00, "D4 credit byte != 0 (overflow guard)");
    req(p[5] == 0x00, "D4 control byte != 0");
    req(p[6] == 0x7C && p[7] == 0x7C, "epson frame != 7C 7C");
    req(le16(p, 8) == p.size() - 10, "inner length (LE) != inner size");
    req(le16(p, 10) == rkey, "rkey (LE) mismatch");
    req(p[12] == 0x42, "write cmd != 0x42");
    req(p[13] == 0xBD, "~cmd != 0xBD");
    req(p[14] == 0x21, "rotr(cmd) != 0x21");
    req(le16(p, 15) == addr, "address (LE) mismatch");
    req(p[17] == value, "reset value mismatch");
    req(p.size() == 18 + wkey.size(), "packet size != 18 + wkey length");
    req(std::string(p.begin() + 18, p.end()) == wkey, "wkey tail mismatch");
}

int main()
{
    using namespace ewr;
    Rng rng(0x1234abcdu);

    const int SEQ_ITERS   = 3000;
    const int WRITE_ITERS = 2000;
    const int PARSE_ITERS = 800;

    std::cout << "== Property: BuildResetSequence structure + per-write round-trip (" << SEQ_ITERS << " cases) ==\n";
    for (int it = 0; it < SEQ_ITERS && g_fail == 0; ++it)
    {
        g_case = "reset-seq";
        DbPrinterModel m;
        m.rkey = static_cast<uint16_t>(rng.next() & 0xFFFF);

        uint32_t A = rng.upto(6); // 0..5 addresses
        for (uint32_t i = 0; i < A; ++i)
            m.addresses.push_back(static_cast<uint16_t>(rng.next() & 0xFFFF));

        uint32_t R = rng.upto(A + 1); // 0..A reset values -> exercises the pad path
        for (uint32_t i = 0; i < R; ++i)
            m.reset_values.push_back(static_cast<uint8_t>(rng.next() & 0xFF));

        uint32_t W = 1 + rng.upto(12);
        for (uint32_t i = 0; i < W; ++i)
            m.wkey.push_back(static_cast<char>('a' + rng.upto(26)));

        auto seq = protocol::BuildResetSequence(m);

        if (seq.size() != 3 + 3 * A) { fail("sequence length != 3 + 3*addresses"); break; }
        if (!eq(seq[0], proto::EJL_PACKET_MODE_INIT.data(), proto::EJL_PACKET_MODE_INIT.size())) fail("prologue[0] != EJL init");
        if (!eq(seq[1], proto::D4_INIT.data(), proto::D4_INIT.size())) fail("prologue[1] != D4 init");
        if (!eq(seq[2], proto::D4_OPEN_CHANNEL.data(), proto::D4_OPEN_CHANNEL.size())) fail("prologue[2] != D4 open");

        for (uint32_t i = 0; i < A && g_fail == 0; ++i)
        {
            const auto& grant = seq[3 + 3 * i + 0];
            const auto& creq  = seq[3 + 3 * i + 1];
            const auto& wr    = seq[3 + 3 * i + 2];
            if (!eq(grant, proto::D4_CREDIT_GRANT.data(), proto::D4_CREDIT_GRANT.size())) fail("credit grant frame mismatch");
            if (!eq(creq, proto::D4_CREDIT_REQUEST.data(), proto::D4_CREDIT_REQUEST.size())) fail("credit request frame mismatch");
            uint8_t expVal = (i < m.reset_values.size()) ? m.reset_values[i] : 0x00;
            checkWritePacket(wr, m.rkey, m.addresses[i], expVal, m.wkey);
        }
    }
    if (g_fail == 0) std::cout << "  PASS  all sequence properties held\n";

    std::cout << "\n== Property: BuildWritePacket direct round-trip (" << WRITE_ITERS << " cases) ==\n";
    for (int it = 0; it < WRITE_ITERS && g_fail == 0; ++it)
    {
        g_case = "write-packet";
        uint16_t rkey = static_cast<uint16_t>(rng.next() & 0xFFFF);
        uint16_t addr = static_cast<uint16_t>(rng.next() & 0xFFFF);
        uint8_t  val  = static_cast<uint8_t>(rng.next() & 0xFF);
        std::string wkey;
        uint32_t W = 1 + rng.upto(12);
        for (uint32_t i = 0; i < W; ++i)
            wkey.push_back(static_cast<char>('A' + rng.upto(26)));

        auto p = protocol::BuildWritePacket(static_cast<ResetKey>(rkey), EepromWrite{static_cast<EepromAddress>(addr), val}, wkey);
        checkWritePacket(p, rkey, addr, val, wkey);
    }
    if (g_fail == 0) std::cout << "  PASS  all write-packet properties held\n";

    std::cout << "\n== Property: ParseWiresharkText matches a reference strip model (" << PARSE_ITERS << " cases) ==\n";
    for (int it = 0; it < PARSE_ITERS && g_fail == 0; ++it)
    {
        g_case = "parse";
        uint32_t K = 1 + rng.upto(5);
        std::vector<Bytes> arrays;
        std::ostringstream text;

        for (uint32_t k = 0; k < K; ++k)
        {
            uint32_t len = 2 + rng.upto(38); // 2..39 -> sometimes >=27
            Bytes a;
            for (uint32_t i = 0; i < len; ++i)
                a.push_back(static_cast<unsigned char>(rng.next() & 0xFF));
            if (rng.upto(2) == 0) { a[0] = 0x1B; a[1] = 0x00; } // frequently trigger the strip predicate

            arrays.push_back(a);

            text << "static const unsigned char pkt" << k << "[" << len << "] = { ";
            for (uint32_t i = 0; i < len; ++i)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "0x%x", static_cast<int>(a[i]));
                text << buf;
                if (i + 1 < len) text << ", ";
            }
            text << " };\n";
        }

        // Reference model of the strip/drop rule.
        std::vector<Bytes> expected;
        for (Bytes a : arrays)
        {
            if (a.size() >= 27 && a[0] == 0x1B && a[1] == 0x00)
                a.erase(a.begin(), a.begin() + 27);
            if (!a.empty()) expected.push_back(a);
        }

        auto got = protocol::ParseWiresharkText(text.str());
        if (got.size() != expected.size()) { fail("parsed packet count != reference"); break; }
        for (size_t i = 0; i < got.size(); ++i)
            if (got[i] != expected[i]) { fail("parsed packet bytes != reference"); break; }
    }
    if (g_fail == 0) std::cout << "  PASS  all parse properties held\n";

    std::cout << "\n"
              << (g_fail == 0 ? "ALL PROPERTY TESTS PASSED"
                              : (std::to_string(g_fail) + " PROPERTY CHECK(S) FAILED"))
              << "\n";
    return g_fail == 0 ? 0 : 1;
}
