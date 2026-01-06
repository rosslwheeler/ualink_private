#include "ualink/tl_flit.h"

#include <algorithm>
#include <cstring>

using namespace ualink::tl;

std::array<std::byte, 8> ualink::tl::encode_tl_request_header(const TlRequestHeader& header) {
  UALINK_TRACE_SCOPED(__func__);

  if (header.tag > 0xFFF) {
    throw std::invalid_argument("encode_tl_request_header: tag out of range");
  }
  if (header.size > 0x3F) {
    throw std::invalid_argument("encode_tl_request_header: size out of range");
  }
  if (header.address > 0x3FFFFFFFFFFULL) {
    throw std::invalid_argument("encode_tl_request_header: address out of range (max 42 bits)");
  }

  std::array<std::byte, 8> buffer{};
  bit_fields::NetworkBitWriter writer(buffer);

  std::uint8_t half_flit_bit = header.half_flit ? 1U : 0U;
  std::uint16_t address_hi = static_cast<std::uint16_t>((header.address >> 26) & 0xFFFFU);
  std::uint32_t address_lo = static_cast<std::uint32_t>(header.address & 0x3FFFFFFU);

  writer.serialize(kTlRequestHeaderFormat,
                   static_cast<std::uint8_t>(header.opcode),
                   half_flit_bit,
                   header.size,
                   header.tag,
                   address_hi,
                   address_lo);

  return buffer;
}

TlRequestHeader ualink::tl::decode_tl_request_header(std::span<const std::byte, 8> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  bit_fields::NetworkBitReader reader(bytes);
  TlRequestHeader header{};

  std::uint8_t opcode = 0;
  std::uint8_t half_flit_bit = 0;
  std::uint16_t address_hi = 0;
  std::uint32_t address_lo = 0;

  reader.deserialize_into(kTlRequestHeaderFormat,
                          opcode,
                          half_flit_bit,
                          header.size,
                          header.tag,
                          address_hi,
                          address_lo);

  header.opcode = static_cast<TlOpcode>(opcode);
  header.half_flit = (half_flit_bit != 0);
  header.address = (static_cast<std::uint64_t>(address_hi) << 26) | address_lo;

  return header;
}

std::array<std::byte, 4> ualink::tl::encode_tl_response_header(const TlResponseHeader& header) {
  UALINK_TRACE_SCOPED(__func__);

  if (header.tag > 0xFFF) {
    throw std::invalid_argument("encode_tl_response_header: tag out of range");
  }
  if (header.status > 0xF) {
    throw std::invalid_argument("encode_tl_response_header: status out of range");
  }

  std::array<std::byte, 4> buffer{};
  bit_fields::NetworkBitWriter writer(buffer);

  std::uint8_t half_flit_bit = header.half_flit ? 1U : 0U;
  std::uint8_t data_valid_bit = header.data_valid ? 1U : 0U;

  writer.serialize(kTlResponseHeaderFormat,
                   static_cast<std::uint8_t>(header.opcode),
                   half_flit_bit,
                   header.status,
                   header.tag,
                   data_valid_bit,
                   0U);  // reserved

  return buffer;
}

TlResponseHeader ualink::tl::decode_tl_response_header(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  bit_fields::NetworkBitReader reader(bytes);
  TlResponseHeader header{};

  std::uint8_t opcode = 0;
  std::uint8_t half_flit_bit = 0;
  std::uint8_t data_valid_bit = 0;
  std::uint16_t reserved = 0;

  reader.deserialize_into(kTlResponseHeaderFormat,
                          opcode,
                          half_flit_bit,
                          header.status,
                          header.tag,
                          data_valid_bit,
                          reserved);

  header.opcode = static_cast<TlOpcode>(opcode);
  header.half_flit = (half_flit_bit != 0);
  header.data_valid = (data_valid_bit != 0);

  return header;
}

std::array<std::byte, kTlFlitBytes> TlSerializer::serialize_read_request(const TlReadRequest& request,
                                                                  TlMessageType message_type) {
  UALINK_TRACE_SCOPED(__func__);

  std::array<std::byte, kTlFlitBytes> flit{};
  const std::array<std::byte, 8> header = encode_tl_request_header(request.header);

  std::copy_n(header.begin(), 8, flit.begin());

  // Read requests don't have payload data in the request flit
  // Rest of the flit is zeros (already initialized)

  return flit;
}

