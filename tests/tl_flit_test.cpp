#include "ualink/tl_flit.h"

#include <algorithm>
#include <cassert>
#include <iostream>

#include "ualink/trace.h"

using namespace ualink::tl;

static void test_encode_decode_request_header() {
  UALINK_TRACE_SCOPED(__func__);

  TlRequestHeader original{};
  original.opcode = TlOpcode::kReadRequest;
  original.half_flit = false;
  original.size = 0x20;
  original.tag = 0xABC;
  original.address = 0x123456789ABULL;

  const std::array<std::byte, 8> encoded = encode_tl_request_header(original);

  // Verify encoded bit fields
  {
    bit_fields::NetworkBitReader reader(encoded);
    const auto parsed = reader.deserialize(kTlRequestHeaderFormat);
    const std::uint16_t address_hi = static_cast<std::uint16_t>((original.address >> 26) & 0xFFFFU);
    const std::uint32_t address_lo = static_cast<std::uint32_t>(original.address & 0x3FFFFFFU);
    const std::array<bit_fields::ExpectedField, 6> expected{{
        {"opcode", static_cast<std::uint8_t>(original.opcode)},
        {"half_flit", original.half_flit ? 1U : 0U},
        {"size", original.size},
        {"tag", original.tag},
        {"address_hi", address_hi},
        {"address_lo", address_lo},
    }};
    reader.assert_expected(parsed, expected);
  }

  // Verify decode produces same values
  const TlRequestHeader decoded = decode_tl_request_header(encoded);
  assert(decoded.opcode == original.opcode);
  assert(decoded.half_flit == original.half_flit);
  assert(decoded.size == original.size);
  assert(decoded.tag == original.tag);
  assert(decoded.address == original.address);

  std::cout << "test_encode_decode_request_header: PASS\n";
}

static void test_encode_decode_response_header() {
  UALINK_TRACE_SCOPED(__func__);

  TlResponseHeader original{};
  original.opcode = TlOpcode::kReadResponse;
  original.half_flit = false;
  original.status = 0x5;
  original.tag = 0xDEF;
  original.data_valid = true;

  const std::array<std::byte, 4> encoded = encode_tl_response_header(original);

  // Verify encoded bit fields
  {
    bit_fields::NetworkBitReader reader(encoded);
    const auto parsed = reader.deserialize(kTlResponseHeaderFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"opcode", static_cast<std::uint8_t>(original.opcode)},
        {"half_flit", original.half_flit ? 1U : 0U},
        {"status", original.status},
        {"tag", original.tag},
        {"data_valid", original.data_valid ? 1U : 0U},
    }};
    reader.assert_expected(parsed, expected);
  }

  // Verify decode produces same values
  const TlResponseHeader decoded = decode_tl_response_header(encoded);
  assert(decoded.opcode == original.opcode);
  assert(decoded.half_flit == original.half_flit);
  assert(decoded.status == original.status);
  assert(decoded.tag == original.tag);
  assert(decoded.data_valid == original.data_valid);

  std::cout << "test_encode_decode_response_header: PASS\n";
}

static void test_pack_deserialize_read_request() {
  UALINK_TRACE_SCOPED(__func__);

  TlReadRequest request{};
  request.header.opcode = TlOpcode::kReadRequest;
  request.header.half_flit = false;
  request.header.size = 32;  // 6-bit field, max value is 63
  request.header.tag = 0x123;
  request.header.address = 0x100000000ULL;

  const std::array<std::byte, kTlFlitBytes> packed = TlSerializer::serialize_read_request(request);

  // Verify encoded header in packed flit
  {
    std::array<std::byte, 8> header_bytes{};
    std::copy_n(packed.begin(), 8, header_bytes.begin());
    bit_fields::NetworkBitReader reader(header_bytes);
    const auto parsed = reader.deserialize(kTlRequestHeaderFormat);
    const std::uint16_t address_hi = static_cast<std::uint16_t>((request.header.address >> 26) & 0xFFFFU);
    const std::uint32_t address_lo = static_cast<std::uint32_t>(request.header.address & 0x3FFFFFFU);
    const std::array<bit_fields::ExpectedField, 6> expected{{
        {"opcode", static_cast<std::uint8_t>(request.header.opcode)},
        {"half_flit", request.header.half_flit ? 1U : 0U},
        {"size", request.header.size},
        {"tag", request.header.tag},
        {"address_hi", address_hi},
        {"address_lo", address_lo},
    }};
    reader.assert_expected(parsed, expected);
  }

  const std::optional<TlReadRequest> unpacked = TlDeserializer::deserialize_read_request(packed);
  assert(unpacked.has_value());

  // Verify unpacked header matches original using bit fields
  {
    const std::array<std::byte, 8> unpacked_encoded = encode_tl_request_header(unpacked->header);
    bit_fields::NetworkBitReader reader(unpacked_encoded);
    const auto parsed = reader.deserialize(kTlRequestHeaderFormat);
    const std::uint16_t address_hi = static_cast<std::uint16_t>((request.header.address >> 26) & 0xFFFFU);
    const std::uint32_t address_lo = static_cast<std::uint32_t>(request.header.address & 0x3FFFFFFU);
    const std::array<bit_fields::ExpectedField, 6> expected{{
        {"opcode", static_cast<std::uint8_t>(request.header.opcode)},
        {"half_flit", request.header.half_flit ? 1U : 0U},
        {"size", request.header.size},
        {"tag", request.header.tag},
        {"address_hi", address_hi},
        {"address_lo", address_lo},
    }};
    reader.assert_expected(parsed, expected);
  }

  std::cout << "test_pack_deserialize_read_request: PASS\n";
}

