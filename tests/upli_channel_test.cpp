#include "ualink/upli_channel.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace ualink::upli;

// Helper to verify UpliRequestFields match using bit_fields verification pattern
void verify_request_fields_match(const UpliRequestFields& expected,
                                  const UpliRequestFields& actual) {
  bit_fields::NetworkBitReader reader(serialize_upli_request(actual));
  const auto parsed = reader.deserialize(kUpliRequestFormat);

  const bit_fields::ExpectedTable<13> kExpected{{
      {"req_vld", expected.req_vld ? 1U : 0U},
      {"req_port_id", expected.req_port_id},
      {"req_src_phys_acc_id", expected.req_src_phys_acc_id},
      {"req_dst_phys_acc_id", expected.req_dst_phys_acc_id},
      {"req_tag", expected.req_tag},
      {"req_addr", expected.req_addr},
      {"req_cmd", expected.req_cmd},
      {"req_len", expected.req_len},
      {"req_num_beats", expected.req_num_beats},
      {"req_attr", expected.req_attr},
      {"req_meta_data", expected.req_meta_data},
      {"req_vc", expected.req_vc},
      {"req_auth_tag", expected.req_auth_tag},
  }};

  reader.assert_expected(parsed, kExpected);
}

// Test Request channel serialize/deserialize round-trip
void test_request_round_trip() {
  std::cout << "test_request_round_trip: ";

  UpliRequestFields original{};
  original.req_vld = true;
  original.req_port_id = 2;
  original.req_src_phys_acc_id = 0x123;  // 10 bits
  original.req_dst_phys_acc_id = 0x3FF;  // 10 bits max
  original.req_tag = 0x456;              // 11 bits
  original.req_addr = 0x123456789ABCDEF; // 57 bits
  original.req_cmd = 0x28;               // Write command
  original.req_len = 15;                 // 16 doublewords
  original.req_num_beats = 3;            // 4 beats
  original.req_attr = 0xAB;
  original.req_meta_data = 0xCD;
  original.req_vc = 1;
  original.req_auth_tag = 0xFEDCBA9876543210ULL;

  // Serialize
  const auto serialized = serialize_upli_request(original);

  // Deserialize
  const auto deserialized = deserialize_upli_request(serialized);

  // Verify all fields match using bit_fields library verification
  verify_request_fields_match(original, deserialized);

  std::cout << "PASS\n";
}

// Helper to verify UpliOrigDataFields match
void verify_orig_data_fields_match(const UpliOrigDataFields& expected,
                                    const UpliOrigDataFields& actual) {
  const auto serialized = serialize_upli_orig_data(actual);
  const std::size_t ctrl_bits = kUpliOrigDataControlFormat.total_bits();
  const std::size_t ctrl_bytes = (ctrl_bits + 7) / 8;

  bit_fields::NetworkBitReader reader(std::span(serialized.data(), ctrl_bytes));
  const auto parsed = reader.deserialize(kUpliOrigDataControlFormat);

  const bit_fields::ExpectedTable<4> kExpected{{
      {"orig_data_vld", expected.orig_data_vld ? 1U : 0U},
      {"orig_data_port_id", expected.orig_data_port_id},
      {"orig_data_error", expected.orig_data_error ? 1U : 0U},
      {"_reserved", 0U},
  }};

  reader.assert_expected(parsed, kExpected);
  assert(actual.data == expected.data);
}

// Test OrigData channel serialize/deserialize round-trip
void test_orig_data_round_trip() {
  std::cout << "test_orig_data_round_trip: ";

  UpliOrigDataFields original{};
  original.orig_data_vld = true;
  original.orig_data_port_id = 3;
  original.orig_data_error = false;

  // Fill data with pattern
  for (std::size_t byte_index = 0; byte_index < kUpliDataBeatBytes; ++byte_index) {
    original.data[byte_index] = std::byte(byte_index & 0xFF);
  }

  // Serialize
  const auto serialized = serialize_upli_orig_data(original);

  // Deserialize
  const auto deserialized = deserialize_upli_orig_data(serialized);

  // Verify all fields match using bit_fields library verification
  verify_orig_data_fields_match(original, deserialized);

  std::cout << "PASS\n";
}

// Helper to verify UpliRdRspFields match
void verify_rd_rsp_fields_match(const UpliRdRspFields& expected,
                                 const UpliRdRspFields& actual) {
  const auto serialized = serialize_upli_rd_rsp(actual);
  const std::size_t ctrl_bits = kUpliRdRspFormat.total_bits();
  const std::size_t ctrl_bytes = (ctrl_bits + 7) / 8;

  bit_fields::NetworkBitReader reader(std::span(serialized.data(), ctrl_bytes));
  const auto parsed = reader.deserialize(kUpliRdRspFormat);

  const bit_fields::ExpectedTable<8> kExpected{{
      {"rd_rsp_vld", expected.rd_rsp_vld ? 1U : 0U},
      {"rd_rsp_port_id", expected.rd_rsp_port_id},
      {"rd_rsp_tag", expected.rd_rsp_tag},
      {"rd_rsp_status", expected.rd_rsp_status},
      {"rd_rsp_attr", expected.rd_rsp_attr},
      {"rd_rsp_data_error", expected.rd_rsp_data_error ? 1U : 0U},
      {"rd_rsp_auth_tag", expected.rd_rsp_auth_tag},
      {"_reserved", 0U},
  }};

  reader.assert_expected(parsed, kExpected);
  assert(actual.data == expected.data);
}

