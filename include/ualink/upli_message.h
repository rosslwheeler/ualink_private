#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "bit_fields/bit_fields.h"
#include "ualink/trace.h"
#include "ualink/upli_channel.h"

namespace ualink::upli {

// UPLI message opcodes (matches TL opcodes for passthrough)
enum class UpliOpcode : std::uint8_t {
  kReadRequest = 0,
  kReadResponse = 1,
  kWriteRequest = 2,
  kWriteCompletion = 3,
  kMessage = 4,
  kAtomicRequest = 5,
  kAtomicResponse = 6,
  kReserved = 7,
};

// UPLI message priority levels
enum class UpliPriority : std::uint8_t {
  kLow = 0,
  kMedium = 1,
  kHigh = 2,
  kCritical = 3,
};

// UPLI message header format (first 8 bytes)
// Maps to TL request header but adds UPLI-specific fields
// Total: 3+2+2+3+12+16+26 = 64 bits
inline constexpr bit_fields::PacketFormat<7> kUpliMessageHeaderFormat{{{
    {"opcode", 3},
    {"priority", 2},     // UPLI priority level
    {"vc", 2},           // Virtual channel (0-3)
    {"size", 3},         // Transfer size encoding (0-7)
    {"tag", 12},         // Transaction tag
    {"address_hi", 16},  // Upper 16 bits of address
    {"address_lo", 26},  // Lower 26 bits of address (42 bits total)
}}};

// UPLI response header format (first 4 bytes)
inline constexpr bit_fields::PacketFormat<7> kUpliResponseHeaderFormat{{{
    {"opcode", 3},
    {"priority", 2},
    {"vc", 2},
    {"status", 4},      // Response status code
    {"tag", 12},        // Matching tag from request
    {"data_valid", 1},  // Whether response contains data
    {"_reserved", 8},
}}};

// UPLI message header structure
struct UpliMessageHeader {
  UpliOpcode opcode{UpliOpcode::kReadRequest};
  UpliPriority priority{UpliPriority::kMedium};
  std::uint8_t vc{0};      // Virtual channel (0-3)
  std::uint8_t size{0};    // Transfer size encoding
  std::uint16_t tag{0};    // Transaction tag (12 bits)
  std::uint64_t address{0};  // 42-bit address
};

// UPLI response header structure
struct UpliResponseHeader {
  UpliOpcode opcode{UpliOpcode::kReadResponse};
  UpliPriority priority{UpliPriority::kMedium};
  std::uint8_t vc{0};
  std::uint8_t status{0};
  std::uint16_t tag{0};
  bool data_valid{false};
};

// UPLI message types
constexpr std::size_t kUpliMaxPayloadBytes = 56;  // 64 - 8 byte header

struct UpliReadRequest {
  UpliMessageHeader header{};
};

struct UpliReadResponse {
  UpliResponseHeader header{};
  std::array<std::byte, kUpliMaxPayloadBytes + 4> data{};  // 60 bytes (4 byte header)
};

struct UpliWriteRequest {
  UpliMessageHeader header{};
  std::array<std::byte, kUpliMaxPayloadBytes> data{};  // 56 bytes
};

struct UpliWriteCompletion {
  UpliResponseHeader header{};
};

// UPLI message serializer
class UpliMessageSerializer {
 public:
  // Serialize UPLI message header
  [[nodiscard]] static std::array<std::byte, 8> serialize_message_header(
      const UpliMessageHeader& header);

  // Serialize UPLI response header
  [[nodiscard]] static std::array<std::byte, 4> serialize_response_header(
      const UpliResponseHeader& header);

  // Serialize complete messages into channel format
  [[nodiscard]] static UpliChannelFlit serialize_read_request(
      const UpliReadRequest& request);

  [[nodiscard]] static UpliChannelFlit serialize_read_response(
      const UpliReadResponse& response);

  [[nodiscard]] static UpliChannelFlit serialize_write_request(
      const UpliWriteRequest& request);

  [[nodiscard]] static UpliChannelFlit serialize_write_completion(
      const UpliWriteCompletion& completion);
};

// UPLI message deserializer
class UpliMessageDeserializer {
 public:
  // Deserialize UPLI message header
  [[nodiscard]] static UpliMessageHeader deserialize_message_header(
      std::span<const std::byte, 8> bytes);

  // Deserialize UPLI response header
  [[nodiscard]] static UpliResponseHeader deserialize_response_header(
      std::span<const std::byte, 4> bytes);

  // Deserialize opcode from flit
  [[nodiscard]] static UpliOpcode deserialize_opcode(
      const UpliChannelFlit& flit);

  // Deserialize complete messages from channel format
  [[nodiscard]] static std::optional<UpliReadRequest> deserialize_read_request(
      const UpliChannelFlit& flit);

  [[nodiscard]] static std::optional<UpliReadResponse> deserialize_read_response(
      const UpliChannelFlit& flit);

  [[nodiscard]] static std::optional<UpliWriteRequest> deserialize_write_request(
      const UpliChannelFlit& flit);

  [[nodiscard]] static std::optional<UpliWriteCompletion> deserialize_write_completion(
      const UpliChannelFlit& flit);
};

}  // namespace ualink::upli