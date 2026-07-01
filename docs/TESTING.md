# Testing EWR

The safety-critical byte logic lives in a pure, I/O-free core
(`include/ewr/protocol.h`), so it is tested entirely on the host — **no printer,
network, or USB required**. Three complementary techniques.

## Build & run

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure    # golden_test + property_test + executor_test
```

Run a binary directly for per-assertion output:

```bash
./build/golden_test
./build/property_test
./build/executor_test
```

## 1. Golden-byte tests — `tests/golden_test.cpp`

Pin the **exact** bytes the builders emit, so any change to the on-wire format
is caught. Anchored on a known vector (PX-7V: `rkey=1`, `wkey="Zvubnpsj"`,
`addr=58`). Covers the full 6-packet sequence, all five init frames, non-zero
reset values, the `reset[]` padding, and the Replay 27-byte USBPcap strip.

**Add a vector:** construct a `DbPrinterModel`, call `GenerateSequence`, then
`check("name", seq[i], Bytes{ ... })` with the expected bytes.

## 2. Property-based tests — `tests/property_test.cpp`

Assert **invariants** over thousands of randomized inputs (deterministic PRNG,
so failures reproduce). For every generated write packet: the D4 length equals
the packet size, rkey/address/inner-length are little-endian, credit is zero,
the `0x42/0xBD/0x21` checksum holds, and the packet decodes back to its inputs.
`ParseWiresharkText` is checked against an independent strip/drop reference
model.

**Add a property:** extend the relevant loop; keep it a pure invariant (no fixed
bytes — that is golden's job).

## 3. Executor / seam tests — `tests/executor_test.cpp`

Drive the shared `ProtocolExecutor` through a `FakeTransport` (an in-memory
`ITransport` that records sent packets and returns scripted device replies) — no
USB, no printer. This is where the **honest success gate** is pinned: success is
declared only when every EEPROM-write packet's own reply carries `:42:OK;`. The
headline case is the regression guard for the historical false-SUCCESS defect —
a run where every D4 handshake replies but the write is *not* acknowledged must
report failure. Also covers explicit `:42:NG;` rejection, partial multi-address
acks, and send-error short-circuit.

**Add a case:** build a `PayloadSequence`, script one `Drain()` reply per packet
(`:42:OK;` / `:42:NG;` / a handshake reply), run the executor, assert on
`ExecutionResult`.

## 4. Mutation testing — `scripts/mutation_test.py`

Proves the tests actually **bite**. It seeds deliberate defects into the
EEPROM-writing path (wrong command byte, non-zero credit, flipped endianness,
broken rotate, zeroed reset values, altered strip length), rebuilds, and
verifies the suite fails for each. A *surviving* mutant is a test blind spot.

```bash
python3 scripts/mutation_test.py       # expects: 10/10 killed
```

It restores every file and a clean build on exit. **Add a mutant:** append a
`(file, old, new, label)` tuple to `MUTATIONS`.

### Deeper: LLVM-level mutation testing with Mull

`scripts/mutation_test.py` is dependency-free (textual mutants) and runs
anywhere. For a far larger, *semantic* mutant set,
[Mull](https://github.com/mull-project/mull) mutates at the LLVM-IR level.
Prerequisites: `clang-19` and `mull-19` (LLVM 19).

```bash
scripts/mull.sh          # or: CLANGXX=clang++-19 scripts/mull.sh
```

It instruments only `src/protocol.cpp` (the pure byte builders + the ACK
classifiers `IsWritePacket` / `IsWriteAcknowledged` / `IsWriteRejected`), uses
`tests/property_test.cpp` as the oracle, and reads `mull.yml` (`cxx_all`
mutators). Current score: **89%** — 56 mutants, 6 survivors, all understood:

- a big-endian `>>`/`<<` swap in `pushBe16` that only differs for packets
  ≥ 256 bytes (which never occur — the high length byte is always `0x00`);
- two `cxx_remove_void_call` mutants that drop a `push_back` — Mull cannot kill
  these against a same-process oracle;
- two `cxx_replace_scalar_call` mutants that swap a `.size()` for the literal
  `42`, equivalent over the real input domain (write frames are always
  ≥ 19 bytes, and the substring loop bound only over-runs into a read that the
  oracle's inputs never distinguish);
- one `cxx_assign_const` on `match = false` inside the substring search that Mull
  reports as surviving but which the property suite provably **kills** (flipping
  it to `match = true` makes the ACK-token assertions fail) — a Mull-encoding
  false survivor, tracked under the "investigate Mull survivors" task.

## The iron rule

Never rename a protocol literal or unify a byte-writer before the golden test
covering it is green — and it must stay green afterward. The golden net is what
makes the rest of the refactor safe on real hardware.
