# ADR 0001 — Multi-vendor reset via `IResetProtocol`

Status: **Accepted** (P4 implemented) · Date: 2026-07-01

## Context

EWR resets Epson waste-ink counters over IEEE-1284.4 (D4). The question is
whether the design can serve other vendors (Canon, Brother, HP, …) with Epson as
*one* implementation rather than the only path — using SOLID (LSP / DIP / OCP)
where it pays for a single-user CLI, and no further.

Before this ADR the only concrete coupling in the stateful send→settle→gate loop
(`ProtocolExecutor`) was three direct calls into the Epson byte core:
`protocol::IsWritePacket`, `IsWriteAcknowledged`, `IsWriteRejected`. Success was
therefore defined in Epson's terms (`:42:OK;`), so the executor could not drive a
non-Epson device even though its transport (`ITransport`) was already abstract.

## Decision

Introduce a small vendor seam, `ewr::IResetProtocol` (`include/ewr/vendor.h`), and
depend on it from `ProtocolExecutor` by constructor injection (DIP). Epson becomes
`EpsonD4Protocol`, a thin adapter over the pure `ewr::protocol` core.

```cpp
enum class Ack { Acknowledged, Rejected, None };

class IResetProtocol {
public:
    virtual bool IsWritePacket(const D4Packet& packet) const = 0;  // which sends must be gated
    virtual Ack  ClassifyReply(const D4Packet& reply) const = 0;   // how to read the reply
    virtual ~IResetProtocol() = default;
};
```

The interface is kept to exactly what the executor needs (**ISP**): it does *not*
include sequence building. How a `PayloadSequence` is produced — generated from a
DB model (Smart) or parsed from a capture (Replay) — is a **separate concern**
(the *sequence source*) and stays out of the vendor protocol.

## Extension recipe (OCP)

Adding a packet-driven vendor requires **no change to `ProtocolExecutor`**:

1. Implement `IResetProtocol` for the vendor (its write-frame test + reply rule).
2. Provide its reset data source (see P7) and build a `PayloadSequence`.
3. Select it at the device-discovery boundary (see P6) and inject it into the
   executor — exactly where `main` injects `EpsonD4Protocol` today.

`tests/executor_test.cpp` proves this today: a `StubProtocol` with its own write
marker and ack bytes (no Epson token anywhere) drives the executor to
success/failure, confirming the executor depends only on the injected interface.

## Consequences

- The executor is vendor-neutral; Epson is one adapter (`src/vendor.cpp`).
- Behavior is unchanged for Epson (golden / property / executor tests stay green;
  Mull 97%).
- `ProtocolExecutor` holds a `const IResetProtocol&` — the protocol must outlive
  the executor (it does: both are stack-local in `main` and the tests).

## Non-goals / hard limits (be honest about scope)

These are *why* this ADR does not promise "supports all printers":

1. **Vendors differ in reset *mechanism*, not just bytes.** Canon uses service-mode
   command codes; Brother uses panel maintenance mode. `IResetProtocol` models a
   packet stream with an interpretable reply — it fits packet-driven resets, not
   every mechanism.
2. **Some resets need physical button sequences** that are not USB-automatable at
   all. Out of scope by physics, not by design.
3. **Per-vendor reset data is scarce.** The bundled database (reinkpy-derived) is
   Epson-only; a new vendor needs its own reset data before P7 is worth doing.
4. **Replay keeps Epson semantics for Epson dumps.** The Replay path streams
   captured bytes (vendor-agnostic transport), but the bundled dumps are Epson
   captures, so they are correctly gated with `EpsonD4Protocol`'s `:42:OK;` rule.
   A "neutral, never-gated" replay would *weaken* that safety check, so it was
   deliberately **not** introduced.

## Follow-ups

- **P6** — vendor-neutral device discovery + registry (map a matched USB device to
  its `IResetProtocol`). Deferred until a real second vendor exists; a registry for
  one vendor is speculative (YAGNI), and the discovery code is per-platform
  (`usb_linux.cpp` / `usb_windows.cpp`) so it needs a Windows build to verify.
- **P7** — generalize the reset-DB schema behind a vendor discriminator. Deferred:
  data-dependent (see limit 3).
