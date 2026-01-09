#include "ualink/upli_message.h"

#include <cassert>
#include <iostream>

using namespace ualink::upli;

// Test message header serialization/deserialization
void test_message_header_roundtrip() {
  std::cout << "test_message_header_roundtrip: ";

  UpliMessageHeader original{};
  original.opcode = UpliOpcode::kReadRequest;
  original.priority = UpliPriority::kHigh;
  original.vc = 2;
  original.size = 0x7;  // Max 3-bit value
  original.tag = 0xABC;
  original.address = 0x123456789ABULL;

  const auto serialized = UpliMessageSerializer::serialize_message_header(original);
  const auto deserialized = UpliMessageDeserializer::deserialize_message_header(serialized);

  assert(deserialized.opcode == original.opcode);
  assert(deserialized.priority == original.priority);
  assert(deserialized.vc == original.vc);
  assert(deserialized.size == original.size);
  assert(deserialized.tag == original.tag);
  assert(deserialized.address == original.address);

  std::cout << "PASS\n";
}

// Test response header serialization/deserialization
void test_response_header_roundtrip() {
  std::cout << "test_response_header_roundtrip: ";

  UpliResponseHeader original{};
  original.opcode = UpliOpcode::kReadResponse;
  original.priority = UpliPriority::kCritical;
  original.vc = 1;
  original.status = 0x5;
  original.tag = 0xDEF;
  original.data_valid = true;

  const auto serialized = UpliMessageSerializer::serialize_response_header(original);
  const auto deserialized = UpliMessageDeserializer::deserialize_response_header(serialized);

  assert(deserialized.opcode == original.opcode);
  assert(deserialized.priority == original.priority);
  assert(deserialized.vc == original.vc);
  assert(deserialized.status == original.status);
  assert(deserialized.tag == original.tag);
  assert(deserialized.data_valid == original.data_valid);

  std::cout << "PASS\n";
}

// Test read request serialization/deserialization
void test_read_request_roundtrip() {
  std::cout << "test_read_request_roundtrip: ";

  UpliReadRequest original{};
  original.header.opcode = UpliOpcode::kReadRequest;
  original.header.priority = UpliPriority::kMedium;
  original.header.vc = 0;
  original.header.size = 4;  // 3-bit field (0-7)
  original.header.tag = 0x123;
  original.header.address = 0x100000000ULL;

  const auto flit = UpliMessageSerializer::serialize_read_request(original);
  const auto deserialized = UpliMessageDeserializer::deserialize_read_request(flit);

  assert(deserialized.has_value());
  assert(deserialized->header.opcode == original.header.opcode);
  assert(deserialized->header.priority == original.header.priority);
  assert(deserialized->header.vc == original.header.vc);
  assert(deserialized->header.size == original.header.size);
  assert(deserialized->header.tag == original.header.tag);
  assert(deserialized->header.address == original.header.address);

  std::cout << "PASS\n";
}

// Test read response serialization/deserialization
void test_read_response_roundtrip() {
  std::cout << "test_read_response_roundtrip: ";

  UpliReadResponse original{};
  original.header.opcode = UpliOpcode::kReadResponse;
  original.header.priority = UpliPriority::kHigh;
  original.header.vc = 3;
  original.header.status = 0;
  original.header.tag = 0x456;
  original.header.data_valid = true;

  // Fill data with pattern
  for (std::size_t i = 0; i < original.data.size(); ++i) {
    original.data[i] = std::byte{static_cast<unsigned char>(i & 0xFF)};
  }

  const auto flit = UpliMessageSerializer::serialize_read_response(original);
  const auto deserialized = UpliMessageDeserializer::deserialize_read_response(flit);

  assert(deserialized.has_value());
  assert(deserialized->header.opcode == original.header.opcode);
  assert(deserialized->header.priority == original.header.priority);
  assert(deserialized->header.vc == original.header.vc);
  assert(deserialized->header.status == original.header.status);
  assert(deserialized->header.tag == original.header.tag);
  assert(deserialized->header.data_valid == original.header.data_valid);

  // Verify data
  for (std::size_t i = 0; i < 60; ++i) {
    assert(deserialized->data[i] == original.data[i]);
  }

  std::cout << "PASS\n";
}

// Test write request serialization/deserialization
void test_write_request_roundtrip() {
  std::cout << "test_write_request_roundtrip: ";

  UpliWriteRequest original{};
  original.header.opcode = UpliOpcode::kWriteRequest;
  original.header.priority = UpliPriority::kLow;
  original.header.vc = 2;
  original.header.size = 2;  // 3-bit field (0-7)
  original.header.tag = 0x789;
  original.header.address = 0x200000000ULL;

  // Fill data with pattern
  for (std::size_t i = 0; i < original.data.size(); ++i) {
    original.data[i] = std::byte{static_cast<unsigned char>(0xFF - (i & 0xFF))};
  }

  const auto flit = UpliMessageSerializer::serialize_write_request(original);
  const auto deserialized = UpliMessageDeserializer::deserialize_write_request(flit);

  assert(deserialized.has_value());
  assert(deserialized->header.opcode == original.header.opcode);
  assert(deserialized->header.priority == original.header.priority);
  assert(deserialized->header.vc == original.header.vc);
  assert(deserialized->header.size == original.header.size);
  assert(deserialized->header.tag == original.header.tag);
  assert(deserialized->header.address == original.header.address);

  // Verify data
  for (std::size_t i = 0; i < original.data.size(); ++i) {
    assert(deserialized->data[i] == original.data[i]);
  }

  std::cout << "PASS\n";
}

