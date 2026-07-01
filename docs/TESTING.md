# Testing EWR

The safety-critical byte logic lives in a pure, I/O-free core
(`include/ewr/protocol.h`), so it is tested entirely on the host — **no printer,
network, or USB required**. Three complementary techniques.

## Build & run

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure    # runs golden_test + property_test
```

Run a binary directly for per-assertion output:

```bash
./build/golden_test
./build/property_test
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

## 3. Mutation testing — `scripts/mutation_test.py`

Proves the tests actually **bite**. It seeds deliberate defects into the
EEPROM-writing path (wrong command byte, non-zero credit, flipped endianness,
broken rotate, zeroed reset values, altered strip length), rebuilds, and
verifies the suite fails for each. A *surviving* mutant is a test blind spot.

```bash
python3 scripts/mutation_test.py       # expects: 10/10 killed
```

It restores every file and a clean build on exit. **Add a mutant:** append a
`(file, old, new, label)` tuple to `MUTATIONS`.

## The iron rule

Never rename a protocol literal or unify a byte-writer before the golden test
covering it is green — and it must stay green afterward. The golden net is what
makes the rest of the refactor safe on real hardware.
