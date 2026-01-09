#include "ualink/upli_message.h"

#include <algorithm>
#include <stdexcept>

using namespace ualink::upli;

// Serialize message header
std::array<std::byte, 8> UpliMessageSerializer::serialize_message_header(
    const UpliMessageHeader& header) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges
  if (header.vc > 3) {
    throw std::invalid_argument("serialize_message_header: vc out of range (max 3)");
  }
  if (header.tag > 0xFFF) {
    throw std::invalid_argument("serialize_message_header: tag out of range (max 12 bits)");
  }
  if (header.size > 0x7) {
    throw std::invalid_argument("serialize_message_header: size out of range (max 3 bits)");
  }
  if (header.address > 0x3FFFFFFFFFFULL) {
    throw std::invalid_argument("serialize_message_header: address out of range (max 42 bits)");
  }

  std::array<std::byte, 8> buffer{};
  bit_fields::NetworkBitWriter writer(buffer);

  // Split address into hi/lo parts
  const std::uint16_t address_hi = static_cast<std::uint16_t>((header.address >> 26) & 0xFFFFU);
  const std::uint32_t address_lo = static_cast<std::uint32_t>(header.address & 0x3FFFFFFU);

  writer.serialize(kUpliMessageHeaderFormat,
                   static_cast<std::uint8_t>(header.opcode),
                   static_cast<std::uint8_t>(header.priority),
                   header.vc,
                   header.size,
                   header.tag,
                   address_hi,
                   address_lo);

  return buffer;
}

// Serialize response header
std::array<std::byte, 4> UpliMessageSerializer::serialize_response_header(
    const UpliResponseHeader& header) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges
  if (header.vc > 3) {
    throw std::invalid_argument("serialize_response_header: vc out of range (max 3)");
  }
  if (header.tag > 0xFFF) {
    throw std::invalid_argument("serialize_response_header: tag out of range (max 12 bits)");
  }
  if (header.status > 0xF) {
    throw std::invalid_argument("serialize_response_header: status out of range (max 4 bits)");
  }

  std::array<std::byte, 4> buffer{};
  bit_fields::NetworkBitWriter writer(buffer);

  const std::uint8_t data_valid_bit = header.data_valid ? 1U : 0U;

  writer.serialize(kUpliResponseHeaderFormat,
                   static_cast<std::uint8_t>(header.opcode),
                   static_cast<std::uint8_t>(header.priority),
                   header.vc,
                   header.status,
                   header.tag,
                   data_valid_bit,
                   0U);  // reserved

  return buffer;
}

// Serialize read request
UpliChannelFlit UpliMessageSerializer::serialize_read_request(
    const UpliReadRequest& request) {
  UALINK_TRACE_SCOPED(__func__);

  UpliChannelFlit flit{};
  const auto header_bytes = serialize_message_header(request.header);

  // Copy header into flit
  std::copy_n(header_bytes.begin(), 8, flit.data.begin());

  // Rest is zeros (already initialized)

  return flit;
}

// Serialize read response
UpliChannelFlit UpliMessageSerializer::serialize_read_response(
    const UpliReadResponse& response) {
  UALINK_TRACE_SCOPED(__func__);

  UpliChannelFlit flit{};
  const auto header_bytes = serialize_response_header(response.header);

  // Copy header (4 bytes)
  std::copy_n(header_bytes.begin(), 4, flit.data.begin());

  // Copy response data (60 bytes)
  std::copy_n(response.data.begin(), 60, flit.data.begin() + 4);

  return flit;
}

// Serialize write request
UpliChannelFlit UpliMessageSerializer::serialize_write_request(
    const UpliWriteRequest& request) {
  UALINK_TRACE_SCOPED(__func__);

  UpliChannelFlit flit{};
  const auto header_bytes = serialize_message_header(request.header);

  // Copy header (8 bytes)
  std::copy_n(header_bytes.begin(), 8, flit.data.begin());

  // Copy write data (56 bytes)
  std::copy_n(request.data.begin(), 56, flit.data.begin() + 8);

  return flit;
}

// Serialize write completion
UpliChannelFlit UpliMessageSerializer::serialize_write_completion(
    const UpliWriteCompletion& completion) {
  UALINK_TRACE_SCOPED(__func__);

  UpliChannelFlit flit{};
  const auto header_bytes = serialize_response_header(completion.header);

  // Copy header (4 bytes)
  std::copy_n(header_bytes.begin(), 4, flit.data.begin());

  // Rest is zeros (already initialized)

  return flit;
}