static void test_pack_deserialize_read_response() {
  UALINK_TRACE_SCOPED(__func__);

  TlReadResponse response{};
  response.header.opcode = TlOpcode::kReadResponse;
  response.header.half_flit = false;
  response.header.status = 0;
  response.header.tag = 0x123;
  response.header.data_valid = true;

  // Fill data with pattern
  for (std::size_t byte_index = 0; byte_index < response.data.size(); ++byte_index) {
    response.data[byte_index] = std::byte{static_cast<unsigned char>(byte_index)};
  }

  const std::array<std::byte, kTlFlitBytes> packed = TlSerializer::serialize_read_response(response);

  // Verify encoded header in packed flit
  {
    std::array<std::byte, 4> header_bytes{};
    std::copy_n(packed.begin(), 4, header_bytes.begin());
    bit_fields::NetworkBitReader reader(header_bytes);
    const auto parsed = reader.deserialize(kTlResponseHeaderFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"opcode", static_cast<std::uint8_t>(response.header.opcode)},
        {"half_flit", response.header.half_flit ? 1U : 0U},
        {"status", response.header.status},
        {"tag", response.header.tag},
        {"data_valid", response.header.data_valid ? 1U : 0U},
    }};
    reader.assert_expected(parsed, expected);
  }

  const std::optional<TlReadResponse> unpacked = TlDeserializer::deserialize_read_response(packed);
  assert(unpacked.has_value());

  // Verify unpacked header matches original using bit fields
  {
    const std::array<std::byte, 4> unpacked_encoded = encode_tl_response_header(unpacked->header);
    bit_fields::NetworkBitReader reader(unpacked_encoded);
    const auto parsed = reader.deserialize(kTlResponseHeaderFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"opcode", static_cast<std::uint8_t>(response.header.opcode)},
        {"half_flit", response.header.half_flit ? 1U : 0U},
        {"status", response.header.status},
        {"tag", response.header.tag},
        {"data_valid", response.header.data_valid ? 1U : 0U},
    }};
    reader.assert_expected(parsed, expected);
  }

  // Verify data
  for (std::size_t byte_index = 0; byte_index < response.data.size(); ++byte_index) {
    assert(unpacked->data[byte_index] == response.data[byte_index]);
  }

  std::cout << "test_pack_deserialize_read_response: PASS\n";
}

