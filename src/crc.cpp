#include "ualink/crc.h"

#include <algorithm>

#include "ualink/trace.h"

using namespace ualink::dl;

// Pre-computed CRC-32 lookup table for the IEEE 802.3 polynomial
static constexpr std::array<std::uint32_t, 256> generate_crc32_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t table_index = 0; table_index < 256; ++table_index) {
    std::uint32_t crc = table_index << 24;
    for (std::size_t bit_index = 0; bit_index < 8; ++bit_index) {
      if ((crc & 0x80000000U) != 0) {
        crc = (crc << 1) ^ kCrc32Polynomial;
      } else {
        crc = crc << 1;
      }
    }
    table[table_index] = crc;
  }
  return table;
}

static constexpr std::array<std::uint32_t, 256> kCrc32Table = generate_crc32_table();

std::array<std::byte, 4> ualink::dl::compute_crc32(std::span<const std::byte> data) {
  UALINK_TRACE_SCOPED(__func__);

  std::uint32_t crc = 0xFFFFFFFFU;

  for (const std::byte byte : data) {
    const std::uint8_t table_index = static_cast<std::uint8_t>((crc >> 24) ^ std::to_integer<std::uint8_t>(byte));
    crc = (crc << 8) ^ kCrc32Table[table_index];
  }

  crc = ~crc;

  // Convert to network byte order (big-endian)
  std::array<std::byte, 4> result{};
  result[0] = static_cast<std::byte>((crc >> 24) & 0xFFU);
  result[1] = static_cast<std::byte>((crc >> 16) & 0xFFU);
  result[2] = static_cast<std::byte>((crc >> 8) & 0xFFU);
  result[3] = static_cast<std::byte>(crc & 0xFFU);

  return result;
}

bool ualink::dl::verify_crc32(std::span<const std::byte> data,
                               std::span<const std::byte, 4> expected_crc) {
  UALINK_TRACE_SCOPED(__func__);

  const std::array<std::byte, 4> computed_crc = compute_crc32(data);
  return std::equal(computed_crc.begin(), computed_crc.end(), expected_crc.begin());
}