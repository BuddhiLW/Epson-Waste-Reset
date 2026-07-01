#pragma once
#include <cstdint>
#include <vector>

// Domain value objects for the EWR protocol core.
//
// Strong types are used ONLY where a bare scalar is a real transposition hazard:
// BuildWritePacket used to take (uint16_t rkey, uint16_t address, uint8_t value, ...)
// — four adjacent scalars the compiler would happily let you swap, in the one
// function that writes to printer EEPROM. Wrapping rkey/address makes a swap a
// compile error. The reset value stays a plain uint8_t (a raw EEPROM byte with no
// invariant), and the write key stays a string.

namespace ewr {

    enum class ResetKey      : uint16_t {};
    enum class EepromAddress : uint16_t {};

    struct EepromWrite
    {
        EepromAddress addr;
        uint8_t       value;
    };

    using D4Packet        = std::vector<unsigned char>; // one framed, on-wire packet
    using PayloadSequence = std::vector<D4Packet>;      // the ubiquitous concept

} // namespace ewr
