#include "ualink/dl_messages.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace ualink::dl {

static constexpr bool fits_in_bits(std::uint64_t value, std::uint8_t bits) {
  if (bits >= 64) {
    return true;
  }
  return value <= ((1ULL << bits) - 1ULL);
}

static void validate_common(const DlMsgCommon &c) {
  if (!fits_in_bits(c.mtype, 3) || !fits_in_bits(c.mclass, 4)) {
    throw std::invalid_argument("DL message: mtype/mclass out of range");
  }
}

static void validate_compressed(std::uint8_t compressed) {
  if (compressed != 0) {
    throw std::invalid_argument("DL message: compressed=1 not supported");
  }
}

static void store_be32(std::span<std::byte, 4> out, std::uint32_t value) {
  out[0] = std::byte{static_cast<unsigned char>((value >> 24) & 0xFFU)};
  out[1] = std::byte{static_cast<unsigned char>((value >> 16) & 0xFFU)};
  out[2] = std::byte{static_cast<unsigned char>((value >> 8) & 0xFFU)};
  out[3] = std::byte{static_cast<unsigned char>(value & 0xFFU)};
}

static std::uint32_t load_be32(std::span<const std::byte, 4> in) {
  const auto b0 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(in[0]));
  const auto b1 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(in[1]));
  const auto b2 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(in[2]));
  const auto b3 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(in[3]));
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

std::array<std::byte, 4> serialize_uart_stream_reset_request(const UartStreamResetRequest &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);
  if (!fits_in_bits(msg.stream_id, 3)) {
    throw std::invalid_argument("UART Stream Reset Request: stream_id out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kUartStreamResetRequestFormat, 0U, msg.all_streams ? 1U : 0U, msg.stream_id, msg.common.mtype, msg.common.mclass, 0U,
              0U);
  return out;
}

std::optional<UartStreamResetRequest> deserialize_uart_stream_reset_request(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  UartStreamResetRequest msg{};
  std::uint32_t reserved_hi = 0;
  std::uint8_t all_streams = 0;
  std::uint8_t reserved = 0;
  std::uint8_t compressed = 0;

  r.deserialize_into(kUartStreamResetRequestFormat, reserved_hi, all_streams, msg.stream_id, msg.common.mtype, msg.common.mclass,
                     reserved, compressed);

  try {
    validate_compressed(compressed);
  } catch (...) {
    return std::nullopt;
  }

  msg.all_streams = (all_streams != 0);
  return msg;
}

std::array<std::byte, 4> serialize_uart_stream_reset_response(const UartStreamResetResponse &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);
  if (!fits_in_bits(msg.status, 3)) {
    throw std::invalid_argument("UART Stream Reset Response: status out of range");
  }
  if (!fits_in_bits(msg.stream_id, 3)) {
    throw std::invalid_argument("UART Stream Reset Response: stream_id out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kUartStreamResetResponseFormat, 0U, msg.status, msg.all_streams ? 1U : 0U, msg.stream_id, msg.common.mtype,
              msg.common.mclass, 0U, 0U);
  return out;
}

std::optional<UartStreamResetResponse> deserialize_uart_stream_reset_response(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  UartStreamResetResponse msg{};
  std::uint32_t reserved_hi = 0;
  std::uint8_t all_streams = 0;
  std::uint8_t reserved = 0;
  std::uint8_t compressed = 0;

  r.deserialize_into(kUartStreamResetResponseFormat, reserved_hi, msg.status, all_streams, msg.stream_id, msg.common.mtype,
                     msg.common.mclass, reserved, compressed);

  try {
    validate_compressed(compressed);
  } catch (...) {
    return std::nullopt;
  }

  msg.all_streams = (all_streams != 0);
  return msg;
}

std::vector<std::byte> serialize_uart_stream_transport_message(const UartStreamTransportMessage &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);
  if (!fits_in_bits(msg.stream_id, 3)) {
    throw std::invalid_argument("UART Stream transport: stream_id out of range");
  }
  if (msg.payload_dwords.empty()) {
    throw std::invalid_argument("UART Stream transport: payload must be >= 1 dword");
  }
  if (msg.payload_dwords.size() > 32) {
    throw std::invalid_argument("UART Stream transport: payload too large (max 32 dwords by length field)");
  }

  const std::uint8_t length = static_cast<std::uint8_t>(msg.payload_dwords.size() - 1U);

  std::vector<std::byte> out((1U + msg.payload_dwords.size()) * 4U);
  {
    std::array<std::byte, 4> header{};
    bit_fields::NetworkBitWriter w(header);
    w.serialize(kUartStreamTransportHeaderFormat, length, 0U, msg.stream_id, msg.common.mtype, msg.common.mclass, 0U, 0U);
    std::copy(header.begin(), header.end(), out.begin());
  }

  for (std::size_t i = 0; i < msg.payload_dwords.size(); ++i) {
    std::array<std::byte, 4> dw{};
    store_be32(dw, msg.payload_dwords[i]);
    std::copy(dw.begin(), dw.end(), out.begin() + (1U + i) * 4U);
  }

  return out;
}

