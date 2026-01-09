#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "bit_fields/bit_fields.h"
#include "ualink/trace.h"

namespace ualink::dl {

// =============================================================================
// DL message type codes (Table 6-3)
// =============================================================================

enum class DlMessageClass : std::uint8_t {
  kBasic = 0b0000,
  kUart = 0b0001,
  kControl = 0b1000,
};

enum class DlBasicMessageType : std::uint8_t {
  kNoOp = 0b000,
  kTlRateNotification = 0b100,
  kDeviceIdRequest = 0b101,
  kPortNumberRequestResponse = 0b110,
};

enum class DlControlMessageType : std::uint8_t {
  kChannelOnlineOfflineNegotiation = 0b100,
};

enum class DlUartMessageType : std::uint8_t {
  kStreamTransportMessage = 0b000,
  kStreamCreditUpdate = 0b001,
  kStreamResetRequest = 0b110,
  kStreamResetResponse = 0b111,
};

// =============================================================================
// DL message formats (Chapter 6 tables)
// Fields are specified MSB -> LSB.
// =============================================================================

// Table 6-10 UART Stream Reset Request
inline constexpr bit_fields::PacketFormat<7> kUartStreamResetRequestFormat{{{
    {"_reserved_hi", 19}, // 31:13
    {"all_streams", 1},   // 12
    {"stream_id", 3},     // 11:9
    {"mtype", 3},         // 8:6
    {"mclass", 4},        // 5:2
    {"_reserved", 1},     // 1
    {"compressed", 1},    // 0
}}};

// Table 6-11 UART Stream Reset Response
inline constexpr bit_fields::PacketFormat<8> kUartStreamResetResponseFormat{{{
    {"_reserved_hi", 16}, // 31:16
    {"status", 3},        // 15:13
    {"all_streams", 1},   // 12
    {"stream_id", 3},     // 11:9
    {"mtype", 3},         // 8:6
    {"mclass", 4},        // 5:2
    {"_reserved", 1},     // 1
    {"compressed", 1},    // 0
}}};

// Table 6-12 UART Stream transport message (DW0 header; payload dwords follow)
inline constexpr bit_fields::PacketFormat<7> kUartStreamTransportHeaderFormat{{{
    {"length", 5},        // 31:27 (payload_dwords - 1)
    {"_reserved_hi", 15}, // 26:12
    {"stream_id", 3},     // 11:9
    {"mtype", 3},         // 8:6
    {"mclass", 4},        // 5:2
    {"_reserved", 1},     // 1
    {"compressed", 1},    // 0
}}};

// Table 6-13 UART Stream Credit Update
inline constexpr bit_fields::PacketFormat<7> kUartStreamCreditUpdateFormat{{{
    {"data_fc_seq", 12}, // 31:20
    {"_reserved_hi", 8}, // 19:12
    {"stream_id", 3},    // 11:9
    {"mtype", 3},        // 8:6
    {"mclass", 4},       // 5:2
    {"_reserved", 1},    // 1
    {"compressed", 1},   // 0
}}};

// Table 6-4 TL Rate Notification
inline constexpr bit_fields::PacketFormat<8> kTlRateNotificationFormat{{{
    {"rate", 16},      // 31:16
    {"_reserved0", 3}, // 15:13
    {"ack", 1},        // 12
    {"_reserved1", 3}, // 11:9
    {"mtype", 3},      // 8:6
    {"mclass", 4},     // 5:2
    {"_reserved2", 1}, // 1
    {"compressed", 1}, // 0
}}};

// Table 6-5 Device ID Request/Response
inline constexpr bit_fields::PacketFormat<11> kDeviceIdFormat{{{
    {"valid", 1},      // 31
    {"type", 2},       // 30:29
    {"_reserved0", 3}, // 28:26
    {"id", 10},        // 25:16
    {"_reserved1", 3}, // 15:13
    {"ack", 1},        // 12
    {"_reserved2", 3}, // 11:9
    {"mtype", 3},      // 8:6
    {"mclass", 4},     // 5:2
    {"_reserved3", 1}, // 1
    {"compressed", 1}, // 0
}}};

// Table 6-6 Port ID Request/Response
inline constexpr bit_fields::PacketFormat<10> kPortIdFormat{{{
    {"valid", 1},        // 31
    {"_reserved0", 3},   // 30:28
    {"port_number", 12}, // 27:16
    {"_reserved1", 3},   // 15:13
    {"ack", 1},          // 12
    {"_reserved2", 3},   // 11:9
    {"mtype", 3},        // 8:6
    {"mclass", 4},       // 5:2
    {"_reserved3", 1},   // 1
    {"compressed", 1},   // 0
}}};

// Table 6-7 No-Op Message
inline constexpr bit_fields::PacketFormat<5> kNoOpMessageFormat{{{
    {"_reserved_hi", 23}, // 31:9
    {"mtype", 3},         // 8:6
    {"mclass", 4},        // 5:2
    {"_reserved", 1},     // 1
    {"compressed", 1},    // 0
}}};

// Table 6-8 Channel Negotiation
inline constexpr bit_fields::PacketFormat<9> kChannelNegotiationFormat{{{
    {"_reserved0", 4},       // 31:28
    {"channel_response", 4}, // 27:24
    {"channel_command", 4},  // 23:20
    {"channel_target", 4},   // 19:16
    {"_reserved1", 7},       // 15:9
    {"mtype", 3},            // 8:6
    {"mclass", 4},           // 5:2
    {"_reserved2", 1},       // 1
    {"compressed", 1},       // 0
}}};

// Table 6-9 Vendor Defined Packet Type Length (TL) DWord
inline constexpr bit_fields::PacketFormat<3> kVendorDefinedPacketTypeLengthFormat{{{
    {"vendor_id", 16}, // 31:16
    {"type", 8},       // 15:8
    {"length", 8},     // 7:0
}}};

struct DlMsgCommon {
  std::uint8_t mtype{0};
  std::uint8_t mclass{0};
};

[[nodiscard]] constexpr DlMsgCommon make_common(DlMessageClass mclass, std::uint8_t mtype) {
  return DlMsgCommon{.mtype = mtype, .mclass = static_cast<std::uint8_t>(mclass)};
}

[[nodiscard]] constexpr DlMsgCommon make_common(DlBasicMessageType mtype) {
  return make_common(DlMessageClass::kBasic, static_cast<std::uint8_t>(mtype));
}

[[nodiscard]] constexpr DlMsgCommon make_common(DlControlMessageType mtype) {
  return make_common(DlMessageClass::kControl, static_cast<std::uint8_t>(mtype));
}

[[nodiscard]] constexpr DlMsgCommon make_common(DlUartMessageType mtype) {
  return make_common(DlMessageClass::kUart, static_cast<std::uint8_t>(mtype));
}

struct UartStreamResetRequest {
  bool all_streams{false};
  std::uint8_t stream_id{0};
  DlMsgCommon common{};
};

struct UartStreamResetResponse {
  std::uint8_t status{0};
  bool all_streams{false};
  std::uint8_t stream_id{0};
  DlMsgCommon common{};
};

struct UartStreamTransportMessage {
  std::uint8_t stream_id{0};
  DlMsgCommon common{};
  std::vector<std::uint32_t> payload_dwords{}; // 1..32
};

struct UartStreamCreditUpdate {
  std::uint16_t data_fc_seq{0}; // 12 bits
  std::uint8_t stream_id{0};
  DlMsgCommon common{};
};

struct TlRateNotification {
  std::uint16_t rate{0};
  bool ack{false};
  DlMsgCommon common{};
};

struct DeviceIdMessage {
  bool valid{false};
  std::uint8_t type{0}; // 2 bits
  std::uint16_t id{0};  // 10 bits
  bool ack{false};
  DlMsgCommon common{};
};

struct PortIdMessage {
  bool valid{false};
  std::uint16_t port_number{0}; // encoded as 12 bits (27:16)
  bool ack{false};
  DlMsgCommon common{};
};

struct NoOpMessage {
  DlMsgCommon common{};
};

struct ChannelNegotiation {
  std::uint8_t channel_response{0};
  std::uint8_t channel_command{0};
  std::uint8_t channel_target{0};
  DlMsgCommon common{};
};

struct VendorDefinedPacketTypeLength {
  std::uint16_t vendor_id{0};
  std::uint8_t type{0};
  std::uint8_t length{0};
};

[[nodiscard]] std::array<std::byte, 4> serialize_uart_stream_reset_request(const UartStreamResetRequest &msg);
[[nodiscard]] std::optional<UartStreamResetRequest> deserialize_uart_stream_reset_request(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_uart_stream_reset_response(const UartStreamResetResponse &msg);
[[nodiscard]] std::optional<UartStreamResetResponse> deserialize_uart_stream_reset_response(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::vector<std::byte> serialize_uart_stream_transport_message(const UartStreamTransportMessage &msg);
[[nodiscard]] std::optional<UartStreamTransportMessage> deserialize_uart_stream_transport_message(std::span<const std::byte> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_uart_stream_credit_update(const UartStreamCreditUpdate &msg);
[[nodiscard]] std::optional<UartStreamCreditUpdate> deserialize_uart_stream_credit_update(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_tl_rate_notification(const TlRateNotification &msg);
[[nodiscard]] std::optional<TlRateNotification> deserialize_tl_rate_notification(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_device_id_message(const DeviceIdMessage &msg);
[[nodiscard]] std::optional<DeviceIdMessage> deserialize_device_id_message(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_port_id_message(const PortIdMessage &msg);
[[nodiscard]] std::optional<PortIdMessage> deserialize_port_id_message(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_no_op_message(const NoOpMessage &msg);
[[nodiscard]] std::optional<NoOpMessage> deserialize_no_op_message(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_channel_negotiation(const ChannelNegotiation &msg);
[[nodiscard]] std::optional<ChannelNegotiation> deserialize_channel_negotiation(std::span<const std::byte, 4> bytes);

[[nodiscard]] std::array<std::byte, 4> serialize_vendor_defined_packet_type_length(const VendorDefinedPacketTypeLength &msg);
[[nodiscard]] VendorDefinedPacketTypeLength deserialize_vendor_defined_packet_type_length(std::span<const std::byte, 4> bytes);

} // namespace ualink::dl