static void test_pack_deserialize_write_request() {
  UALINK_TRACE_SCOPED(__func__);

  TlWriteRequest request{};
  request.header.opcode = TlOpcode::kWriteRequest;
  request.header.half_flit = false;
  request.header.size = 32;  // 6-bit field, max value is 63
  request.header.tag = 0x456;
  request.header.address = 0x200000000ULL;

  // Fill data with pattern
  for (std::size_t byte_index = 0; byte_index < request.data.size(); ++byte_index) {
    request.data[byte_index] = std::byte{static_cast<unsigned char>(0xFF - byte_index)};
  }

  const std::array<std::byte, kTlFlitBytes> packed = TlSerializer::serialize_write_request(request);

  // Verify encoded header in packed flit
  {
    std::array<std::byte, 8> header_bytes{};
    std::copy_n(packed.begin(), 8, header_bytes.begin());
    bit_fields::NetworkBitReader reader(header_bytes);
    const auto parsed = reader.deserialize(kTlRequestHeaderFormat);
    const std::uint16_t address_hi = static_cast<std::uint16_t>((request.header.address >> 26) & 0xFFFFU);
    const std::uint32_t address_lo = static_cast<std::uint32_t>(request.header.address & 0x3FFFFFFU);
    const std::array<bit_fields::ExpectedField, 6> expected{{
        {"opcode", static_cast<std::uint8_t>(request.header.opcode)},
        {"half_flit", request.header.half_flit ? 1U : 0U},
        {"size", request.header.size},
        {"tag", request.header.tag},
        {"address_hi", address_hi},
        {"address_lo", address_lo},
    }};
    reader.assert_expected(parsed, expected);
  }

  const std::optional<TlWriteRequest> unpacked = TlDeserializer::deserialize_write_request(packed);
  assert(unpacked.has_value());

  // Verify unpacked header matches original using bit fields
  {
    const std::array<std::byte, 8> unpacked_encoded = encode_tl_request_header(unpacked->header);
    bit_fields::NetworkBitReader reader(unpacked_encoded);
    const auto parsed = reader.deserialize(kTlRequestHeaderFormat);
    const std::uint16_t address_hi = static_cast<std::uint16_t>((request.header.address >> 26) & 0xFFFFU);
    const std::uint32_t address_lo = static_cast<std::uint32_t>(request.header.address & 0x3FFFFFFU);
    const std::array<bit_fields::ExpectedField, 6> expected{{
        {"opcode", static_cast<std::uint8_t>(request.header.opcode)},
        {"half_flit", request.header.half_flit ? 1U : 0U},
        {"size", request.header.size},
        {"tag", request.header.tag},
        {"address_hi", address_hi},
        {"address_lo", address_lo},
    }};
    reader.assert_expected(parsed, expected);
  }

  // Verify data
  for (std::size_t byte_index = 0; byte_index < request.data.size(); ++byte_index) {
    assert(unpacked->data[byte_index] == request.data[byte_index]);
  }

  std::cout << "test_pack_deserialize_write_request: PASS\n";
}

static void test_pack_deserialize_write_completion() {
  UALINK_TRACE_SCOPED(__func__);

  TlWriteCompletion completion{};
  completion.header.opcode = TlOpcode::kWriteCompletion;
  completion.header.half_flit = false;
  completion.header.status = 0;
  completion.header.tag = 0x456;
  completion.header.data_valid = false;

  const std::array<std::byte, kTlFlitBytes> packed = TlSerializer::serialize_write_completion(completion);

  // Verify encoded header in packed flit
  {
    std::array<std::byte, 4> header_bytes{};
    std::copy_n(packed.begin(), 4, header_bytes.begin());
    bit_fields::NetworkBitReader reader(header_bytes);
    const auto parsed = reader.deserialize(kTlResponseHeaderFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"opcode", static_cast<std::uint8_t>(completion.header.opcode)},
        {"half_flit", completion.header.half_flit ? 1U : 0U},
        {"status", completion.header.status},
        {"tag", completion.header.tag},
        {"data_valid", completion.header.data_valid ? 1U : 0U},
    }};
    reader.assert_expected(parsed, expected);
  }

  const std::optional<TlWriteCompletion> unpacked =
      TlDeserializer::deserialize_write_completion(packed);
  assert(unpacked.has_value());

  // Verify unpacked header matches original using bit fields
  {
    const std::array<std::byte, 4> unpacked_encoded = encode_tl_response_header(unpacked->header);
    bit_fields::NetworkBitReader reader(unpacked_encoded);
    const auto parsed = reader.deserialize(kTlResponseHeaderFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"opcode", static_cast<std::uint8_t>(completion.header.opcode)},
        {"half_flit", completion.header.half_flit ? 1U : 0U},
        {"status", completion.header.status},
        {"tag", completion.header.tag},
        {"data_valid", completion.header.data_valid ? 1U : 0U},
    }};
    reader.assert_expected(parsed, expected);
  }

  std::cout << "test_pack_deserialize_write_completion: PASS\n";
}

