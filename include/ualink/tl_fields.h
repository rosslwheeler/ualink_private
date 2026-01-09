#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "bit_fields/bit_fields.h"
#include "ualink/trace.h"

namespace ualink::tl {

// =============================================================================
// TL Field Formats (Chapter 5 tables)
// Fields are specified MSB -> LSB.
// =============================================================================

enum class TlFieldType : std::uint8_t {
  kFlowControlNop = 0x0,
  kUncompressedRequest = 0x1,
  kUncompressedResponse = 0x2,
  kCompressedRequest = 0x3,
  kCompressedResponseSingleBeatRead = 0x4,
  kCompressedResponseWriteOrMultiBeatRead = 0x5,
};

// Table 5-29 Uncompressed Request Field Signals (128 bits)
inline constexpr bit_fields::PacketFormat<15> kUncompressedRequestFieldFormat{{{
    {"ftype", 4},     // [127:124]
    {"cmd", 6},       // [123:118]
    {"vchan", 2},     // [117:116]
    {"asi", 2},       // [115:114]
    {"tag", 11},      // [113:103]
    {"pool", 1},      // [102]
    {"attr", 8},      // [101:94]
    {"len", 6},       // [93:88]
    {"metadata", 8},  // [87:80]
    {"addr", 55},     // [79:25]
    {"srcaccid", 10}, // [24:15]
    {"dstaccid", 10}, // [14:5]
    {"cload", 1},     // [4]
    {"cway", 2},      // [3:2]
    {"numbeats", 2},  // [1:0]
}}};

// Table 5-30 Uncompressed Response Field Signals (64 bits)
inline constexpr bit_fields::PacketFormat<12> kUncompressedResponseFieldFormat{{{
    {"ftype", 4},     // [63:60]
    {"vchan", 2},     // [59:58]
    {"tag", 11},      // [57:47]
    {"pool", 1},      // [46]
    {"len", 2},       // [45:44]
    {"offset", 2},    // [43:42]
    {"status", 4},    // [41:38]
    {"rd_wr", 1},     // [37]
    {"last", 1},      // [36]
    {"srcaccid", 10}, // [35:26]
    {"dstaccid", 10}, // [25:16]
    {"spares", 16},   // [15:0]
}}};

// Table 5-31 Compressed Request Field Signals (64 bits)
inline constexpr bit_fields::PacketFormat<12> kCompressedRequestFieldFormat{{{
    {"ftype", 4},     // [63:60]
    {"cmd", 3},       // [59:57]
    {"vchan", 2},     // [56:55]
    {"asi", 2},       // [54:53]
    {"tag", 11},      // [52:42]
    {"pool", 1},      // [41]
    {"len", 2},       // [40:39]
    {"metadata", 3},  // [38:36]
    {"addr", 14},     // [35:22]
    {"srcaccid", 10}, // [21:12]
    {"dstaccid", 10}, // [11:2]
    {"cway", 2},      // [1:0]
}}};

// Table 5-34 Compressed Response for Single Beat Read Field Signals (32 bits)
inline constexpr bit_fields::PacketFormat<8> kCompressedSingleBeatReadResponseFieldFormat{{{
    {"ftype", 4},     // [31:28]
    {"vchan", 2},     // [27:26]
    {"tag", 11},      // [25:15]
    {"pool", 1},      // [14]
    {"dstaccid", 10}, // [13:4]
    {"offset", 2},    // [3:2]
    {"last", 1},      // [1]
    {"spare", 1},     // [0]
}}};

// Table 5-36 Compressed Response for Write or Multi-Beat Read Field Signals (32 bits)
inline constexpr bit_fields::PacketFormat<8> kCompressedWriteOrMultiBeatReadResponseFieldFormat{{{
    {"ftype", 4},     // [31:28]
    {"vchan", 2},     // [27:26]
    {"tag", 11},      // [25:15]
    {"pool", 1},      // [14]
    {"dstaccid", 10}, // [13:4]
    {"len", 2},       // [3:2]
    {"rd_wr", 1},     // [1]
    {"spare", 1},     // [0]
}}};

// Table 5-38 Flow Control/NOP Field (32 bits)
inline constexpr bit_fields::PacketFormat<5> kFlowControlNopFieldFormat{{{
    {"ftype", 4},    // [31:28]
    {"req_cmd", 6},  // [27:22]
    {"rsp_cmd", 6},  // [21:16]
    {"req_data", 8}, // [15:8]
    {"rsp_data", 8}, // [7:0]
}}};

struct UncompressedRequestField {
  std::uint8_t cmd{0};
  std::uint8_t vchan{0};
  std::uint8_t asi{0};
  std::uint16_t tag{0};
  bool pool{false};
  std::uint8_t attr{0};
  std::uint8_t len{0};
  std::uint8_t metadata{0};
  std::uint64_t addr{0}; // 55 bits
  std::uint16_t srcaccid{0};
  std::uint16_t dstaccid{0};
  bool cload{false};
  std::uint8_t cway{0};
  std::uint8_t numbeats{0};
};

struct UncompressedResponseField {
  std::uint8_t vchan{0};
  std::uint16_t tag{0};
  bool pool{false};
  std::uint8_t len{0};
  std::uint8_t offset{0};
  std::uint8_t status{0};
  bool rd_wr{false};
  bool last{false};
  std::uint16_t srcaccid{0};
  std::uint16_t dstaccid{0};
  std::uint16_t spares{0};
};

struct CompressedRequestField {
  std::uint8_t cmd{0};
  std::uint8_t vchan{0};
  std::uint8_t asi{0};
  std::uint16_t tag{0};
  bool pool{false};
  std::uint8_t len{0};
  std::uint8_t metadata{0};
  std::uint16_t addr{0};
  std::uint16_t srcaccid{0};
  std::uint16_t dstaccid{0};
  std::uint8_t cway{0};
};

struct CompressedSingleBeatReadResponseField {
  std::uint8_t vchan{0};
  std::uint16_t tag{0};
  bool pool{false};
  std::uint16_t dstaccid{0};
  std::uint8_t offset{0};
  bool last{false};
};

struct CompressedWriteOrMultiBeatReadResponseField {
  std::uint8_t vchan{0};
  std::uint16_t tag{0};
  bool pool{false};
  std::uint16_t dstaccid{0};
  std::uint8_t len{0};
  bool rd_wr{false};
};

struct FlowControlNopField {
  std::uint8_t req_cmd{0};
  std::uint8_t rsp_cmd{0};
  std::uint8_t req_data{0};
  std::uint8_t rsp_data{0};
};

[[nodiscard]] std::array<std::byte, 16> serialize_uncompressed_request_field(const UncompressedRequestField &f);
[[nodiscard]] std::optional<UncompressedRequestField> deserialize_uncompressed_request_field(std::span<const std::byte, 16> bytes);

[[nodiscard]] std::array<std::byte, 8> serialize_uncompressed_response_field(const UncompressedResponseField &f);
[[nodiscard]] std::optional<UncompressedResponseField> deserialize_uncompressed_response_field(std::span<const std::byte, 8> bytes);

[[nodiscard]] std::array<std::byte, 8> serialize_compressed_request_field(const CompressedRequestField &f);
[[nodiscard]] std::optional<CompressedRequestField> deserialize_compressed_request_field(std::span<const std::byte, 8> bytes);

[[nodiscard]] std::array<std::byte, 4>
serialize_compressed_single_beat_read_response_field(const CompressedSingleBeatReadResponseField &f);
[[nodiscard]] std::optional<CompressedSingleBeatReadResponseField>
deserialize_compressed_single_beat_read_response_field(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4>
serialize_compressed_write_or_multibeat_read_response_field(const CompressedWriteOrMultiBeatReadResponseField &f);
[[nodiscard]] std::optional<CompressedWriteOrMultiBeatReadResponseField>
deserialize_compressed_write_or_multibeat_read_response_field(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_flow_control_nop_field(const FlowControlNopField &f);
[[nodiscard]] std::optional<FlowControlNopField> deserialize_flow_control_nop_field(std::span<const std::byte, 4> bytes);

} // namespace ualink::tl
