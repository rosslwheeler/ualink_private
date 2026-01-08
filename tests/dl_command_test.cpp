#include "ualink/dl_command.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>

#include "ualink/crc.h"
#include "ualink/dl_flit.h"

using namespace ualink::dl;

// Test ACK command creation
void test_ack_creation() {
  std::cout << "test_ack_creation: ";

  const std::uint16_t ack_seq = 0x123;  // 9-bit value
  const std::uint8_t flit_seq_lo = 0x5; // 3-bit value

  const DlFlit ack_flit = CommandFactory::create_ack(ack_seq, flit_seq_lo);

  // Deserialize header
  const CommandFlitHeaderFields header = deserialize_command_flit_header(ack_flit.flit_header);

  // Verify header fields
  assert(header.op == static_cast<std::uint8_t>(DlCommandOp::kAck));
  assert(header.payload == false);
  assert(header.ack_req_seq == ack_seq);
  assert(header.flit_seq_lo == flit_seq_lo);

  // Verify segment headers and payload are all zeros
  for (const auto byte : ack_flit.segment_headers) {
    assert(byte == std::byte{0});
  }
  for (const auto byte : ack_flit.payload) {
    assert(byte == std::byte{0});
  }

  // Verify CRC is valid
  constexpr std::size_t kCrcBufferSize = 3 + kDlSegmentCount + kDlPayloadBytes;
  std::array<std::byte, kCrcBufferSize> crc_buffer{};
  std::copy_n(ack_flit.flit_header.begin(), 3, crc_buffer.begin());
  std::copy_n(ack_flit.segment_headers.begin(), kDlSegmentCount, crc_buffer.begin() + 3);
  std::copy_n(ack_flit.payload.begin(), kDlPayloadBytes, crc_buffer.begin() + 3 + kDlSegmentCount);

  assert(verify_crc32(crc_buffer, ack_flit.crc));

  std::cout << "PASS\n";
}

// Test NAK command creation
void test_nak_creation() {
  std::cout << "test_nak_creation: ";

  const std::uint16_t nak_seq = 0x1FF;  // 9-bit max value
  const std::uint8_t flit_seq_lo = 0x7; // 3-bit max value

  const DlFlit nak_flit = CommandFactory::create_nak(nak_seq, flit_seq_lo);

  // Deserialize header
  const CommandFlitHeaderFields header = deserialize_command_flit_header(nak_flit.flit_header);

  // Verify header fields
  assert(header.op == static_cast<std::uint8_t>(DlCommandOp::kNak));
  assert(header.payload == false);
  assert(header.ack_req_seq == nak_seq);
  assert(header.flit_seq_lo == flit_seq_lo);

  // Verify CRC is valid
  constexpr std::size_t kCrcBufferSize = 3 + kDlSegmentCount + kDlPayloadBytes;
  std::array<std::byte, kCrcBufferSize> crc_buffer{};
  std::copy_n(nak_flit.flit_header.begin(), 3, crc_buffer.begin());
  std::copy_n(nak_flit.segment_headers.begin(), kDlSegmentCount, crc_buffer.begin() + 3);
  std::copy_n(nak_flit.payload.begin(), kDlPayloadBytes, crc_buffer.begin() + 3 + kDlSegmentCount);

  assert(verify_crc32(crc_buffer, nak_flit.crc));

  std::cout << "PASS\n";
}

// Test command processor with ACK
void test_command_processor_ack() {
  std::cout << "test_command_processor_ack: ";

  DlCommandProcessor processor;

  std::uint16_t received_ack_seq = 0;
  bool ack_callback_called = false;

  processor.set_ack_callback([&](std::uint16_t ack_seq) {
    received_ack_seq = ack_seq;
    ack_callback_called = true;
  });

  // Create an ACK command
  const std::uint16_t expected_ack_seq = 42;
  const DlFlit ack_flit = CommandFactory::create_ack(expected_ack_seq, 0);

  // Process the flit
  const bool was_command = processor.process_flit(ack_flit);

  // Verify results
  assert(was_command);
  assert(ack_callback_called);
  assert(received_ack_seq == expected_ack_seq);

  // Verify stats
  const auto stats = processor.get_stats();
  assert(stats.acks_received == 1);
  assert(stats.naks_received == 0);

  std::cout << "PASS\n";
}

