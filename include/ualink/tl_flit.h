#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "bit_fields/bit_fields.h"
#include "ualink/trace.h"

namespace ualink::tl {

// TL flit size constants
constexpr std::size_t kTlFlitBytes = 64;
constexpr std::size_t kTlHalfFlitBytes = 32;

// TL Operation codes
enum class TlOpcode : std::uint8_t {
  kReadRequest = 0,
  kReadResponse = 1,
  kWriteRequest = 2,
  kWriteCompletion = 3,
  kMessage = 4,
  kAtomicRequest = 5,
  kAtomicResponse = 6,
  kReserved = 7,
};

// TL Message field values (2 bits in segment header)
enum class TlMessageType : std::uint8_t {
  kNone = 0,
  kStart = 1,      // Start of multi-flit message
  kContinue = 2,   // Continuation of multi-flit message
  kEnd = 3,        // End of multi-flit message
};

// TL Request header format (common fields for all request types)
// Fits in first 8 bytes of TL flit
inline constexpr bit_fields::PacketFormat<6> kTlRequestHeaderFormat{{{
    {"opcode", 3},
    {"half_flit", 1},      // 1 = half-flit (32B), 0 = full-flit (64B)
    {"size", 6},           // Transfer size encoding
    {"tag", 12},           // Transaction tag for matching requests/responses
    {"address_hi", 16},    // Upper 16 bits of address
    {"address_lo", 26},    // Lower 26 bits of address (42 bits total)
}}};

// TL Response header format
inline constexpr bit_fields::PacketFormat<6> kTlResponseHeaderFormat{{{
    {"opcode", 3},
    {"half_flit", 1},
    {"status", 4},         // Response status code
    {"tag", 12},           // Matching tag from request
    {"data_valid", 1},     // Whether response contains data
    {"_reserved", 11},
}}};

// TL header structures
struct TlRequestHeader {
  TlOpcode opcode{TlOpcode::kReadRequest};
  bool half_flit{false};
  std::uint8_t size{0};
  std::uint16_t tag{0};
  std::uint64_t address{0};  // 42-bit address, stored in 64-bit field
};

struct TlResponseHeader {
  TlOpcode opcode{TlOpcode::kReadResponse};
  bool half_flit{false};
  std::uint8_t status{0};
  std::uint16_t tag{0};
  bool data_valid{false};
};

// TL transaction types
struct TlReadRequest {
  TlRequestHeader header{};
};

struct TlReadResponse {
  TlResponseHeader header{};
  std::array<std::byte, kTlFlitBytes - 4> data{};  // 60 bytes of data (response header is 4 bytes)
};

struct TlWriteRequest {
  TlRequestHeader header{};
  std::array<std::byte, kTlFlitBytes - 8> data{};  // 56 bytes of data
};

struct TlWriteCompletion {
  TlResponseHeader header{};
};

// TL flit serialization/deserialization
class TlSerializer {
public:
  // Serialize read request into TL flit
  [[nodiscard]] static std::array<std::byte, kTlFlitBytes> serialize_read_request(
      const TlReadRequest& request,
      TlMessageType message_type = TlMessageType::kNone);

  // Serialize read response into TL flit
  [[nodiscard]] static std::array<std::byte, kTlFlitBytes> serialize_read_response(
      const TlReadResponse& response,
      TlMessageType message_type = TlMessageType::kNone);

  // Serialize write request into TL flit
  [[nodiscard]] static std::array<std::byte, kTlFlitBytes> serialize_write_request(
      const TlWriteRequest& request,
      TlMessageType message_type = TlMessageType::kNone);

  // Serialize write completion into TL flit
  [[nodiscard]] static std::array<std::byte, kTlFlitBytes> serialize_write_completion(
      const TlWriteCompletion& completion,
      TlMessageType message_type = TlMessageType::kNone);
};

class TlDeserializer {
public:
  // Decode TL opcode from flit
  [[nodiscard]] static TlOpcode decode_opcode(std::span<const std::byte, kTlFlitBytes> flit);

  // Deserialize read request from TL flit
  [[nodiscard]] static std::optional<TlReadRequest> deserialize_read_request(
      std::span<const std::byte, kTlFlitBytes> flit);

  // Deserialize read response from TL flit
  [[nodiscard]] static std::optional<TlReadResponse> deserialize_read_response(
      std::span<const std::byte, kTlFlitBytes> flit);

  // Deserialize write request from TL flit
  [[nodiscard]] static std::optional<TlWriteRequest> deserialize_write_request(
      std::span<const std::byte, kTlFlitBytes> flit);

  // Deserialize write completion from TL flit
  [[nodiscard]] static std::optional<TlWriteCompletion> deserialize_write_completion(
      std::span<const std::byte, kTlFlitBytes> flit);
};

// Encode TL request header
[[nodiscard]] std::array<std::byte, 8> encode_tl_request_header(const TlRequestHeader& header);

// Decode TL request header
[[nodiscard]] TlRequestHeader decode_tl_request_header(std::span<const std::byte, 8> bytes);

// Encode TL response header
[[nodiscard]] std::array<std::byte, 4> encode_tl_response_header(const TlResponseHeader& header);

// Decode TL response header
[[nodiscard]] TlResponseHeader decode_tl_response_header(std::span<const std::byte, 4> bytes);

// Convert TlMessageType to message field value
[[nodiscard]] constexpr std::uint8_t tl_message_type_to_field(TlMessageType type) noexcept {
  return static_cast<std::uint8_t>(type);
}

// Convert message field value to TlMessageType
[[nodiscard]] constexpr TlMessageType tl_message_field_to_type(std::uint8_t field) noexcept {
  if (field > 3) {
    return TlMessageType::kNone;
  }
  return static_cast<TlMessageType>(field);
}

}  // namespace ualink::tl
