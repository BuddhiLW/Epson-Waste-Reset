# EWR Refactor Roadmap

Applying SOLID + Stratified Design + CPPB + DDD to the ~1200-LOC CLI, sized
proportionately for a single-user hobby tool. The pure-core extraction is the
one lever all four frameworks agree on.

## Status

| Step | What | Status |
|---|---|---|
| **P0** | Golden-byte safety net | ✅ done |
| **P1** | Extract pure protocol core + value objects | ✅ core done (`DbPrinterModel`→`PrinterProfile` rename deferred) |
| **P2** | `Result<T,Error>` seam, honest errors, filter empty-address models from the menu | ✅ done |
| **P3** | `ITransport` + owned device, one shared executor, the real success fix | ✅ done |
| **P-side** | OTA download hardening (atomic temp-rename + JSON content gate, `fclose(NULL)` UB) | ✅ done |
| **P4** | `IResetProtocol` vendor seam — Epson as one concrete impl (LSP/DIP/OCP) | ✅ done — see [ADR 0001](adr/0001-multi-vendor-reset-protocol.md) |
| **P5** | (reframed) OCP validation via a second `IResetProtocol` | ✅ covered by the `StubProtocol` executor test; a `ReplayProtocol` was rejected (would weaken Epson-dump ACK gating — see ADR 0001) |
| **P6** | Vendor-neutral device discovery + registry | ⬜ deferred (YAGNI for one vendor; per-platform discovery needs a Windows build) |
| **P7** | Generalize the reset-DB schema behind a vendor discriminator | ⬜ deferred (data-dependent) |

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

Success *used to be* declared on **any** inbound byte. The D4 handshake packets
always reply, so the tool could report `SUCCESS` even if every EEPROM write was
rejected. The real per-write reply is the ASCII token `:42:OK;` (vs `:42:NG;`),
confirmed against a live `ewr_trace.log` capture
(`02 02 00 10 00 01 7c 7c :42:OK; 0c`).

P3 fixed this. The pure core gained `protocol::IsWritePacket` /
`IsWriteAcknowledged` / `IsWriteRejected`; the new shared `ProtocolExecutor`
gates SUCCESS on **every** write packet's own `:42:OK;`, and `tests/executor_test.cpp`
drives it through a `FakeTransport` so the regression can never come back
without a red test.

## P3 shape (SOLID / CPPB / DDD)

- **`ITransport{Send,Drain}`** (`include/ewr/transport.h`) — the byte-pipe seam.
  `ConnectEpsonPrinter()` is a factory returning an owning `unique_ptr` whose
  destructor releases the interface, reattaches the kernel driver, and tears
  libusb down (**RAII replaced `DisconnectPrinter`**).
- **`ProtocolExecutor`** (`include/ewr/executor.h`, `src/executor.cpp`) — one
  platform-independent send → 100 ms settle → 250 ms drain → trace → gate loop,
  shared by both back-ends.
- The mutable module globals `EP_IN` / `EP_OUT` / `TARGET_INTERFACE` became
  per-instance members of `LibUsbTransport`, which also fixed a latent
  stale-endpoint bug (the old connect guard tested the globals, not the
  per-iteration locals, so a second Epson device could be claimed on the first
  device's interface/endpoints).

## Out of scope (proportionality)

No DI container; no per-primitive strong types (`wkey` stays a string); no eager
polymorphism over the 1450-model catalog; no macOS backend yet; no
hexagonal/CQRS ceremony; don't rewrite the working USB scan; don't touch
`models/*.c` or `scripts/update_db.py`.