// Test command processor with NAK
void test_command_processor_nak() {
  std::cout << "test_command_processor_nak: ";

  DlCommandProcessor processor;

  std::uint16_t received_nak_seq = 0;
  bool nak_callback_called = false;

  processor.set_nak_callback([&](std::uint16_t nak_seq) {
    received_nak_seq = nak_seq;
    nak_callback_called = true;
  });

  // Create a NAK command
  const std::uint16_t expected_nak_seq = 100;
  const DlFlit nak_flit = CommandFactory::create_nak(expected_nak_seq, 0);

  // Process the flit
  const bool was_command = processor.process_flit(nak_flit);

  // Verify results
  assert(was_command);
  assert(nak_callback_called);
  assert(received_nak_seq == expected_nak_seq);

  // Verify stats
  const auto stats = processor.get_stats();
  assert(stats.acks_received == 0);
  assert(stats.naks_received == 1);

  std::cout << "PASS\n";
}

// Test command processor with explicit (non-command) flit
void test_command_processor_explicit_flit() {
  std::cout << "test_command_processor_explicit_flit: ";

  DlCommandProcessor processor;

  bool callback_called = false;
  processor.set_ack_callback([&](std::uint16_t) { callback_called = true; });
  processor.set_nak_callback([&](std::uint16_t) { callback_called = true; });

  // Create an explicit flit (op = 0)
  DlFlit explicit_flit{};
  ExplicitFlitHeaderFields header{};
  header.op = 0; // Explicit
  header.payload = true;
  header.flit_seq_no = 10;
  explicit_flit.flit_header = serialize_explicit_flit_header(header);

  // Process the flit
  const bool was_command = processor.process_flit(explicit_flit);

  // Verify results - should not be treated as command
  assert(!was_command);
  assert(!callback_called);

  std::cout << "PASS\n";
}

// Test ACK/NAK manager - expected sequence
void test_ack_nak_manager_expected_sequence() {
  std::cout << "test_ack_nak_manager_expected_sequence: ";

  DlAckNakManager manager;
  manager.set_ack_every_n_flits(0); // ACK immediately

  // Process expected sequence number
  const std::uint16_t expected_seq = 0;
  const std::uint8_t our_tx_seq_lo = 3;

  const auto command_flit = manager.process_received_flit(expected_seq, our_tx_seq_lo);

  // Should generate ACK
  assert(command_flit.has_value());

  // Deserialize and verify it's an ACK
  const CommandFlitHeaderFields header = deserialize_command_flit_header(command_flit->flit_header);
  assert(header.op == static_cast<std::uint8_t>(DlCommandOp::kAck));
  assert(header.ack_req_seq == expected_seq);
  assert(header.flit_seq_lo == our_tx_seq_lo);

  std::cout << "PASS\n";
}

// Test ACK/NAK manager - out of order sequence (NAK)
void test_ack_nak_manager_out_of_order() {
  std::cout << "test_ack_nak_manager_out_of_order: ";

  DlAckNakManager manager;

  // Expecting sequence 0, but receive sequence 5 (out of order)
  const std::uint16_t received_seq = 5;
  const std::uint8_t our_tx_seq_lo = 2;

  const auto command_flit = manager.process_received_flit(received_seq, our_tx_seq_lo);

  // Should generate NAK for expected sequence (0)
  assert(command_flit.has_value());

  const CommandFlitHeaderFields header = deserialize_command_flit_header(command_flit->flit_header);
  assert(header.op == static_cast<std::uint8_t>(DlCommandOp::kNak));
  assert(header.ack_req_seq == 0); // NAK for expected sequence
  assert(header.flit_seq_lo == our_tx_seq_lo);

  std::cout << "PASS\n";
}

// Test ACK/NAK manager - duplicate sequence
void test_ack_nak_manager_duplicate() {
  std::cout << "test_ack_nak_manager_duplicate: ";

  DlAckNakManager manager;
  manager.set_ack_every_n_flits(0);

  // Process sequence 0
  auto command_flit = manager.process_received_flit(0, 0);
  assert(command_flit.has_value()); // ACK generated

  // Process sequence 0 again (duplicate)
  command_flit = manager.process_received_flit(0, 0);

  // Should not generate any command for duplicate
  assert(!command_flit.has_value());

  std::cout << "PASS\n";
}