// Deserialize message header
UpliMessageHeader UpliMessageDeserializer::deserialize_message_header(
    std::span<const std::byte, 8> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  bit_fields::NetworkBitReader reader(bytes);
  UpliMessageHeader header{};

  std::uint8_t opcode = 0;
  std::uint8_t priority = 0;
  std::uint16_t address_hi = 0;
  std::uint32_t address_lo = 0;

  reader.deserialize_into(kUpliMessageHeaderFormat,
                          opcode,
                          priority,
                          header.vc,
                          header.size,
                          header.tag,
                          address_hi,
                          address_lo);

  header.opcode = static_cast<UpliOpcode>(opcode);
  header.priority = static_cast<UpliPriority>(priority);
  header.address = (static_cast<std::uint64_t>(address_hi) << 26) | address_lo;

  return header;
}

// Deserialize response header
UpliResponseHeader UpliMessageDeserializer::deserialize_response_header(
    std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  bit_fields::NetworkBitReader reader(bytes);
  UpliResponseHeader header{};

  std::uint8_t opcode = 0;
  std::uint8_t priority = 0;
  std::uint8_t data_valid_bit = 0;
  std::uint16_t reserved = 0;

  reader.deserialize_into(kUpliResponseHeaderFormat,
                          opcode,
                          priority,
                          header.vc,
                          header.status,
                          header.tag,
                          data_valid_bit,
                          reserved);

  header.opcode = static_cast<UpliOpcode>(opcode);
  header.priority = static_cast<UpliPriority>(priority);
  header.data_valid = (data_valid_bit != 0);

  return header;
}

// Deserialize opcode from flit
UpliOpcode UpliMessageDeserializer::deserialize_opcode(
    const UpliChannelFlit& flit) {
  UALINK_TRACE_SCOPED(__func__);

  // Opcode is in the first 3 bits
  const std::uint8_t first_byte = static_cast<std::uint8_t>(flit.data[0]);
  const std::uint8_t opcode = (first_byte >> 5) & 0x7U;

  return static_cast<UpliOpcode>(opcode);
}

// Deserialize read request
std::optional<UpliReadRequest> UpliMessageDeserializer::deserialize_read_request(
    const UpliChannelFlit& flit) {
  UALINK_TRACE_SCOPED(__func__);

  const UpliOpcode opcode = deserialize_opcode(flit);
  if (opcode != UpliOpcode::kReadRequest) {
    return std::nullopt;
  }

  UpliReadRequest request{};
  std::array<std::byte, 8> header_bytes{};
  std::copy_n(flit.data.begin(), 8, header_bytes.begin());
  request.header = deserialize_message_header(header_bytes);

  return request;
}

// Deserialize read response
std::optional<UpliReadResponse> UpliMessageDeserializer::deserialize_read_response(
    const UpliChannelFlit& flit) {
  UALINK_TRACE_SCOPED(__func__);

  const UpliOpcode opcode = deserialize_opcode(flit);
  if (opcode != UpliOpcode::kReadResponse) {
    return std::nullopt;
  }

  UpliReadResponse response{};
  std::array<std::byte, 4> header_bytes{};
  std::copy_n(flit.data.begin(), 4, header_bytes.begin());
  response.header = deserialize_response_header(header_bytes);

  // Copy response data (60 bytes)
  std::copy_n(flit.data.begin() + 4, 60, response.data.begin());

  return response;
}

// Deserialize write request
std::optional<UpliWriteRequest> UpliMessageDeserializer::deserialize_write_request(
    const UpliChannelFlit& flit) {
  UALINK_TRACE_SCOPED(__func__);

  const UpliOpcode opcode = deserialize_opcode(flit);
  if (opcode != UpliOpcode::kWriteRequest) {
    return std::nullopt;
  }

  UpliWriteRequest request{};
  std::array<std::byte, 8> header_bytes{};
  std::copy_n(flit.data.begin(), 8, header_bytes.begin());
  request.header = deserialize_message_header(header_bytes);

  // Copy write data (56 bytes)
  std::copy_n(flit.data.begin() + 8, 56, request.data.begin());

  return request;
}

// Deserialize write completion
std::optional<UpliWriteCompletion> UpliMessageDeserializer::deserialize_write_completion(
    const UpliChannelFlit& flit) {
  UALINK_TRACE_SCOPED(__func__);

  const UpliOpcode opcode = deserialize_opcode(flit);
  if (opcode != UpliOpcode::kWriteCompletion) {
    return std::nullopt;
  }

  UpliWriteCompletion completion{};
  std::array<std::byte, 4> header_bytes{};
  std::copy_n(flit.data.begin(), 4, header_bytes.begin());
  completion.header = deserialize_response_header(header_bytes);

  return completion;
}
