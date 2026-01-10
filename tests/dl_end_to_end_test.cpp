#include "ualink/dl_flit.h"
#include "ualink/dl_message_processor.h"
#include "ualink/dl_message_queue.h"
#include "ualink/dl_messages.h"
#include "ualink/trace.h"

#include <cassert>
#include <iostream>

using namespace ualink::dl;

// Test complete flow: Enqueue → Serialize → Deserialize → Process
static void test_end_to_end_noop_message() {
  UALINK_TRACE_SCOPED(__func__);

  // 1. Create message and enqueue
  DlMessageQueue queue;
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  queue.enqueue(nop);

  // 2. Serialize into DL flit
  std::vector<TlFlit> tl_flits; // Empty for this test
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 1;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);
  assert(!queue.has_pending_messages()); // Message consumed

  // 3. Deserialize DL flit
  auto result = DlDeserializer::deserialize_ex(dl_flit);
  assert(result.dl_message_dwords.size() == 1);

  // 4. Process message with DlMessageProcessor
  DlMessageProcessor processor;
  bool callback_called = false;

  processor.set_noop_callback([&](const NoOpMessage& msg) {
    callback_called = true;
    assert(msg.common.mclass == 0b00);
    assert(msg.common.mtype == 0b000);
  });

  bool processed = processor.process_dword(result.dl_message_dwords[0], 0);
  assert(processed);
  assert(callback_called);

  const auto stats = processor.get_stats();
  assert(stats.basic_received == 1);
  assert(stats.deserialization_errors == 0);

  std::cout << "test_end_to_end_noop_message: PASS\n";
}

static void test_end_to_end_tl_rate_with_timeout() {
  UALINK_TRACE_SCOPED(__func__);

  // 1. Enqueue TL Rate Notification (request)
  DlMessageQueue queue;
  TlRateNotification request{};
  request.rate = 0x5678;
  request.ack = false; // This is a request
  request.common = make_common(DlBasicMessageType::kTlRateNotification);
  queue.enqueue(request);

  // 2. Serialize
  std::vector<TlFlit> tl_flits;
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 10;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // 3. Deserialize
  auto result = DlDeserializer::deserialize_ex(dl_flit);
  assert(result.dl_message_dwords.size() == 1);

  // 4. Process with timeout tracking
  DlMessageProcessor processor;
  std::uint16_t received_rate = 0;

  processor.set_tl_rate_callback([&](const TlRateNotification& msg) {
    received_rate = msg.rate;
  });

  // Start timeout for this request
  processor.start_basic_timeout(42, 0);

  // Process request
  processor.process_dword(result.dl_message_dwords[0], 0);
  assert(received_rate == 0x5678);

  // Check timeout hasn't expired yet (at time 0, timeout 1µs)
  auto timeout_result = processor.check_basic_timeout(0, 1);
  assert(timeout_result == TimeoutResult::kNoTimeout);

  // Now simulate response
  TlRateNotification response{};
  response.rate = 0x5678;
  response.ack = true; // This is a response
  response.common = make_common(DlBasicMessageType::kTlRateNotification);

  auto response_dword = serialize_tl_rate_notification(response);
  processor.process_dword(response_dword, 0);

  // Timeout should be cancelled now
  timeout_result = processor.check_basic_timeout(1000, 1);
  assert(timeout_result == TimeoutResult::kNoTimeout);

  std::cout << "test_end_to_end_tl_rate_with_timeout: PASS\n";
}

static void test_end_to_end_channel_negotiation() {
  UALINK_TRACE_SCOPED(__func__);

  // 1. Enqueue Channel Negotiation Request
  DlMessageQueue queue;
  ChannelNegotiation request{};
  request.channel_command = 0b0000; // Request
  request.channel_response = 0;
  request.channel_target = 0;
  request.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  queue.enqueue(request);

  // 2. Serialize
  std::vector<TlFlit> tl_flits;
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 20;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // 3. Deserialize
  auto result = DlDeserializer::deserialize_ex(dl_flit);
  assert(result.dl_message_dwords.size() == 1);

  // 4. Process and verify state machine transition
  DlMessageProcessor processor;
  std::uint8_t received_command = 0xFF;

  processor.set_control_callback([&](const ChannelNegotiation& msg) {
    received_command = msg.channel_command;
  });

  // Initial state
  assert(processor.get_channel_state() == ChannelState::kOffline);

  // Process request
  processor.process_dword(result.dl_message_dwords[0], 0);
  assert(received_command == 0b0000);
  assert(processor.get_channel_state() == ChannelState::kRequestSent);

  // Now send Ack
  ChannelNegotiation ack{};
  ack.channel_command = 0b0001; // Ack
  ack.channel_response = 0;
  ack.channel_target = 0;
  ack.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);

  auto ack_dword = serialize_channel_negotiation(ack);
  processor.process_dword(ack_dword, 1);
  assert(received_command == 0b0001);
  assert(processor.get_channel_state() == ChannelState::kOnline);

  std::cout << "test_end_to_end_channel_negotiation: PASS\n";
}

