#include "ualink/crc.h"

#include <cassert>
#include <cstring>
#include <iostream>

#include "ualink/trace.h"

using namespace ualink::dl;

static void assert_crc_equals(const std::array<std::byte, 4>& actual,
                              std::uint8_t b0,
                              std::uint8_t b1,
                              std::uint8_t b2,
                              std::uint8_t b3) {
  assert(actual[0] == std::byte{b0});
  assert(actual[1] == std::byte{b1});
  assert(actual[2] == std::byte{b2});
  assert(actual[3] == std::byte{b3});
}

static void test_crc32_empty_data() {
  UALINK_TRACE_SCOPED(__func__);
  std::array<std::byte, 0> empty{};
  const std::array<std::byte, 4> crc = compute_crc32(empty);

  // CRC-32 of empty data should be ~0xFFFFFFFF = 0x00000000
  // In network byte order (big-endian)
  assert_crc_equals(crc, 0x00, 0x00, 0x00, 0x00);

  std::cout << "test_crc32_empty_data: PASS\n";
}

static void test_crc32_known_value() {
  UALINK_TRACE_SCOPED(__func__);
  // Test with "123456789" which has a well-known CRC-32 value
  const std::array<std::byte, 9> data{
      std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
      std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
      std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};

  const std::array<std::byte, 4> crc = compute_crc32(data);

  // Known CRC-32 value for "123456789" is 0xCBF43926 (IEEE 802.3)
  // In network byte order (big-endian): CB F4 39 26
  assert_crc_equals(crc, 0xCB, 0xF4, 0x39, 0x26);

  std::cout << "test_crc32_known_value: PASS\n";
}

static void test_crc32_single_byte() {
  UALINK_TRACE_SCOPED(__func__);
  const std::array<std::byte, 1> data{std::byte{0xAA}};
  const std::array<std::byte, 4> crc = compute_crc32(data);

  // Verify CRC is computed (not all zeros, not all ones)
  const bool all_zero = (crc[0] == std::byte{0x00} && crc[1] == std::byte{0x00} &&
                         crc[2] == std::byte{0x00} && crc[3] == std::byte{0x00});
  const bool all_ones = (crc[0] == std::byte{0xFF} && crc[1] == std::byte{0xFF} &&
                         crc[2] == std::byte{0xFF} && crc[3] == std::byte{0xFF});

  assert(!all_zero);
  assert(!all_ones);

  std::cout << "test_crc32_single_byte: PASS\n";
}

static void test_crc32_verify_success() {
  UALINK_TRACE_SCOPED(__func__);
  const std::array<std::byte, 9> data{
      std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
      std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
      std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};

  const std::array<std::byte, 4> crc = compute_crc32(data);
  const bool valid = verify_crc32(data, crc);

  assert(valid);

  std::cout << "test_crc32_verify_success: PASS\n";
}

static void test_crc32_verify_failure() {
  UALINK_TRACE_SCOPED(__func__);
  const std::array<std::byte, 9> data{
      std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
      std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
      std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};

  // Intentionally wrong CRC
  const std::array<std::byte, 4> wrong_crc{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

  const bool valid = verify_crc32(data, wrong_crc);

  assert(!valid);

  std::cout << "test_crc32_verify_failure: PASS\n";
}

static void test_crc32_data_corruption_detection() {
  UALINK_TRACE_SCOPED(__func__);
  std::array<std::byte, 9> data{
      std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
      std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
      std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};

  const std::array<std::byte, 4> original_crc = compute_crc32(data);

  // Corrupt one byte
  data[4] = std::byte{'X'};

  const bool valid = verify_crc32(data, original_crc);
  assert(!valid);

  std::cout << "test_crc32_data_corruption_detection: PASS\n";
}

static void test_crc32_deterministic() {
  UALINK_TRACE_SCOPED(__func__);
  const std::array<std::byte, 9> data{
      std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
      std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
      std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};

  const std::array<std::byte, 4> crc1 = compute_crc32(data);
  const std::array<std::byte, 4> crc2 = compute_crc32(data);

  assert(crc1 == crc2);

  std::cout << "test_crc32_deterministic: PASS\n";
}

static void test_crc32_large_buffer() {
  UALINK_TRACE_SCOPED(__func__);
  // Test with 1KB of data
  std::array<std::byte, 1024> data{};
  for (std::size_t byte_index = 0; byte_index < data.size(); ++byte_index) {
    data[byte_index] = static_cast<std::byte>(byte_index & 0xFF);
  }

  const std::array<std::byte, 4> crc = compute_crc32(data);
  const bool valid = verify_crc32(data, crc);

  assert(valid);

  std::cout << "test_crc32_large_buffer: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_crc32_empty_data();
  test_crc32_known_value();
  test_crc32_single_byte();
  test_crc32_verify_success();
  test_crc32_verify_failure();
  test_crc32_data_corruption_detection();
  test_crc32_deterministic();
  test_crc32_large_buffer();

  std::cout << "\nAll CRC tests passed!\n";
  return 0;
}