std::optional<UartStreamTransportMessage> deserialize_uart_stream_transport_message(std::span<const std::byte> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  if (bytes.size() < 8 || (bytes.size() % 4) != 0) {
    return std::nullopt;
  }

  UartStreamTransportMessage msg{};
  std::uint8_t length = 0;
  std::uint32_t reserved_hi = 0;
  std::uint8_t reserved = 0;
  std::uint8_t compressed = 0;
  {
    std::array<std::byte, 4> header{};
    std::copy_n(bytes.data(), 4, header.begin());
    bit_fields::NetworkBitReader r(header);
    r.deserialize_into(kUartStreamTransportHeaderFormat, length, reserved_hi, msg.stream_id, msg.common.mtype, msg.common.mclass,
                       reserved, compressed);
  }

  if (compressed != 0) {
    return std::nullopt;
  }

  const std::size_t payload_dwords = static_cast<std::size_t>(length) + 1U;
  const std::size_t needed_bytes = (1U + payload_dwords) * 4U;
  if (bytes.size() < needed_bytes) {
    return std::nullopt;
  }

  msg.payload_dwords.resize(payload_dwords);
  for (std::size_t i = 0; i < payload_dwords; ++i) {
    std::array<std::byte, 4> dw{};
    std::copy_n(bytes.data() + (1U + i) * 4U, 4, dw.begin());
    msg.payload_dwords[i] = load_be32(dw);
  }

  return msg;
}

std::array<std::byte, 4> serialize_uart_stream_credit_update(const UartStreamCreditUpdate &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);
  if (!fits_in_bits(msg.data_fc_seq, 12)) {
    throw std::invalid_argument("UART Stream Credit Update: data_fc_seq out of range");
  }
  if (!fits_in_bits(msg.stream_id, 3)) {
    throw std::invalid_argument("UART Stream Credit Update: stream_id out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kUartStreamCreditUpdateFormat, msg.data_fc_seq, 0U, msg.stream_id, msg.common.mtype, msg.common.mclass, 0U, 0U);
  return out;
}

std::optional<UartStreamCreditUpdate> deserialize_uart_stream_credit_update(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  UartStreamCreditUpdate msg{};
  std::uint8_t reserved_hi = 0;
  std::uint8_t reserved = 0;
  std::uint8_t compressed = 0;

  r.deserialize_into(kUartStreamCreditUpdateFormat, msg.data_fc_seq, reserved_hi, msg.stream_id, msg.common.mtype,
                     msg.common.mclass, reserved, compressed);

  if (compressed != 0) {
    return std::nullopt;
  }
  return msg;
}

std::array<std::byte, 4> serialize_tl_rate_notification(const TlRateNotification &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kTlRateNotificationFormat, msg.rate, 0U, msg.ack ? 1U : 0U, 0U, msg.common.mtype, msg.common.mclass, 0U, 0U);
  return out;
}

std::optional<TlRateNotification> deserialize_tl_rate_notification(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  TlRateNotification msg{};
  std::uint8_t reserved0 = 0;
  std::uint8_t ack = 0;
  std::uint8_t reserved1 = 0;
  std::uint8_t reserved2 = 0;
  std::uint8_t compressed = 0;

  r.deserialize_into(kTlRateNotificationFormat, msg.rate, reserved0, ack, reserved1, msg.common.mtype, msg.common.mclass, reserved2,
                     compressed);

  if (compressed != 0) {
    return std::nullopt;
  }
  msg.ack = (ack != 0);
  return msg;
}

std::array<std::byte, 4> serialize_device_id_message(const DeviceIdMessage &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);
  if (!fits_in_bits(msg.type, 2)) {
    throw std::invalid_argument("Device ID: type out of range");
  }
  if (!fits_in_bits(msg.id, 10)) {
    throw std::invalid_argument("Device ID: id out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kDeviceIdFormat, msg.valid ? 1U : 0U, msg.type, 0U, msg.id, 0U, msg.ack ? 1U : 0U, 0U, msg.common.mtype,
              msg.common.mclass, 0U, 0U);
  return out;
}