static void test_decode_opcode() {
  UALINK_TRACE_SCOPED(__func__);

  // Test read request
  TlReadRequest read_req{};
  read_req.header.opcode = TlOpcode::kReadRequest;
  std::array<std::byte, kTlFlitBytes> flit = TlSerializer::serialize_read_request(read_req);
  assert(TlDeserializer::decode_opcode(flit) == TlOpcode::kReadRequest);

  // Test write request
  TlWriteRequest write_req{};
  write_req.header.opcode = TlOpcode::kWriteRequest;
  flit = TlSerializer::serialize_write_request(write_req);
  assert(TlDeserializer::decode_opcode(flit) == TlOpcode::kWriteRequest);

  // Test read response
  TlReadResponse read_resp{};
  read_resp.header.opcode = TlOpcode::kReadResponse;
  flit = TlSerializer::serialize_read_response(read_resp);
  assert(TlDeserializer::decode_opcode(flit) == TlOpcode::kReadResponse);

  std::cout << "test_decode_opcode: PASS\n";
}

static void test_unpack_wrong_opcode() {
  UALINK_TRACE_SCOPED(__func__);

  // Pack a read request
  TlReadRequest request{};
  request.header.opcode = TlOpcode::kReadRequest;
  const std::array<std::byte, kTlFlitBytes> packed = TlSerializer::serialize_read_request(request);

  // Try to unpack as write request (should fail)
  const std::optional<TlWriteRequest> unpacked = TlDeserializer::deserialize_write_request(packed);
  assert(!unpacked.has_value());

  std::cout << "test_unpack_wrong_opcode: PASS\n";
}

static void test_half_flit_flag() {
  UALINK_TRACE_SCOPED(__func__);

  TlReadRequest request{};
  request.header.opcode = TlOpcode::kReadRequest;
  request.header.half_flit = true;
  request.header.size = 32;
  request.header.tag = 0x789;
  request.header.address = 0x300000000ULL;

  const std::array<std::byte, kTlFlitBytes> packed = TlSerializer::serialize_read_request(request);
  const std::optional<TlReadRequest> unpacked = TlDeserializer::deserialize_read_request(packed);

  assert(unpacked.has_value());
  assert(unpacked->header.half_flit == true);

  std::cout << "test_half_flit_flag: PASS\n";
}

static void test_address_42_bits() {
  UALINK_TRACE_SCOPED(__func__);

  TlRequestHeader header{};
  header.opcode = TlOpcode::kReadRequest;
  header.address = 0x3FFFFFFFFFFULL;  // Maximum 42-bit address

  const std::array<std::byte, 8> encoded = encode_tl_request_header(header);
  const TlRequestHeader decoded = decode_tl_request_header(encoded);

  assert(decoded.address == header.address);

  std::cout << "test_address_42_bits: PASS\n";
}

static void test_tag_12_bits() {
  UALINK_TRACE_SCOPED(__func__);

  TlRequestHeader header{};
  header.opcode = TlOpcode::kReadRequest;
  header.tag = 0xFFF;  // Maximum 12-bit tag

  const std::array<std::byte, 8> encoded = encode_tl_request_header(header);
  const TlRequestHeader decoded = decode_tl_request_header(encoded);

  assert(decoded.tag == header.tag);

  std::cout << "test_tag_12_bits: PASS\n";
}

static void test_message_type_conversion() {
  UALINK_TRACE_SCOPED(__func__);

  assert(tl_message_type_to_field(TlMessageType::kNone) == 0);
  assert(tl_message_type_to_field(TlMessageType::kStart) == 1);
  assert(tl_message_type_to_field(TlMessageType::kContinue) == 2);
  assert(tl_message_type_to_field(TlMessageType::kEnd) == 3);

  assert(tl_message_field_to_type(0) == TlMessageType::kNone);
  assert(tl_message_field_to_type(1) == TlMessageType::kStart);
  assert(tl_message_field_to_type(2) == TlMessageType::kContinue);
  assert(tl_message_field_to_type(3) == TlMessageType::kEnd);

  // Invalid field should return kNone
  assert(tl_message_field_to_type(4) == TlMessageType::kNone);

  std::cout << "test_message_type_conversion: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_encode_decode_request_header();
  test_encode_decode_response_header();
  test_pack_deserialize_read_request();
  test_pack_deserialize_read_response();
  test_pack_deserialize_write_request();
  test_pack_deserialize_write_completion();
  test_decode_opcode();
  test_unpack_wrong_opcode();
  test_half_flit_flag();
  test_address_42_bits();
  test_tag_12_bits();
  test_message_type_conversion();

  std::cout << "\nAll TL flit tests passed!\n";
  return 0;
}