static void test_end_to_end_multiple_message_types() {
  UALINK_TRACE_SCOPED(__func__);

  // 1. Enqueue multiple messages of different types
  DlMessageQueue queue;

  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  queue.enqueue(nop);

  DeviceIdMessage device_id{};
  device_id.id = 0x123;
  device_id.type = 0x2;
  device_id.valid = true;
  device_id.ack = false;
  device_id.common = make_common(DlBasicMessageType::kDeviceIdRequest);
  queue.enqueue(device_id);

  ChannelNegotiation channel{};
  channel.channel_command = 0b0011; // Pending
  channel.channel_response = 0;
  channel.channel_target = 0;
  channel.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  queue.enqueue(channel);

  // 2. Serialize all messages
  std::vector<TlFlit> tl_flits;
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 30;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // 3. Deserialize
  auto result = DlDeserializer::deserialize_ex(dl_flit);
  assert(result.dl_message_dwords.size() == 3);

  // 4. Process all messages
  DlMessageProcessor processor;
  int noop_count = 0;
  int device_id_count = 0;
  int channel_count = 0;

  processor.set_noop_callback([&](const NoOpMessage&) { noop_count++; });
  processor.set_device_id_callback([&](const DeviceIdMessage& msg) {
    device_id_count++;
    assert(msg.id == 0x123);
  });
  processor.set_control_callback([&](const ChannelNegotiation& msg) {
    channel_count++;
    assert(msg.channel_command == 0b0011);
  });

  for (const auto& dword : result.dl_message_dwords) {
    bool processed = processor.process_dword(dword, 0);
    assert(processed);
  }

  assert(noop_count == 1);
  assert(device_id_count == 1);
  assert(channel_count == 1);

  const auto stats = processor.get_stats();
  assert(stats.basic_received == 2); // NoOp + DeviceId
  assert(stats.control_received == 1); // Channel
  assert(stats.deserialization_errors == 0);

  std::cout << "test_end_to_end_multiple_message_types: PASS\n";
}

static void test_end_to_end_uart_messages() {
  UALINK_TRACE_SCOPED(__func__);

  // 1. Enqueue UART messages
  DlMessageQueue queue;

  UartStreamResetRequest reset_req{};
  reset_req.stream_id = 0; // Must be 0
  reset_req.all_streams = false;
  reset_req.common = make_common(DlUartMessageType::kStreamResetRequest);
  queue.enqueue(reset_req);

  UartStreamCreditUpdate credit{};
  credit.stream_id = 0; // Must be 0
  credit.data_fc_seq = 0x1FF;
  credit.common = make_common(DlUartMessageType::kStreamCreditUpdate);
  queue.enqueue(credit);

  // 2. Serialize
  std::vector<TlFlit> tl_flits;
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 40;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // 3. Deserialize
  auto result = DlDeserializer::deserialize_ex(dl_flit);
  assert(result.dl_message_dwords.size() == 2);

  // 4. Process
  DlMessageProcessor processor;
  bool reset_called = false;
  bool credit_called = false;
  std::uint16_t received_credits = 0;

  processor.set_uart_reset_req_callback([&](const UartStreamResetRequest&) {
    reset_called = true;
  });

  processor.set_uart_credit_callback([&](const UartStreamCreditUpdate& msg) {
    credit_called = true;
    received_credits = msg.data_fc_seq;
  });

  for (const auto& dword : result.dl_message_dwords) {
    bool processed = processor.process_dword(dword, 0);
    assert(processed);
  }

  assert(reset_called);
  assert(credit_called);
  assert(received_credits == 0x1FF);

  const auto stats = processor.get_stats();
  assert(stats.uart_received == 2);

  std::cout << "test_end_to_end_uart_messages: PASS\n";
}

static void test_end_to_end_with_tl_flits() {
  UALINK_TRACE_SCOPED(__func__);

  // 1. Enqueue DL message
  DlMessageQueue queue;
  PortIdMessage port{};
  port.port_number = 0xABC;
  port.valid = true;
  port.ack = false;
  port.common = make_common(DlBasicMessageType::kPortNumberRequestResponse);
  queue.enqueue(port);

  // 2. Create TL flits
  std::vector<TlFlit> tl_flits;
  TlFlit flit{};
  for (std::size_t i = 0; i < kTlFlitBytes; ++i) {
    flit.data[i] = static_cast<std::byte>(0xAA);
  }
  flit.message_field = 0x1;
  tl_flits.push_back(flit);

  // 3. Serialize (both DL message and TL flit)
  ExplicitFlitHeaderFields header{};
  header.op = 0x1;
  header.payload = true;
  header.flit_seq_no = 50;

  DlFlit dl_flit = DlSerializer::serialize(tl_flits, header, &queue);

  // 4. Deserialize
  auto result = DlDeserializer::deserialize_ex(dl_flit);
  assert(result.dl_message_dwords.size() == 1);
  assert(result.tl_flits.size() == 1);

  // Verify TL flit
  for (std::size_t i = 0; i < kTlFlitBytes; ++i) {
    assert(result.tl_flits[0].data[i] == std::byte{0xAA});
  }
  assert(result.tl_flits[0].message_field == 0x1);

  // 5. Process DL message
  DlMessageProcessor processor;
  std::uint16_t received_port = 0;

  processor.set_port_id_callback([&](const PortIdMessage& msg) {
    received_port = msg.port_number;
  });

  processor.process_dword(result.dl_message_dwords[0], 0);
  assert(received_port == 0xABC);

  std::cout << "test_end_to_end_with_tl_flits: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_end_to_end_noop_message();
  test_end_to_end_tl_rate_with_timeout();
  test_end_to_end_channel_negotiation();
  test_end_to_end_multiple_message_types();
  test_end_to_end_uart_messages();
  test_end_to_end_with_tl_flits();

  std::cout << "\nAll DL end-to-end tests passed!\n";
  return 0;
}
