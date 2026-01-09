#include "ualink/security_iv.h"
#include "ualink/trace.h"

#include <array>
#include <cassert>
#include <cstdint>

using namespace ualink::security;

int main() {
  UALINK_TRACE_SCOPED(__func__);

  {
    Iv96 iv{};
    iv.invocation = 0x12345678U;

    const auto bytes = serialize_iv96(iv);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kSecurityIvFormat);
    const std::array<bit_fields::ExpectedField, 2> expected{{
        {"fixed", 0ULL},
        {"invocation", iv.invocation},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_iv96(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->invocation == iv.invocation);
  }

  {
    // Non-zero fixed should be rejected.
    Iv96 iv{};
    iv.invocation = 1U;
    auto bytes = serialize_iv96(iv);
    bytes[0] = std::byte{0x01};

    const auto decoded = deserialize_iv96(bytes);
    assert(!decoded.has_value());
  }

  return 0;
}
