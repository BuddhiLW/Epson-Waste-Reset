#pragma once
#include <array>
#include <cstdint>

namespace ewr::proto {

    constexpr uint8_t  D4_PSID = 0x02;        // EPSON-CTRL channel
    constexpr uint8_t  D4_SSID = 0x02;
    constexpr uint8_t  D4_CREDIT_NONE = 0x00; // MUST be 0
    constexpr uint8_t  D4_CONTROL = 0x00;
    constexpr uint8_t  FRAME = 0x7C;          // '|'
    constexpr uint8_t  WRITE_CMD = 0x42;      // '|B'
    constexpr uint16_t D4_HEADER_LEN = 6;

    inline constexpr std::array<unsigned char, 27> EJL_PACKET_MODE_INIT{
        0x00, 0x00, 0x00, 0x1B, 0x01, '@', 'E', 'J', 'L', ' ', '1', '2', '8', '4', '.', '4', '\n',
        '@', 'E', 'J', 'L', '\n', '@', 'E', 'J', 'L', '\n'};

    inline constexpr std::array<unsigned char, 8> D4_INIT{
        0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x10};

    inline constexpr std::array<unsigned char, 17> D4_OPEN_CHANNEL{
        0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};

    inline constexpr std::array<unsigned char, 11> D4_CREDIT_GRANT{
        0x00, 0x00, 0x00, 0x0B, 0x01, 0x00, 0x03, 0x02, 0x02, 0x00, 0x01};

    inline constexpr std::array<unsigned char, 13> D4_CREDIT_REQUEST{
        0x00, 0x00, 0x00, 0x0D, 0x01, 0x00, 0x04, 0x02, 0x02, 0xFF, 0xFF, 0x00, 0x01};

} // namespace ewr::proto