std::array<std::byte, kTlFlitBytes> TlSerializer::serialize_read_response(const TlReadResponse& response,
                                                                   TlMessageType message_type) {
  UALINK_TRACE_SCOPED(__func__);

  std::array<std::byte, kTlFlitBytes> flit{};
  const std::array<std::byte, 4> header = encode_tl_response_header(response.header);

  std::copy_n(header.begin(), 4, flit.begin());

  // Copy response data (56 bytes)
  std::copy_n(response.data.begin(), response.data.size(), flit.begin() + 4);

  return flit;
}

std::array<std::byte, kTlFlitBytes> TlSerializer::serialize_write_request(const TlWriteRequest& request,
                                                                   TlMessageType message_type) {
  UALINK_TRACE_SCOPED(__func__);

  std::array<std::byte, kTlFlitBytes> flit{};
  const std::array<std::byte, 8> header = encode_tl_request_header(request.header);

  std::copy_n(header.begin(), 8, flit.begin());

  // Copy write data (56 bytes)
  std::copy_n(request.data.begin(), request.data.size(), flit.begin() + 8);

  return flit;
}

std::array<std::byte, kTlFlitBytes> TlSerializer::serialize_write_completion(
    const TlWriteCompletion& completion,
    TlMessageType message_type) {
  UALINK_TRACE_SCOPED(__func__);

  std::array<std::byte, kTlFlitBytes> flit{};
  const std::array<std::byte, 4> header = encode_tl_response_header(completion.header);

  std::copy_n(header.begin(), 4, flit.begin());

  // Write completions don't have payload data
  // Rest of the flit is zeros (already initialized)

  return flit;
}

TlOpcode TlDeserializer::decode_opcode(std::span<const std::byte, kTlFlitBytes> flit) {
  UALINK_TRACE_SCOPED(__func__);

  // Opcode is in the first 3 bits
  const std::uint8_t first_byte = static_cast<std::uint8_t>(flit[0]);
  const std::uint8_t opcode = (first_byte >> 5) & 0x7U;

  return static_cast<TlOpcode>(opcode);
}

std::optional<TlReadRequest> TlDeserializer::deserialize_read_request(
    std::span<const std::byte, kTlFlitBytes> flit) {
  UALINK_TRACE_SCOPED(__func__);

  const TlOpcode opcode = decode_opcode(flit);
  if (opcode != TlOpcode::kReadRequest) {
    return std::nullopt;
  }

  TlReadRequest request{};
  std::array<std::byte, 8> header_bytes{};
  std::copy_n(flit.begin(), 8, header_bytes.begin());
  request.header = decode_tl_request_header(header_bytes);

  return request;
}

std::optional<TlReadResponse> TlDeserializer::deserialize_read_response(
    std::span<const std::byte, kTlFlitBytes> flit) {
  UALINK_TRACE_SCOPED(__func__);

  const TlOpcode opcode = decode_opcode(flit);
  if (opcode != TlOpcode::kReadResponse) {
    return std::nullopt;
  }

  TlReadResponse response{};
  std::array<std::byte, 4> header_bytes{};
  std::copy_n(flit.begin(), 4, header_bytes.begin());
  response.header = decode_tl_response_header(header_bytes);

  // Copy response data (56 bytes)
  std::copy_n(flit.begin() + 4, response.data.size(), response.data.begin());

  return response;
}

std::optional<TlWriteRequest> TlDeserializer::deserialize_write_request(
    std::span<const std::byte, kTlFlitBytes> flit) {
  UALINK_TRACE_SCOPED(__func__);

  const TlOpcode opcode = decode_opcode(flit);
  if (opcode != TlOpcode::kWriteRequest) {
    return std::nullopt;
  }

  TlWriteRequest request{};
  std::array<std::byte, 8> header_bytes{};
  std::copy_n(flit.begin(), 8, header_bytes.begin());
  request.header = decode_tl_request_header(header_bytes);

  // Copy write data (56 bytes)
  std::copy_n(flit.begin() + 8, request.data.size(), request.data.begin());

  return request;
}

std::optional<TlWriteCompletion> TlDeserializer::deserialize_write_completion(
    std::span<const std::byte, kTlFlitBytes> flit) {
  UALINK_TRACE_SCOPED(__func__);

  const TlOpcode opcode = decode_opcode(flit);
  if (opcode != TlOpcode::kWriteCompletion) {
    return std::nullopt;
  }

  TlWriteCompletion completion{};
  std::array<std::byte, 4> header_bytes{};
  std::copy_n(flit.begin(), 4, header_bytes.begin());
  completion.header = decode_tl_response_header(header_bytes);

  return completion;
}
