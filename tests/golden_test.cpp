// EWR golden-byte safety net (P0).
//
// Pins the EXACT on-wire output of the current pure builders so that every later
// refactor step (extract protocol.h, dedup transport, name constants) is provably
// byte-for-byte unchanged. NO hardware, network, or USB required.
//
// Iron rule for the refactor: do not rename a protocol literal or unify a
// byte-writer until this net is green, and it must stay green afterwards.

#include "ewr/generator.h"
#include "ewr/parser.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;

static int g_fail = 0;

static std::string hex(const Bytes& b)
{
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (unsigned char c : b)
        o << std::setw(2) << static_cast<int>(c) << ' ';
    return o.str();
}

static void check(const std::string& name, const Bytes& got, const Bytes& want)
{
    if (got == want)
    {
        std::cout << "  PASS  " << name << "\n";
    }
    else
    {
        ++g_fail;
        std::cout << "  FAIL  " << name << "\n"
                  << "        got : " << hex(got) << "\n"
                  << "        want: " << hex(want) << "\n";
    }
}

static void checkTrue(const std::string& name, bool cond)
{
    if (cond)
        std::cout << "  PASS  " << name << "\n";
    else
    {
        ++g_fail;
        std::cout << "  FAIL  " << name << "\n";
    }
}

int main()
{
    using namespace ewr;

    std::cout << "== Smart Protocol golden sequence (PX-7V shape) ==\n";

    DbPrinterModel m;
    m.name = "PX-7V";
    m.rkey = 1;
    m.wkey = "Zvubnpsj";
    m.addresses = {58};
    m.reset_values = {0};

    UniversalGenerator gen;
    auto seq = gen.GenerateSequence(m).value();

    checkTrue("sequence has 6 packets (3 prologue + 1x(grant,req,write))", seq.size() == 6);
    if (seq.size() == 6)
    {
        // EJL packet-mode init — the 3 leading 0x00 bytes are load-bearing ("RESTORED").
        check("seq[0] EJL packet-mode init", seq[0], Bytes{
            0x00, 0x00, 0x00, 0x1B, 0x01, 0x40, 0x45, 0x4A, 0x4C, 0x20, 0x31, 0x32, 0x38, 0x34,
            0x2E, 0x34, 0x0A, 0x40, 0x45, 0x4A, 0x4C, 0x0A, 0x40, 0x45, 0x4A, 0x4C, 0x0A});
        check("seq[1] D4 init", seq[1], Bytes{
            0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x10});
        check("seq[2] D4 open channel", seq[2], Bytes{
            0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00});
        check("seq[3] credit grant", seq[3], Bytes{
            0x00, 0x00, 0x00, 0x0B, 0x01, 0x00, 0x03, 0x02, 0x02, 0x00, 0x01});
        check("seq[4] credit request", seq[4], Bytes{
            0x00, 0x00, 0x00, 0x0D, 0x01, 0x00, 0x04, 0x02, 0x02, 0xFF, 0xFF, 0x00, 0x01});
        // |B EEPROM write: rkey=1, addr=58, value=0, wkey="Zvubnpsj".
        // c=0x42, ~c=0xBD, rotr1(c)=0x21; D4 len 0x001A (big-endian, = total), inner len 0x10 (little-endian).
        check("seq[5] EEPROM write |B", seq[5], Bytes{
            0x02, 0x02, 0x00, 0x1A, 0x00, 0x00, 0x7C, 0x7C, 0x10, 0x00, 0x01, 0x00, 0x42, 0xBD,
            0x21, 0x3A, 0x00, 0x00, 0x5A, 0x76, 0x75, 0x62, 0x6E, 0x70, 0x73, 0x6A});
    }

    std::cout << "\n== Non-zero reset value survives into the write byte ==\n";
    DbPrinterModel mnz = m;
    mnz.reset_values = {7}; // e.g. EP-301 carries non-zero resets — must never be hardcoded to 0
    auto seqnz = gen.GenerateSequence(mnz).value();
    checkTrue("write packet value byte (offset 17) == 0x07",
              seqnz.size() == 6 && seqnz[5].size() == 26 && seqnz[5][17] == 0x07);

    std::cout << "\n== reset_values padding (LoadDatabase guards GenerateSequence OOB) ==\n";
    {
        fs::path dbp = fs::temp_directory_path() / "ewr_golden_db.json";
        {
            std::ofstream f(dbp);
            f << R"({ "TESTPAD": { "rkey":1, "wkey":"Zvubnpsj", "addresses":[58,59], "reset":[0] } })";
        }
        UniversalGenerator g2;
        checkTrue("LoadDatabase ok", g2.LoadDatabase(dbp.string()).ok());
        auto models = g2.GetAvailableModels();
        checkTrue("one model loaded", models.size() == 1);
        if (models.size() == 1)
        {
            const auto& pm = models[0];
            checkTrue("addresses parsed [58,59]", pm.addresses == std::vector<uint16_t>{58, 59});
            checkTrue("reset_values padded to [0,0]", pm.reset_values == std::vector<uint8_t>{0, 0});
            checkTrue("padded model builds 9 packets with no OOB", gen.GenerateSequence(pm).value().size() == 9);
        }
        fs::remove(dbp);
    }

    std::cout << "\n== Replay parser: USBPcap 27-byte strip / drop-empty / keep-others ==\n";
    {
        auto arr = [](const Bytes& b) {
            std::ostringstream o;
            o << "static const unsigned char pkt[" << b.size() << "] = {\n";
            o << std::hex;
            for (size_t i = 0; i < b.size(); ++i)
            {
                o << "0x" << static_cast<int>(b[i]);
                if (i + 1 < b.size())
                    o << ", ";
            }
            o << " };\n\n";
            return o.str();
        };

        Bytes a(30, 0x00); a[0] = 0x1b; a[1] = 0x00; a[27] = 0xAA; a[28] = 0xBB; a[29] = 0xCC; // strip27 -> {AA,BB,CC}
        Bytes b(27, 0x00); b[0] = 0x1b; b[1] = 0x00;                                            // strip27 -> empty -> dropped
        Bytes c{0x11, 0x22, 0x33, 0x44};                                                       // no 0x1B prefix -> kept
        Bytes d(26, 0x00); d[0] = 0x1b; d[1] = 0x00;                                            // size<27 -> NOT stripped -> kept

        fs::path dump = fs::temp_directory_path() / "ewr_golden_replay.c";
        {
            std::ofstream f(dump);
            f << arr(a) << arr(b) << arr(c) << arr(d);
        }

        auto pk = ewr::ParseWiresharkDump(dump.string()).value();
        checkTrue("3 packets survive (empty-after-strip dropped)", pk.size() == 3);
        if (pk.size() == 3)
        {
            check("replay[0] stripped to last 3 bytes", pk[0], Bytes{0xAA, 0xBB, 0xCC});
            check("replay[1] non-0x1B packet kept intact", pk[1], Bytes{0x11, 0x22, 0x33, 0x44});
            checkTrue("replay[2] 26-byte 0x1B00 packet NOT stripped", pk[2].size() == 26 && pk[2][0] == 0x1b);
        }
        fs::remove(dump);
    }

    std::cout << "\n"
              << (g_fail == 0 ? "ALL GOLDEN TESTS PASSED"
                              : (std::to_string(g_fail) + " GOLDEN TEST(S) FAILED"))
              << "\n";
    return g_fail == 0 ? 0 : 1;
}