// Test RdRsp channel serialize/deserialize round-trip
void test_rd_rsp_round_trip() {
  std::cout << "test_rd_rsp_round_trip: ";

  UpliRdRspFields original{};
  original.rd_rsp_vld = true;
  original.rd_rsp_port_id = 1;
  original.rd_rsp_tag = 0x7FF;  // 11 bits max
  original.rd_rsp_status = 0x0;  // OKAY
  original.rd_rsp_attr = 0x55;
  original.rd_rsp_data_error = false;
  original.rd_rsp_auth_tag = 0x1234567890ABCDEFULL;

  // Fill data with pattern
  for (std::size_t byte_index = 0; byte_index < kUpliDataBeatBytes; ++byte_index) {
    original.data[byte_index] = std::byte((byte_index * 3) & 0xFF);
  }

  // Serialize
  const auto serialized = serialize_upli_rd_rsp(original);

  // Deserialize
  const auto deserialized = deserialize_upli_rd_rsp(serialized);

  // Verify all fields match using bit_fields library verification
  verify_rd_rsp_fields_match(original, deserialized);

  std::cout << "PASS\n";
}

// Helper to verify UpliWrRspFields match
void verify_wr_rsp_fields_match(const UpliWrRspFields& expected,
                                 const UpliWrRspFields& actual) {
  bit_fields::NetworkBitReader reader(serialize_upli_wr_rsp(actual));
  const auto parsed = reader.deserialize(kUpliWrRspFormat);

  const bit_fields::ExpectedTable<7> kExpected{{
      {"wr_rsp_vld", expected.wr_rsp_vld ? 1U : 0U},
      {"wr_rsp_port_id", expected.wr_rsp_port_id},
      {"wr_rsp_tag", expected.wr_rsp_tag},
      {"wr_rsp_status", expected.wr_rsp_status},
      {"wr_rsp_attr", expected.wr_rsp_attr},
      {"wr_rsp_auth_tag", expected.wr_rsp_auth_tag},
      {"_reserved", 0U},
  }};

  reader.assert_expected(parsed, kExpected);
}

// Test WrRsp channel serialize/deserialize round-trip
void test_wr_rsp_round_trip() {
  std::cout << "test_wr_rsp_round_trip: ";

  UpliWrRspFields original{};
  original.wr_rsp_vld = true;
  original.wr_rsp_port_id = 0;
  original.wr_rsp_tag = 0x200;
  original.wr_rsp_status = 0x0;  // OKAY
  original.wr_rsp_attr = 0xAA;
  original.wr_rsp_auth_tag = 0xABCDEF0123456789ULL;

  // Serialize
  const auto serialized = serialize_upli_wr_rsp(original);

  // Deserialize
  const auto deserialized = deserialize_upli_wr_rsp(serialized);

  // Verify all fields match using bit_fields library verification
  verify_wr_rsp_fields_match(original, deserialized);

  std::cout << "PASS\n";
}

// Helper to verify UpliCreditReturn match
void verify_credit_return_fields_match(const UpliCreditReturn& expected,
                                        const UpliCreditReturn& actual) {
  const auto serialized = serialize_upli_credit_return(actual);
  const std::size_t port_bits = kUpliCreditPortFormat.total_bits();
  const std::size_t port_bytes = (port_bits + 7) / 8;

  // Verify each port's credit info using bit_fields library
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const std::size_t offset = port_index * port_bytes;
    bit_fields::NetworkBitReader reader(
        std::span(serialized.data() + offset, port_bytes));
    const auto parsed = reader.deserialize(kUpliCreditPortFormat);

    const bit_fields::ExpectedTable<4> kExpected{{
        {"credit_vld", expected.ports[port_index].credit_vld ? 1U : 0U},
        {"credit_pool", expected.ports[port_index].credit_pool ? 1U : 0U},
        {"credit_vc", expected.ports[port_index].credit_vc},
        {"credit_num", expected.ports[port_index].credit_num},
    }};

    reader.assert_expected(parsed, kExpected);
  }

  // Verify init_done flags
  const std::byte init_done_byte = serialized[kMaxPorts * port_bytes];
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const bool init_done =
        (init_done_byte & std::byte(1U << port_index)) != std::byte{0};
    assert(init_done == expected.credit_init_done[port_index]);
  }
}