// Test ACK/NAK manager - ACK every N flits
void test_ack_nak_manager_ack_every_n() {
  std::cout << "test_ack_nak_manager_ack_every_n: ";

  DlAckNakManager manager;
  manager.set_ack_every_n_flits(3); // ACK every 3 flits

  // Process sequences 0, 1, 2 - should only ACK on 2
  auto command_flit = manager.process_received_flit(0, 0);
  assert(!command_flit.has_value()); // No ACK yet

  command_flit = manager.process_received_flit(1, 0);
  assert(!command_flit.has_value()); // No ACK yet

  command_flit = manager.process_received_flit(2, 0);
  assert(command_flit.has_value()); // ACK on 3rd flit

  const CommandFlitHeaderFields header = deserialize_command_flit_header(command_flit->flit_header);
  assert(header.op == static_cast<std::uint8_t>(DlCommandOp::kAck));
  assert(header.ack_req_seq == 2);

  std::cout << "PASS\n";
}

// Test deserialize command op
void test_deserialize_command_op() {
  std::cout << "test_deserialize_command_op: ";

  // Test ACK
  DlFlit ack_flit = CommandFactory::create_ack(10, 0);
  DlCommandOp op = DlCommandProcessor::deserialize_command_op(ack_flit.flit_header);
  assert(op == DlCommandOp::kAck);

  // Test NAK
  DlFlit nak_flit = CommandFactory::create_nak(20, 0);
  op = DlCommandProcessor::deserialize_command_op(nak_flit.flit_header);
  assert(op == DlCommandOp::kNak);

  // Test Explicit
  DlFlit explicit_flit{};
  ExplicitFlitHeaderFields header{};
  header.op = 0;
  explicit_flit.flit_header = serialize_explicit_flit_header(header);
  op = DlCommandProcessor::deserialize_command_op(explicit_flit.flit_header);
  assert(op == DlCommandOp::kExplicit);

  std::cout << "PASS\n";
}

// Test deserialize ACK/REQ sequence
void test_deserialize_ack_req_seq() {
  std::cout << "test_deserialize_ack_req_seq: ";

  const std::uint16_t expected_seq = 0x155;
  const DlFlit ack_flit = CommandFactory::create_ack(expected_seq, 0);

  const std::uint16_t extracted_seq = DlCommandProcessor::deserialize_ack_req_seq(ack_flit);
  assert(extracted_seq == expected_seq);

  std::cout << "PASS\n";
}

// Test command processor callback management
void test_command_processor_callbacks() {
  std::cout << "test_command_processor_callbacks: ";

  DlCommandProcessor processor;

  // Initially no callbacks
  assert(!processor.has_ack_callback());
  assert(!processor.has_nak_callback());

  // Set callbacks
  processor.set_ack_callback([](std::uint16_t) {});
  processor.set_nak_callback([](std::uint16_t) {});

  assert(processor.has_ack_callback());
  assert(processor.has_nak_callback());

  // Clear callbacks
  processor.clear_callbacks();

  assert(!processor.has_ack_callback());
  assert(!processor.has_nak_callback());

  std::cout << "PASS\n";
}

int main() {
  std::cout << "\n=== DL Command Flit Tests ===\n\n";

  // Command factory tests
  test_ack_creation();
  test_nak_creation();

  // Command processor tests
  test_command_processor_ack();
  test_command_processor_nak();
  test_command_processor_explicit_flit();
  test_command_processor_callbacks();

  // ACK/NAK manager tests
  test_ack_nak_manager_expected_sequence();
  test_ack_nak_manager_out_of_order();
  test_ack_nak_manager_duplicate();
  test_ack_nak_manager_ack_every_n();

  // Utility function tests
  test_deserialize_command_op();
  test_deserialize_ack_req_seq();

  std::cout << "\n=== All DL Command Tests Passed ===\n";
  return 0;
}
