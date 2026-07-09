# Testing EWR

The safety-critical byte logic lives in a pure, I/O-free core
(`include/ewr/protocol.h`), so it is tested entirely on the host — **no printer,
network, or USB required**. Three complementary techniques.

## Toolchain (Debian/Ubuntu)

The complete C/C++ compiler chain for building the project and running the
LLVM-level mutation tests:

```bash
sudo apt-get install build-essential clang-19 clang-tools-19 lld-19 libstdc++-14-dev libc6-dev
```

Plus the project's build/runtime dependencies — `cmake pkgconf libusb-1.0-dev
libcurl4-openssl-dev` — and `mull-19` (from the
[Mull APT repo](https://github.com/mull-project/mull)) for the deeper mutation
pass. On a machine with several GCC versions installed, `scripts/mull.sh`
auto-selects the newest one that actually ships `libstdc++` headers (override
with `MULL_GCC_INSTALL_DIR=`), so a headerless `gcc-14` stub won't break the
Clang build with a `'cstdint' file not found`.

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
mutators). Current score: **97%** — 47 mutants, **1** survivor, an accepted
**equivalent mutant**:

- `cxx_replace_scalar_call` turns the `p.size() >= 15` length guard in
  `IsWritePacket` into `42 >= 15` (always true). It is unkillable by any
  well-defined test: the two forms differ only for buffers shorter than 15
  bytes, and observing that difference requires reading `p[6..14]` — out of
  bounds for a sub-15-byte buffer, i.e. the exact undefined behavior the guard
  exists to prevent. `mull.yml` documents it; `tests/property_test.cpp` pins the
  guard's short-circuit behavior with a 14-byte case.

Getting from the earlier 89% (6 survivors) to here closed the real oracle gaps:
a ≥ 256-byte packet now exercises the big-endian high length byte (killing the
`pushBe16` `>>`/`<<` swap), an ACK token placed past byte 42 kills a
`.size()`→const scan-window mutant, and hoisting two `push_back` arguments into
named locals plus rewriting the substring search with `std::equal` let Mull's
`cxx_remove_void_call` / `cxx_assign_const` mutants become killable (the old
multi-line/flag forms were Mull-encoding false survivors).

## 5. Byte-parity vs. the upstream reference — `tests/byte_parity_check.cpp`

The one question that matters for hardware safety: **does the refactor change any
byte that reaches an EEPROM?** This harness answers it directly. It embeds the
*verbatim* upstream `GenerateSequence` / `GenerateWritePacket` algorithm
(namespace `legacy`) and diffs its output against
`ewr::protocol::BuildResetSequence` for **every** model in `database.json`.

```bash
./build/byte_parity_check database.json    # writes verdict to stdout
```

Latest run: **1337** models with addresses · **39 978** packets · **669 352
bytes** compared · **0** mismatches. The full log is committed at
`tests/BYTE_PARITY.txt`. Because the upstream builder is the code that has been
exercised on real printers, byte-for-byte identity means upstream's on-hardware
validation transfers to the refactored output unchanged — without needing a
printer here. (OTA robustness and the Canon path are intentionally *not* in
scope of this equivalence: the Canon reset has no upstream on-hardware oracle and
is gated as `CommitPending` / unverified by design.)

### Verify it yourself, zero-trust — `tests/verify_against_upstream.sh`

`byte_parity_check` embeds a copy of the upstream algorithm, so a skeptic still
has to trust that copy. This script removes that assumption: it **fetches
upstream's own `src/generator.cpp` from `github.com/RxNaison/Epson-Waste-Reset`**,
compiles it into an oracle binary, compiles a dumper from *this* tree, runs both
over the same `database.json`, and `diff`s the emitted bytes. The only thing you
trust is upstream's own source.

```bash
tests/verify_against_upstream.sh            # ref=main, ./database.json
tests/verify_against_upstream.sh main       # or pin any upstream ref
# prints the upstream generator.cpp sha256, then:
# RESULT: IDENTICAL — every emitted byte matches upstream over 1337 models.
```

## The iron rule

Never rename a protocol literal or unify a byte-writer before the golden test
covering it is green — and it must stay green afterward. The golden net is what
makes the rest of the refactor safe on real hardware.
