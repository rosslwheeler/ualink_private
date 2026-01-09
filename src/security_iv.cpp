#include "ualink/security_iv.h"

#include <stdexcept>

namespace ualink::security {

std::array<std::byte, 12> serialize_iv96(const Iv96 &iv) {
  UALINK_TRACE_SCOPED(__func__);
  std::array<std::byte, 12> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kSecurityIvFormat, 0ULL, iv.invocation);
  return out;
}

std::optional<Iv96> deserialize_iv96(std::span<const std::byte, 12> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  std::uint64_t fixed = 0;
  Iv96 iv{};
  r.deserialize_into(kSecurityIvFormat, fixed, iv.invocation);

  if (fixed != 0) {
    return std::nullopt;
  }
  return iv;
}

} // namespace ualink::security
