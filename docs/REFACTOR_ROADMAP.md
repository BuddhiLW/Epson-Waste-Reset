# EWR Refactor Roadmap

Applying SOLID + Stratified Design + CPPB + DDD to the ~1200-LOC CLI, sized
proportionately for a single-user hobby tool. The pure-core extraction is the
one lever all four frameworks agree on.

## Status

| Step | What | Status |
|---|---|---|
| **P0** | Golden-byte safety net | ✅ done |
| **P1** | Extract pure protocol core + value objects | ✅ core done (`DbPrinterModel`→`PrinterProfile` rename deferred) |
| **P2** | `Result<T,Error>` seam, honest errors, filter empty-address models from the menu | ⬜ todo |
| **P3** | `ITransport` + owned device, one shared executor, the real success fix | ⬜ todo |
| **P-side** | OTA download hardening (atomic temp-rename, `fclose(NULL)` UB) | ⬜ todo |

## Hardware-safety invariants (must survive every refactor)

Pinned by `tests/golden_test.cpp` + `tests/property_test.cpp`:

- EJL packet-mode init keeps its **3 leading `0x00`** bytes.
- D4 init / open-channel / credit-grant / credit-request frames are verbatim.
- Per address: `(credit_grant, credit_request, write)` in that order.
- Write packet: D4 `02 02`, **big-endian total length**, **credit `0x00`**,
  control `0x00`; Epson `7C 7C`, **little-endian** inner length / rkey / address;
  `0x42` cmd, `~0x42 = 0xBD`, `rotr1(0x42) = 0x21`; then the reset value and wkey.
- Replay: strip exactly 27 bytes **only** when `size >= 27 && [0]==0x1B &&
  [1]==0x00`; drop packets that become empty after stripping.
- Reset values are data-driven and **not always 0** (e.g. EP-301).
- *(P3)* Preserve the 100 ms inter-packet delay, 250 ms ACK drain, and the
  kernel-driver detach / claim / reattach lifecycle.

## The standout defect (fixed in P3)

Success is currently declared on **any** inbound byte. The D4 handshake packets
always reply, so the tool can report `SUCCESS` even if every EEPROM write was
rejected. The real per-write reply is the ASCII token `:42:OK;` (vs `:42:NG;`).
P3 adds a pure `IsWriteAcknowledged` that parses it and gates SUCCESS on every
write.

## Out of scope (proportionality)

No DI container; no per-primitive strong types (`wkey` stays a string); no eager
polymorphism over the 1450-model catalog; no macOS backend yet; no
hexagonal/CQRS ceremony; don't rewrite the working USB scan; don't touch
`models/*.c` or `scripts/update_db.py`.