std::optional<DeviceIdMessage> deserialize_device_id_message(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  DeviceIdMessage msg{};
  std::uint8_t valid = 0;
  std::uint8_t ack = 0;
  std::uint8_t compressed = 0;
  std::uint8_t reserved0 = 0;
  std::uint8_t reserved1 = 0;
  std::uint8_t reserved2 = 0;
  std::uint8_t reserved3 = 0;

  r.deserialize_into(kDeviceIdFormat, valid, msg.type, reserved0, msg.id, reserved1, ack, reserved2, msg.common.mtype,
                     msg.common.mclass, reserved3, compressed);

  if (compressed != 0) {
    return std::nullopt;
  }
  msg.valid = (valid != 0);
  msg.ack = (ack != 0);
  return msg;
}

std::array<std::byte, 4> serialize_port_id_message(const PortIdMessage &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);
  if (!fits_in_bits(msg.port_number, 12)) {
    throw std::invalid_argument("Port ID: port_number out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kPortIdFormat, msg.valid ? 1U : 0U, 0U, msg.port_number, 0U, msg.ack ? 1U : 0U, 0U, msg.common.mtype,
              msg.common.mclass, 0U, 0U);
  return out;
}

std::optional<PortIdMessage> deserialize_port_id_message(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  PortIdMessage msg{};
  std::uint8_t valid = 0;
  std::uint8_t ack = 0;
  std::uint8_t compressed = 0;
  std::uint8_t reserved0 = 0;
  std::uint8_t reserved1 = 0;
  std::uint8_t reserved2 = 0;
  std::uint8_t reserved3 = 0;

  r.deserialize_into(kPortIdFormat, valid, reserved0, msg.port_number, reserved1, ack, reserved2, msg.common.mtype,
                     msg.common.mclass, reserved3, compressed);

  if (compressed != 0) {
    return std::nullopt;
  }
  msg.valid = (valid != 0);
  msg.ack = (ack != 0);
  return msg;
}

std::array<std::byte, 4> serialize_no_op_message(const NoOpMessage &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kNoOpMessageFormat, 0U, msg.common.mtype, msg.common.mclass, 0U, 0U);
  return out;
}

std::optional<NoOpMessage> deserialize_no_op_message(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  NoOpMessage msg{};
  std::uint32_t reserved_hi = 0;
  std::uint8_t reserved = 0;
  std::uint8_t compressed = 0;

  r.deserialize_into(kNoOpMessageFormat, reserved_hi, msg.common.mtype, msg.common.mclass, reserved, compressed);

  if (compressed != 0) {
    return std::nullopt;
  }
  return msg;
}

std::array<std::byte, 4> serialize_channel_negotiation(const ChannelNegotiation &msg) {
  UALINK_TRACE_SCOPED(__func__);
  validate_common(msg.common);
  if (!fits_in_bits(msg.channel_response, 4) || !fits_in_bits(msg.channel_command, 4) || !fits_in_bits(msg.channel_target, 4)) {
    throw std::invalid_argument("Channel Negotiation: channel fields out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kChannelNegotiationFormat, 0U, msg.channel_response, msg.channel_command, msg.channel_target, 0U, msg.common.mtype,
              msg.common.mclass, 0U, 0U);
  return out;
}

std::optional<ChannelNegotiation> deserialize_channel_negotiation(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  ChannelNegotiation msg{};
  std::uint8_t reserved0 = 0;
  std::uint8_t reserved1 = 0;
  std::uint8_t reserved2 = 0;
  std::uint8_t compressed = 0;

  r.deserialize_into(kChannelNegotiationFormat, reserved0, msg.channel_response, msg.channel_command, msg.channel_target, reserved1,
                     msg.common.mtype, msg.common.mclass, reserved2, compressed);

  if (compressed != 0) {
    return std::nullopt;
  }
  return msg;
}

std::array<std::byte, 4> serialize_vendor_defined_packet_type_length(const VendorDefinedPacketTypeLength &msg) {
  UALINK_TRACE_SCOPED(__func__);
  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kVendorDefinedPacketTypeLengthFormat, msg.vendor_id, msg.type, msg.length);
  return out;
}

VendorDefinedPacketTypeLength deserialize_vendor_defined_packet_type_length(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);
  VendorDefinedPacketTypeLength msg{};
  r.deserialize_into(kVendorDefinedPacketTypeLengthFormat, msg.vendor_id, msg.type, msg.length);
  return msg;
}

} // namespace ualink::dl