// Test Credit Return serialize/deserialize round-trip
void test_credit_return_round_trip() {
  std::cout << "test_credit_return_round_trip: ";

  UpliCreditReturn original{};

  // Configure port 0
  original.ports[0].credit_vld = true;
  original.ports[0].credit_pool = false;
  original.ports[0].credit_vc = 2;
  original.ports[0].credit_num = 3;  // 4 credits
  original.credit_init_done[0] = true;

  // Configure port 1
  original.ports[1].credit_vld = true;
  original.ports[1].credit_pool = true;
  original.ports[1].credit_vc = 0;
  original.ports[1].credit_num = 1;  // 2 credits
  original.credit_init_done[1] = false;

  // Configure port 2
  original.ports[2].credit_vld = false;
  original.ports[2].credit_pool = false;
  original.ports[2].credit_vc = 1;
  original.ports[2].credit_num = 0;  // 1 credit
  original.credit_init_done[2] = true;

  // Configure port 3
  original.ports[3].credit_vld = true;
  original.ports[3].credit_pool = false;
  original.ports[3].credit_vc = 3;
  original.ports[3].credit_num = 2;  // 3 credits
  original.credit_init_done[3] = true;

  // Serialize
  const auto serialized = serialize_upli_credit_return(original);

  // Deserialize
  const auto deserialized = deserialize_upli_credit_return(serialized);

  // Verify all ports match using bit_fields library verification
  verify_credit_return_fields_match(original, deserialized);

  std::cout << "PASS\n";
}

// Test Request field range validation
void test_request_validation() {
  std::cout << "test_request_validation: ";

  UpliRequestFields fields{};
  fields.req_vld = true;

  // Test port_id range
  fields.req_port_id = 4;  // Out of range (max 3)
  try {
    [[maybe_unused]] const auto result = serialize_upli_request(fields);
    assert(false && "Should have thrown");
  } catch (const std::invalid_argument&) {
    // Expected
  }
  fields.req_port_id = 0;  // Reset

  // Test src_phys_acc_id range
  fields.req_src_phys_acc_id = 0x400;  // Out of range (max 0x3FF)
  try {
    [[maybe_unused]] const auto result = serialize_upli_request(fields);
    assert(false && "Should have thrown");
  } catch (const std::invalid_argument&) {
    // Expected
  }
  fields.req_src_phys_acc_id = 0;  // Reset

  // Test tag range
  fields.req_tag = 0x800;  // Out of range (max 0x7FF)
  try {
    [[maybe_unused]] const auto result = serialize_upli_request(fields);
    assert(false && "Should have thrown");
  } catch (const std::invalid_argument&) {
    // Expected
  }

  std::cout << "PASS\n";
}

// Test zero-initialized fields
void test_zero_fields() {
  std::cout << "test_zero_fields: ";

  // Request with all zeros
  UpliRequestFields req{};
  const auto req_serialized = serialize_upli_request(req);
  const auto req_deserialized = deserialize_upli_request(req_serialized);
  assert(req_deserialized.req_vld == false);
  assert(req_deserialized.req_port_id == 0);
  assert(req_deserialized.req_tag == 0);

  // WrRsp with all zeros
  UpliWrRspFields wr{};
  const auto wr_serialized = serialize_upli_wr_rsp(wr);
  const auto wr_deserialized = deserialize_upli_wr_rsp(wr_serialized);
  assert(wr_deserialized.wr_rsp_vld == false);
  assert(wr_deserialized.wr_rsp_tag == 0);

  std::cout << "PASS\n";
}

// Test maximum field values
void test_max_values() {
  std::cout << "test_max_values: ";

  UpliRequestFields req{};
  req.req_vld = true;
  req.req_port_id = 0x3;             // 2 bits max
  req.req_src_phys_acc_id = 0x3FF;   // 10 bits max
  req.req_dst_phys_acc_id = 0x3FF;   // 10 bits max
  req.req_tag = 0x7FF;               // 11 bits max
  req.req_addr = 0x1FFFFFFFFFFFFFFULL; // 57 bits max
  req.req_cmd = 0x3F;                // 6 bits max
  req.req_len = 0x3F;                // 6 bits max
  req.req_num_beats = 0x3;           // 2 bits max
  req.req_attr = 0xFF;               // 8 bits
  req.req_meta_data = 0xFF;          // 8 bits
  req.req_vc = 0x3;                  // 2 bits max
  req.req_auth_tag = 0xFFFFFFFFFFFFFFFFULL; // 64 bits

  const auto serialized = serialize_upli_request(req);
  const auto deserialized = deserialize_upli_request(serialized);

  assert(deserialized.req_port_id == req.req_port_id);
  assert(deserialized.req_src_phys_acc_id == req.req_src_phys_acc_id);
  assert(deserialized.req_tag == req.req_tag);
  assert(deserialized.req_addr == req.req_addr);
  assert(deserialized.req_cmd == req.req_cmd);
  assert(deserialized.req_vc == req.req_vc);

  std::cout << "PASS\n";
}

int main() {
  std::cout << "\n=== UPLI Channel Tests ===\n\n";

  // Round-trip tests
  test_request_round_trip();
  test_orig_data_round_trip();
  test_rd_rsp_round_trip();
  test_wr_rsp_round_trip();
  test_credit_return_round_trip();

  // Validation tests
  test_request_validation();
  test_zero_fields();
  test_max_values();

  std::cout << "\n=== All UPLI Channel Tests Passed ===\n";
  return 0;
}
