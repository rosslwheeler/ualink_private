#include "ualink/dl_flit.h"
#include "ualink/dl_message_queue.h"
#include "ualink/dl_messages.h"
#include "ualink/trace.h"

#include <cassert>
#include <iostream>

using namespace ualink::dl;

static void test_roundtrip_single_dl_message() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Create a TL Rate Notification message
  TlRateNotification msg{};
  msg.rate = 0x1234;
  msg.ack = true;
  msg.common = make_common(DlBasicMessageType::kTlRateNotification);
  queue.enqueue(msg);

  // Create some TL flits
  std::vector<TlFlit> tl_flits;
  for (int i = 0; i < 3; ++i) {
    TlFlit flit{};
    for (std::size_t j = 0; j < kTlFlitBytes; ++j) {
      flit.data[j] = static_cast<std::byte>(i * 100 + j);
    }
    flit.message_field = static_cast<std::uint8_t>(i % 4);
    tl_flits.push_back(flit);
  }

  // Serialize with DL message queue
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 42;

  std::size_t flits_serialized = 0;
  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue, &flits_serialized);

  assert(flits_serialized == 3);

  // Deserialize and verify
  auto result = DlDeserializer::deserialize_ex(dl_flit);

  // Should have extracted 1 DL message DWord
  assert(result.dl_message_dwords.size() == 1);

  // Verify the TL flits
  assert(result.tl_flits.size() == 3);
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < kTlFlitBytes; ++j) {
      assert(result.tl_flits[i].data[j] == tl_flits[i].data[j]);
    }
    assert(result.tl_flits[i].message_field == tl_flits[i].message_field);
  }

  // Verify DL message can be deserialized
  auto deserialized_msg = deserialize_tl_rate_notification(result.dl_message_dwords[0]);
  assert(deserialized_msg.has_value());
  assert(deserialized_msg->rate == msg.rate);
  assert(deserialized_msg->ack == msg.ack);

  // Verify message was consumed from queue
  assert(!queue.has_pending_messages());

  std::cout << "test_roundtrip_single_dl_message: PASS\n";
}

static void test_roundtrip_multiple_dl_messages() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Enqueue 5 DL messages (one per segment)
  for (int i = 0; i < 5; ++i) {
    NoOpMessage nop{};
    nop.common = make_common(DlBasicMessageType::kNoOp);
    queue.enqueue(nop);
  }

  // Create TL flits
  std::vector<TlFlit> tl_flits;
  for (int i = 0; i < 2; ++i) {
    TlFlit flit{};
    for (std::size_t j = 0; j < kTlFlitBytes; ++j) {
      flit.data[j] = static_cast<std::byte>(i * 10 + j);
    }
    tl_flits.push_back(flit);
  }

  // Serialize
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 100;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // Deserialize
  auto result = DlDeserializer::deserialize_ex(dl_flit);

  // Should have extracted 5 DL message DWords (one per segment)
  assert(result.dl_message_dwords.size() == 5);

  // Verify TL flits
  assert(result.tl_flits.size() == 2);

  // Verify queue is empty
  assert(!queue.has_pending_messages());

  std::cout << "test_roundtrip_multiple_dl_messages: PASS\n";
}

static void test_roundtrip_no_dl_messages() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue; // Empty queue

  // Create TL flits
  std::vector<TlFlit> tl_flits;
  for (int i = 0; i < 5; ++i) {
    TlFlit flit{};
    for (std::size_t j = 0; j < kTlFlitBytes; ++j) {
      flit.data[j] = static_cast<std::byte>(i + j);
    }
    tl_flits.push_back(flit);
  }

  // Serialize with empty queue
  ExplicitFlitHeaderFields header{};
  header.op = 0x2;
  header.payload = true;
  header.flit_seq_no = 200;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // Deserialize
  auto result = DlDeserializer::deserialize_ex(dl_flit);

  // Should have no DL messages
  assert(result.dl_message_dwords.empty());

  // Should have all TL flits
  assert(result.tl_flits.size() == 5);

  for (std::size_t i = 0; i < 5; ++i) {
    for (std::size_t j = 0; j < kTlFlitBytes; ++j) {
      assert(result.tl_flits[i].data[j] == tl_flits[i].data[j]);
    }
  }

  std::cout << "test_roundtrip_no_dl_messages: PASS\n";
}

static void test_roundtrip_with_crc_check() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Add a Device ID message
  DeviceIdMessage msg{};
  msg.id = 0x123;  // Must fit in 10 bits (max 0x3FF)
  msg.type = 0x2;  // Must fit in 2 bits
  msg.valid = true;
  msg.common = make_common(DlBasicMessageType::kDeviceIdRequest);
  queue.enqueue(msg);

  // Create TL flits
  std::vector<TlFlit> tl_flits;
  TlFlit flit{};
  for (std::size_t j = 0; j < kTlFlitBytes; ++j) {
    flit.data[j] = static_cast<std::byte>(j * 2);
  }
  tl_flits.push_back(flit);

  // Serialize
  ExplicitFlitHeaderFields header{};
  header.op = 0x3;
  header.payload = true;
  header.flit_seq_no = 99;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // Deserialize with CRC check
  auto result_opt = DlDeserializer::deserialize_ex_with_crc_check(dl_flit);

  assert(result_opt.has_value());
  assert(result_opt->dl_message_dwords.size() == 1);
  assert(result_opt->tl_flits.size() == 1);

  // Verify Device ID message
  auto deserialized_msg = deserialize_device_id_message(result_opt->dl_message_dwords[0]);
  assert(deserialized_msg.has_value());
  assert(deserialized_msg->id == msg.id);

  std::cout << "test_roundtrip_with_crc_check: PASS\n";
}