// Test write completion serialization/deserialization
void test_write_completion_roundtrip() {
  std::cout << "test_write_completion_roundtrip: ";

  UpliWriteCompletion original{};
  original.header.opcode = UpliOpcode::kWriteCompletion;
  original.header.priority = UpliPriority::kMedium;
  original.header.vc = 1;
  original.header.status = 0;
  original.header.tag = 0xABC;
  original.header.data_valid = false;

  const auto flit = UpliMessageSerializer::serialize_write_completion(original);
  const auto deserialized = UpliMessageDeserializer::deserialize_write_completion(flit);

  assert(deserialized.has_value());
  assert(deserialized->header.opcode == original.header.opcode);
  assert(deserialized->header.priority == original.header.priority);
  assert(deserialized->header.vc == original.header.vc);
  assert(deserialized->header.status == original.header.status);
  assert(deserialized->header.tag == original.header.tag);
  assert(deserialized->header.data_valid == original.header.data_valid);

  std::cout << "PASS\n";
}

// Test opcode deserialization
void test_deserialize_opcode() {
  std::cout << "test_deserialize_opcode: ";

  // Test read request
  UpliReadRequest read_req{};
  read_req.header.opcode = UpliOpcode::kReadRequest;
  auto flit = UpliMessageSerializer::serialize_read_request(read_req);
  assert(UpliMessageDeserializer::deserialize_opcode(flit) == UpliOpcode::kReadRequest);

  // Test write request
  UpliWriteRequest write_req{};
  write_req.header.opcode = UpliOpcode::kWriteRequest;
  flit = UpliMessageSerializer::serialize_write_request(write_req);
  assert(UpliMessageDeserializer::deserialize_opcode(flit) == UpliOpcode::kWriteRequest);

  // Test read response
  UpliReadResponse read_resp{};
  read_resp.header.opcode = UpliOpcode::kReadResponse;
  flit = UpliMessageSerializer::serialize_read_response(read_resp);
  assert(UpliMessageDeserializer::deserialize_opcode(flit) == UpliOpcode::kReadResponse);

  std::cout << "PASS\n";
}

// Test wrong opcode deserialization
void test_wrong_opcode_deserialization() {
  std::cout << "test_wrong_opcode_deserialization: ";

  // Serialize a read request
  UpliReadRequest request{};
  request.header.opcode = UpliOpcode::kReadRequest;
  const auto flit = UpliMessageSerializer::serialize_read_request(request);

  // Try to deserialize as write request (should fail)
  const auto wrong = UpliMessageDeserializer::deserialize_write_request(flit);
  assert(!wrong.has_value());

  std::cout << "PASS\n";
}

// Test all priority levels
void test_priority_levels() {
  std::cout << "test_priority_levels: ";

  for (std::uint8_t p = 0; p < 4; ++p) {
    UpliMessageHeader header{};
    header.priority = static_cast<UpliPriority>(p);

    const auto serialized = UpliMessageSerializer::serialize_message_header(header);
    const auto deserialized = UpliMessageDeserializer::deserialize_message_header(serialized);

    assert(deserialized.priority == header.priority);
  }

  std::cout << "PASS\n";
}

// Test all virtual channels
void test_virtual_channels() {
  std::cout << "test_virtual_channels: ";

  for (std::uint8_t vc = 0; vc < 4; ++vc) {
    UpliMessageHeader header{};
    header.vc = vc;

    const auto serialized = UpliMessageSerializer::serialize_message_header(header);
    const auto deserialized = UpliMessageDeserializer::deserialize_message_header(serialized);

    assert(deserialized.vc == header.vc);
  }

  std::cout << "PASS\n";
}

// Test maximum address (42 bits)
void test_max_address() {
  std::cout << "test_max_address: ";

  UpliMessageHeader header{};
  header.address = 0x3FFFFFFFFFFULL;  // Maximum 42-bit address

  const auto serialized = UpliMessageSerializer::serialize_message_header(header);
  const auto deserialized = UpliMessageDeserializer::deserialize_message_header(serialized);

  assert(deserialized.address == header.address);

  std::cout << "PASS\n";
}

// Test maximum tag (12 bits)
void test_max_tag() {
  std::cout << "test_max_tag: ";

  UpliMessageHeader header{};
  header.tag = 0xFFF;  // Maximum 12-bit tag

  const auto serialized = UpliMessageSerializer::serialize_message_header(header);
  const auto deserialized = UpliMessageDeserializer::deserialize_message_header(serialized);

  assert(deserialized.tag == header.tag);

  std::cout << "PASS\n";
}

int main() {
  std::cout << "\n=== UPLI Message Tests ===\n\n";

  // Basic serialization/deserialization tests
  test_message_header_roundtrip();
  test_response_header_roundtrip();

  // Message type tests
  test_read_request_roundtrip();
  test_read_response_roundtrip();
  test_write_request_roundtrip();
  test_write_completion_roundtrip();

  // Opcode tests
  test_deserialize_opcode();
  test_wrong_opcode_deserialization();

  // Field range tests
  test_priority_levels();
  test_virtual_channels();
  test_max_address();
  test_max_tag();

  std::cout << "\n=== All UPLI Message Tests Passed ===\n";
  return 0;
}
