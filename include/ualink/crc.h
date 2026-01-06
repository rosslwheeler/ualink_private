#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ualink::dl {

// CRC-32 polynomial (IEEE 802.3) - 0x04C11DB7
// This is the standard Ethernet CRC polynomial, widely used in networking protocols
constexpr std::uint32_t kCrc32Polynomial = 0x04C11DB7;

// Compute CRC-32 over the given data
// Returns the 32-bit CRC value in network byte order (big-endian)
[[nodiscard]] std::array<std::byte, 4> compute_crc32(std::span<const std::byte> data);

// Verify CRC-32 for the given data
// Returns true if the CRC matches the expected value
[[nodiscard]] bool verify_crc32(std::span<const std::byte> data,
                                 std::span<const std::byte, 4> expected_crc);

}  // namespace ualink::dl