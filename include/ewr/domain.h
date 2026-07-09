#pragma once
#include <cstdint>
#include <vector>

namespace ewr {

    enum class ResetKey      : uint16_t {};
    enum class EepromAddress : uint16_t {};

    struct EepromWrite
    {
        EepromAddress addr;
        uint8_t       value;
    };

    using D4Packet        = std::vector<unsigned char>;
    using PayloadSequence = std::vector<D4Packet>;

} // namespace ewr
