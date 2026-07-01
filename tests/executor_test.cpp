// EWR executor tests: prove the success gate is honest.
//
// The historical defect declared SUCCESS on ANY inbound byte, so a reset was
// reported successful even when every EEPROM write was rejected (the D4
// handshake packets always reply). These tests drive ProtocolExecutor through a
// FakeTransport with scripted device replies — no hardware — and assert that
// success requires every write packet's own reply to carry ":42:OK;".

#include "ewr/executor.h"
#include "ewr/protocol.h"
#include "ewr/generator.h"
#include "ewr/domain.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

static int g_fail = 0;
static void check(bool c, const std::string& msg)
{
    if (!c) { ++g_fail; std::cout << "  FAIL  " << msg << "\n"; }
}

struct FakeTransport : ewr::ITransport
{
    std::vector<Bytes> sent;
    std::vector<Bytes> responses; // scripted; the i-th Drain returns responses[i]
    size_t             idx    = 0;
    bool               sendOk = true;

    bool Send(const Bytes& packet) override { sent.push_back(packet); return sendOk; }
    Bytes Drain() override { return idx < responses.size() ? responses[idx++] : Bytes{}; }
};

static Bytes okFrame()  { return {0x02,0x02,0x00,0x10,0x00,0x01,0x7c,0x7c,0x3a,0x34,0x32,0x3a,0x4f,0x4b,0x3b,0x0c}; }
static Bytes ngFrame()  { return {0x02,0x02,0x00,0x10,0x00,0x01,0x7c,0x7c,0x3a,0x34,0x32,0x3a,0x4e,0x47,0x3b,0x0c}; }
static Bytes d4reply()  { return {0x00,0x00,0x00,0x0a,0x01,0x00,0x83,0x00,0x02,0x02}; }

static ewr::PayloadSequence resetSeq(uint16_t rkey, const std::string& wkey, std::vector<uint16_t> addrs)
{
    ewr::DbPrinterModel m;
    m.name = "T";
    m.rkey = rkey;
    m.wkey = wkey;
    m.addresses = std::move(addrs);
    m.reset_values.assign(m.addresses.size(), 0);
    return ewr::protocol::BuildResetSequence(m);
}

// Script one Drain reply per packet: writeReply for write packets, d4reply otherwise.
static std::vector<Bytes> scriptReplies(const ewr::PayloadSequence& seq, const Bytes& writeReply)
{
    std::vector<Bytes> r;
    for (const auto& pkt : seq)
        r.push_back(ewr::protocol::IsWritePacket(pkt) ? writeReply : d4reply());
    return r;
}

int main()
{
    using namespace ewr;
    const auto NODELAY = std::chrono::milliseconds(0);

    std::cout << "== Executor: honest success gate ==\n";

    // 1) Every write acknowledged -> SUCCESS, packets forwarded verbatim.
    {
        auto seq = resetSeq(1, "Zvubnpsj", {58});
        FakeTransport t;
        t.responses = scriptReplies(seq, okFrame());
        ProtocolExecutor ex(t, nullptr, NODELAY);
        auto r = ex.Run(seq);
        check(r.success, "all writes acked -> success");
        check(r.writesTotal == 1 && r.writesAcked == 1 && r.writesRejected == 0, "write accounting (ok case)");
        check(t.sent.size() == seq.size(), "executor sent every packet");
        check(t.sent == seq, "executor must not mutate packet bytes");
    }

    // 2) THE false-SUCCESS REGRESSION GUARD: every handshake replies, but the
    //    write's own reply carries no ":42:OK;" -> must be FAILURE.
    {
        auto seq = resetSeq(1, "Zvubnpsj", {58});
        FakeTransport t;
        t.responses = scriptReplies(seq, d4reply()); // write gets a non-token reply
        ProtocolExecutor ex(t, nullptr, NODELAY);
        auto r = ex.Run(seq);
        check(!r.success, "handshake-only replies must NOT report success (false-SUCCESS guard)");
        check(r.writesTotal == 1 && r.writesAcked == 0, "write accounting (unacked case)");
    }

    // 3) Write explicitly rejected with ":42:NG;" -> FAILURE.
    {
        auto seq = resetSeq(1, "Zvubnpsj", {58});
        FakeTransport t;
        t.responses = scriptReplies(seq, ngFrame());
        ProtocolExecutor ex(t, nullptr, NODELAY);
        auto r = ex.Run(seq);
        check(!r.success, ":42:NG; must fail");
        check(r.writesRejected == 1 && r.writesTotal == 1, "rejection counted");
    }

    // 4) Multi-address: 3/3 acked -> success; 2/3 acked -> failure.
    {
        auto seq = resetSeq(7, "abcd", {10, 20, 30});
        check(seq.size() == 3 + 3 * 3, "1-addr triple layout (12 packets)");

        FakeTransport tAll;
        tAll.responses = scriptReplies(seq, okFrame());
        ProtocolExecutor exAll(tAll, nullptr, NODELAY);
        auto rAll = exAll.Run(seq);
        check(rAll.success && rAll.writesTotal == 3 && rAll.writesAcked == 3, "3/3 writes acked -> success");

        FakeTransport tOne;
        tOne.responses = scriptReplies(seq, okFrame());
        bool droppedFirst = false;
        for (size_t i = 0; i < seq.size(); ++i)
            if (protocol::IsWritePacket(seq[i]) && !droppedFirst) { tOne.responses[i] = d4reply(); droppedFirst = true; }
        ProtocolExecutor exOne(tOne, nullptr, NODELAY);
        auto rOne = exOne.Run(seq);
        check(!rOne.success && rOne.writesTotal == 3 && rOne.writesAcked == 2, "2/3 writes acked -> failure");
    }

    // 5) A transport send failure short-circuits and reports sendError.
    {
        auto seq = resetSeq(1, "Zvubnpsj", {58});
        FakeTransport t;
        t.sendOk = false;
        ProtocolExecutor ex(t, nullptr, NODELAY);
        auto r = ex.Run(seq);
        check(!r.success && r.sendError, "send failure -> failure + sendError");
        check(t.sent.size() == 1, "send failure short-circuits after first packet");
    }

    std::cout << "\n"
              << (g_fail == 0 ? "ALL EXECUTOR TESTS PASSED"
                              : (std::to_string(g_fail) + " EXECUTOR CHECK(S) FAILED"))
              << "\n";
    return g_fail == 0 ? 0 : 1;
}
