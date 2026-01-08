#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "bit_fields/bit_fields.h"
#include "ualink/trace.h"

namespace ualink::upli {

// UPLI constants
constexpr std::size_t kMaxPorts = 4;
constexpr std::size_t kUpliDataBeatBytes = 64;

// =============================================================================
// UPLI Request Channel Format
// =============================================================================

// UPLI Request channel format (control fields only)
// Data payload (if any) travels on OrigData channel
inline constexpr bit_fields::PacketFormat<13> kUpliRequestFormat{{{
    {"req_vld", 1},              // Valid request beat
    {"req_port_id", 2},          // Port ID for TDM routing
    {"req_src_phys_acc_id", 10}, // Source accelerator physical ID
    {"req_dst_phys_acc_id", 10}, // Destination accelerator physical ID
    {"req_tag", 11},             // Transaction tag
    {"req_addr", 57},            // Request address
    {"req_cmd", 6},              // Command type encoding
    {"req_len", 6},              // Number of doublewords - 1
    {"req_num_beats", 2},        // Number of data beats on OrigData
    {"req_attr", 8},             // Extended attributes
    {"req_meta_data", 8},        // Control info or UPLI message type
    {"req_vc", 2},               // Virtual channel identifier
    {"req_auth_tag", 64},        // Authorization tag for security
}}};

// Request channel fields structure
struct UpliRequestFields {
  bool req_vld{false};
  std::uint8_t req_port_id{0};
  std::uint16_t req_src_phys_acc_id{0};
  std::uint16_t req_dst_phys_acc_id{0};
  std::uint16_t req_tag{0};
  std::uint64_t req_addr{0};
  std::uint8_t req_cmd{0};
  std::uint8_t req_len{0};
  std::uint8_t req_num_beats{0};
  std::uint8_t req_attr{0};
  std::uint8_t req_meta_data{0};
  std::uint8_t req_vc{0};
  std::uint64_t req_auth_tag{0};
};

// =============================================================================
// UPLI Originator Data Channel Format
// =============================================================================

// UPLI Originator Data channel control format (per beat)
inline constexpr bit_fields::PacketFormat<4> kUpliOrigDataControlFormat{{{
    {"orig_data_vld", 1},        // Valid data beat
    {"orig_data_port_id", 2},    // Port ID
    {"orig_data_error", 1},      // Data error "poison" bit
    {"_reserved", 4},            // Reserved bits
}}};

struct UpliOrigDataFields {
  bool orig_data_vld{false};
  std::uint8_t orig_data_port_id{0};
  bool orig_data_error{false};
  std::array<std::byte, kUpliDataBeatBytes> data{};  // 64-byte payload
};

// =============================================================================
// UPLI Read Response Channel Format
// =============================================================================

// UPLI Read Response channel format
inline constexpr bit_fields::PacketFormat<8> kUpliRdRspFormat{{{
    {"rd_rsp_vld", 1},           // Valid response beat
    {"rd_rsp_port_id", 2},       // Port ID
    {"rd_rsp_tag", 11},          // Transaction tag (matches ReqTag)
    {"rd_rsp_status", 4},        // Response status code
    {"rd_rsp_attr", 8},          // Response attributes
    {"rd_rsp_data_error", 1},    // Data error bit
    {"rd_rsp_auth_tag", 64},     // Authorization tag
    {"_reserved", 5},            // Reserved bits
}}};

struct UpliRdRspFields {
  bool rd_rsp_vld{false};
  std::uint8_t rd_rsp_port_id{0};
  std::uint16_t rd_rsp_tag{0};
  std::uint8_t rd_rsp_status{0};
  std::uint8_t rd_rsp_attr{0};
  bool rd_rsp_data_error{false};
  std::uint64_t rd_rsp_auth_tag{0};
  std::array<std::byte, kUpliDataBeatBytes> data{};  // 64-byte read data
};

// =============================================================================
// UPLI Write Response Channel Format
// =============================================================================

// UPLI Write Response channel format (no data payload)
inline constexpr bit_fields::PacketFormat<7> kUpliWrRspFormat{{{
    {"wr_rsp_vld", 1},           // Valid write response
    {"wr_rsp_port_id", 2},       // Port ID
    {"wr_rsp_tag", 11},          // Transaction tag (matches ReqTag)
    {"wr_rsp_status", 4},        // Response status code
    {"wr_rsp_attr", 8},          // Response attributes
    {"wr_rsp_auth_tag", 64},     // Authorization tag
    {"_reserved", 6},            // Reserved bits
}}};

struct UpliWrRspFields {
  bool wr_rsp_vld{false};
  std::uint8_t wr_rsp_port_id{0};
  std::uint16_t wr_rsp_tag{0};
  std::uint8_t wr_rsp_status{0};
  std::uint8_t wr_rsp_attr{0};
  std::uint64_t wr_rsp_auth_tag{0};
};

// =============================================================================
// UPLI Credit Return Format
// =============================================================================

// Per-port credit return format (replicated for each of 4 ports)
inline constexpr bit_fields::PacketFormat<4> kUpliCreditPortFormat{{{
    {"credit_vld", 1},           // Credit valid for this port
    {"credit_pool", 1},          // 0=VC-specific, 1=Pool credit
    {"credit_vc", 2},            // Virtual channel (0-3)
    {"credit_num", 2},           // Number of credits returned (0-3 encoding)
}}};

struct UpliCreditPortFields {
  bool credit_vld{false};
  bool credit_pool{false};      // false = VC-specific, true = pool
  std::uint8_t credit_vc{0};
  std::uint8_t credit_num{0};   // 0-3 encoding (actual credits = num + 1)
};

// Full credit return structure for all 4 ports
struct UpliCreditReturn {
  std::array<UpliCreditPortFields, kMaxPorts> ports{};
  std::array<bool, kMaxPorts> credit_init_done{};  // Initialization complete per port
};

// =============================================================================
// Serialize/Deserialize Functions
// =============================================================================

// Request channel
[[nodiscard]] std::vector<std::byte> serialize_upli_request(
    const UpliRequestFields& fields);
[[nodiscard]] UpliRequestFields deserialize_upli_request(
    std::span<const std::byte> bytes);

// Originator Data channel
[[nodiscard]] std::vector<std::byte> serialize_upli_orig_data(
    const UpliOrigDataFields& fields);
[[nodiscard]] UpliOrigDataFields deserialize_upli_orig_data(
    std::span<const std::byte> bytes);

// Read Response channel
[[nodiscard]] std::vector<std::byte> serialize_upli_rd_rsp(
    const UpliRdRspFields& fields);
[[nodiscard]] UpliRdRspFields deserialize_upli_rd_rsp(
    std::span<const std::byte> bytes);

// Write Response channel
[[nodiscard]] std::vector<std::byte> serialize_upli_wr_rsp(
    const UpliWrRspFields& fields);
[[nodiscard]] UpliWrRspFields deserialize_upli_wr_rsp(
    std::span<const std::byte> bytes);

// Credit Return
[[nodiscard]] std::vector<std::byte> serialize_upli_credit_return(
    const UpliCreditReturn& credits);
[[nodiscard]] UpliCreditReturn deserialize_upli_credit_return(
    std::span<const std::byte> bytes);

}  // namespace ualink::upli