static void test_roundtrip_with_corrupted_crc() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Add message
  PortIdMessage msg{};
  msg.port_number = 0x456;  // Must fit in 12 bits (max 0xFFF)
  msg.valid = true;
  msg.common = make_common(DlBasicMessageType::kPortNumberRequestResponse);
  queue.enqueue(msg);

  // Create TL flits
  std::vector<TlFlit> tl_flits;
  TlFlit flit{};
  tl_flits.push_back(flit);

  // Serialize
  ExplicitFlitHeaderFields header{};
  header.op = 0x4;
  header.payload = true;
  header.flit_seq_no = 123;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // Corrupt the CRC
  dl_flit.crc[0] = static_cast<std::byte>(0xFF);
  dl_flit.crc[1] = static_cast<std::byte>(0xFF);

  // Deserialize with CRC check should fail
  auto result_opt = DlDeserializer::deserialize_ex_with_crc_check(dl_flit);

  assert(!result_opt.has_value());

  std::cout << "test_roundtrip_with_corrupted_crc: PASS\n";
}

static void test_dl_message_priority_placement() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Enqueue a NoOp message
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  queue.enqueue(nop);

  // Create enough TL flits to fill segment 0
  std::vector<TlFlit> tl_flits;
  for (int i = 0; i < 2; ++i) { // 2 TL flits = 128 bytes
    TlFlit flit{};
    for (std::size_t j = 0; j < kTlFlitBytes; ++j) {
      flit.data[j] = static_cast<std::byte>(0xAA + i + j);
    }
    tl_flits.push_back(flit);
  }

  // Serialize
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 50;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // Manually verify DL message is at start of segment 0
  // Segment 0 starts at offset 0
  auto result = DlDeserializer::deserialize_ex(dl_flit);

  // Should have 1 DL message
  assert(result.dl_message_dwords.size() == 1);

  // Should have only 1 TL flit (2nd one doesn't fit after DL message in segment 0)
  // Segment 0: DL message (4 bytes) + TL flit (64 bytes) = 68 bytes < 128 bytes
  // Actually, segment 0 has 128 bytes, so after DL message (4 bytes), we have 124 bytes
  // That's enough for 1 TL flit (64 bytes), not 2
  assert(result.tl_flits.size() == 1);

  std::cout << "test_dl_message_priority_placement: PASS\n";
}

static void test_uart_stream_transport_multi_flit() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Create UART Stream Transport with 3 payload DWords
  UartStreamTransportMessage uart{};
  uart.stream_id = 0;
  uart.common = make_common(DlUartMessageType::kStreamTransportMessage);
  uart.payload_dwords = {0x11111111U, 0x22222222U, 0x33333333U};
  queue.enqueue(uart);

  // Serialize first flit (should pack header + first payload DWord)
  std::vector<TlFlit> tl_flits; // No TL flits
  ExplicitFlitHeaderFields header1{};
  header1.op = 0x1;
  header1.payload = true;
  header1.flit_seq_no = 1;

  DlFlit dl_flit1 = DlSerializer::serialize(tl_flits, header1, &queue);

  // Deserialize first flit
  auto result1 = DlDeserializer::deserialize_ex(dl_flit1);
  // Should have 2 DWords (header + payload[0]) in first 2 segments
  assert(result1.dl_message_dwords.size() == 2);

  // Serialize second flit (should pack payload[1] + payload[2])
  ExplicitFlitHeaderFields header2{};
  header2.op = 0x1;
  header2.payload = true;
  header2.flit_seq_no = 2;

  DlFlit dl_flit2 = DlSerializer::serialize(tl_flits, header2, &queue);

  // Deserialize second flit
  auto result2 = DlDeserializer::deserialize_ex(dl_flit2);
  // Should have 2 DWords (payload[1] + payload[2])
  assert(result2.dl_message_dwords.size() == 2);

  // Queue should be empty now
  assert(!queue.has_pending_messages());

  std::cout << "test_uart_stream_transport_multi_flit: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_roundtrip_single_dl_message();
  test_roundtrip_multiple_dl_messages();
  test_roundtrip_no_dl_messages();
  test_roundtrip_with_crc_check();
  test_roundtrip_with_corrupted_crc();
  test_dl_message_priority_placement();
  test_uart_stream_transport_multi_flit();

  std::cout << "\nAll DL flit message integration tests passed!\n";
  return 0;
}