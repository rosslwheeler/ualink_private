#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "bit_fields/bit_fields.h"
#include "ualink/trace.h"

namespace ualink::security {

// =============================================================================
// Security IV format (Table 9-3)
// 96-bit IV = fixed[95:32] (64 bits, must be 0) + invocation[31:0]
// Fields are specified MSB -> LSB.
// =============================================================================

inline constexpr bit_fields::PacketFormat<2> kSecurityIvFormat{{{
    {"fixed", 64},      // [95:32]
    {"invocation", 32}, // [31:0]
}}};

struct Iv96 {
  std::uint32_t invocation{0};
};

[[nodiscard]] std::array<std::byte, 12> serialize_iv96(const Iv96 &iv);
[[nodiscard]] std::optional<Iv96> deserialize_iv96(std::span<const std::byte, 12> bytes);

} // namespace ualink::security
