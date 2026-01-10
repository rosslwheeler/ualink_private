#include "ualink/dl_message_processor.h"
#include "ualink/trace.h"

#include <cassert>
#include <iostream>

using namespace ualink::dl;

static void test_process_noop_message() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  bool callback_called = false;

  processor.set_noop_callback([&callback_called](const NoOpMessage& msg) {
    callback_called = true;
    assert(msg.common.mclass == 0b00);
    assert(msg.common.mtype == 0b000);
  });

  // Create and serialize a NoOp message
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  auto dword = serialize_no_op_message(nop);

  // Process the DWord
  bool result = processor.process_dword(dword, 0);
  assert(result);
  assert(callback_called);

  const auto stats = processor.get_stats();
  assert(stats.basic_received == 1);
  assert(stats.deserialization_errors == 0);

  std::cout << "test_process_noop_message: PASS\n";
}

static void test_process_tl_rate_notification() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  bool callback_called = false;
  std::uint16_t received_rate = 0;

  processor.set_tl_rate_callback([&](const TlRateNotification& msg) {
    callback_called = true;
    received_rate = msg.rate;
  });

  // Create TL Rate Notification
  TlRateNotification msg{};
  msg.rate = 0x1234;
  msg.ack = false;
  msg.common = make_common(DlBasicMessageType::kTlRateNotification);
  auto dword = serialize_tl_rate_notification(msg);

  // Process
  bool result = processor.process_dword(dword, 0);
  assert(result);
  assert(callback_called);
  assert(received_rate == 0x1234);

  std::cout << "test_process_tl_rate_notification: PASS\n";
}

static void test_process_device_id_message() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  bool callback_called = false;
  std::uint16_t received_id = 0;

  processor.set_device_id_callback([&](const DeviceIdMessage& msg) {
    callback_called = true;
    received_id = msg.id;
  });

  // Create Device ID message
  DeviceIdMessage msg{};
  msg.id = 0x2AB;  // 10 bits
  msg.type = 0x1;  // 2 bits
  msg.valid = true;
  msg.ack = false;
  msg.common = make_common(DlBasicMessageType::kDeviceIdRequest);
  auto dword = serialize_device_id_message(msg);

  // Process
  bool result = processor.process_dword(dword, 100);
  assert(result);
  assert(callback_called);
  assert(received_id == 0x2AB);

  std::cout << "test_process_device_id_message: PASS\n";
}

static void test_basic_timeout_tracking() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;

  // Start timeout at time 0
  processor.start_basic_timeout(42, 0);

  // Check at time 0 - should not timeout
  auto result = processor.check_basic_timeout(0, 1);
  assert(result == TimeoutResult::kNoTimeout);

  // Check at time 1 (exactly at timeout boundary) - should timeout
  result = processor.check_basic_timeout(1, 1);
  assert(result == TimeoutResult::kTimeoutExpired);

  const auto stats = processor.get_stats();
  assert(stats.timeouts == 1);

  std::cout << "test_basic_timeout_tracking: PASS\n";
}

static void test_basic_timeout_cancellation() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;

  // Set up callback to cancel timeout when response received
  processor.set_tl_rate_callback([](const TlRateNotification&) {
    // Timeout cancellation happens automatically in processor
  });

  // Start timeout
  processor.start_basic_timeout(1, 0);

  // Send response (ack=true triggers timeout cancellation)
  TlRateNotification response{};
  response.rate = 0x100;
  response.ack = true;
  response.common = make_common(DlBasicMessageType::kTlRateNotification);
  auto dword = serialize_tl_rate_notification(response);

  processor.process_dword(dword, 0);

  // Check timeout - should be cancelled
  auto result = processor.check_basic_timeout(1000, 1);
  assert(result == TimeoutResult::kNoTimeout);

  const auto stats = processor.get_stats();
  assert(stats.timeouts == 0);

  std::cout << "test_basic_timeout_cancellation: PASS\n";
}

static void test_channel_negotiation_state_machine() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  std::uint8_t last_command = 0xFF;

  processor.set_control_callback([&](const ChannelNegotiation& msg) {
    last_command = msg.channel_command;
  });

  // Initial state should be offline
  assert(processor.get_channel_state() == ChannelState::kOffline);

  // Receive Request command
  ChannelNegotiation request{};
  request.channel_command = 0b0000;  // Request
  request.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  auto dword1 = serialize_channel_negotiation(request);

  processor.process_dword(dword1, 0);
  assert(last_command == 0b0000);
  assert(processor.get_channel_state() == ChannelState::kRequestSent);

  // Receive Ack command
  ChannelNegotiation ack{};
  ack.channel_command = 0b0001;  // Ack
  ack.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  auto dword2 = serialize_channel_negotiation(ack);

  processor.process_dword(dword2, 1);
  assert(last_command == 0b0001);
  assert(processor.get_channel_state() == ChannelState::kOnline);

  const auto stats = processor.get_stats();
  assert(stats.control_received == 2);

  std::cout << "test_channel_negotiation_state_machine: PASS\n";
}

static void test_channel_negotiation_nack() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;

  // Transition to RequestSent state
  processor.transition_channel_state(ChannelState::kRequestSent, 0);

  // Receive NAck command
  ChannelNegotiation nack{};
  nack.channel_command = 0b0010;  // NAck
  nack.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  auto dword = serialize_channel_negotiation(nack);

  processor.process_dword(dword, 1);

  // Should transition back to offline
  assert(processor.get_channel_state() == ChannelState::kOffline);

  std::cout << "test_channel_negotiation_nack: PASS\n";
}

static void test_uart_reset_messages() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  bool req_callback_called = false;
  bool rsp_callback_called = false;

  processor.set_uart_reset_req_callback([&](const UartStreamResetRequest&) {
    req_callback_called = true;
  });

  processor.set_uart_reset_rsp_callback([&](const UartStreamResetResponse&) {
    rsp_callback_called = true;
  });

  // Send reset request
  UartStreamResetRequest req{};
  req.stream_id = 0;
  req.common = make_common(DlUartMessageType::kStreamResetRequest);
  auto dword1 = serialize_uart_stream_reset_request(req);

  processor.process_dword(dword1, 0);
  assert(req_callback_called);

  // Send reset response
  UartStreamResetResponse rsp{};
  rsp.stream_id = 0;
  rsp.common = make_common(DlUartMessageType::kStreamResetResponse);
  auto dword2 = serialize_uart_stream_reset_response(rsp);

  processor.process_dword(dword2, 1);
  assert(rsp_callback_called);

  const auto stats = processor.get_stats();
  assert(stats.uart_received == 2);

  std::cout << "test_uart_reset_messages: PASS\n";
}

static void test_uart_credit_update() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  bool callback_called = false;
  std::uint16_t received_data_fc_seq = 0;

  processor.set_uart_credit_callback([&](const UartStreamCreditUpdate& msg) {
    callback_called = true;
    received_data_fc_seq = msg.data_fc_seq;
  });

  // Send credit update
  UartStreamCreditUpdate msg{};
  msg.stream_id = 0;
  msg.data_fc_seq = 0x1FF;  // 9 bits
  msg.common = make_common(DlUartMessageType::kStreamCreditUpdate);
  auto dword = serialize_uart_stream_credit_update(msg);

  processor.process_dword(dword, 0);
  assert(callback_called);
  assert(received_data_fc_seq == 0x1FF);

  std::cout << "test_uart_credit_update: PASS\n";
}

static void test_invalid_message_class() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;

  // Create DWord with reserved mclass (0b11)
  std::array<std::byte, 4> invalid_dword{};
  invalid_dword[0] = std::byte{0b11000000};  // mclass=0b11 (reserved)

  // Process should fail
  bool result = processor.process_dword(invalid_dword, 0);
  assert(!result);

  const auto stats = processor.get_stats();
  assert(stats.deserialization_errors == 1);

  std::cout << "test_invalid_message_class: PASS\n";
}

static void test_stats_reset() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;

  // Process some messages
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  auto dword = serialize_no_op_message(nop);

  processor.process_dword(dword, 0);
  processor.process_dword(dword, 1);

  auto stats = processor.get_stats();
  assert(stats.basic_received == 2);

  // Reset stats
  processor.reset_stats();

  stats = processor.get_stats();
  assert(stats.basic_received == 0);
  assert(stats.control_received == 0);
  assert(stats.uart_received == 0);
  assert(stats.deserialization_errors == 0);
  assert(stats.timeouts == 0);

  std::cout << "test_stats_reset: PASS\n";
}

static void test_port_id_message() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  bool callback_called = false;
  std::uint16_t received_port = 0;

  processor.set_port_id_callback([&](const PortIdMessage& msg) {
    callback_called = true;
    received_port = msg.port_number;
  });

  // Create Port ID message
  PortIdMessage msg{};
  msg.port_number = 0x789;  // 12 bits
  msg.valid = true;
  msg.ack = false;
  msg.common = make_common(DlBasicMessageType::kPortNumberRequestResponse);
  auto dword = serialize_port_id_message(msg);

  // Process
  bool result = processor.process_dword(dword, 0);
  assert(result);
  assert(callback_called);
  assert(received_port == 0x789);

  std::cout << "test_port_id_message: PASS\n";
}

static void test_multiple_message_types() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageProcessor processor;
  int basic_count = 0;
  int control_count = 0;
  int uart_count = 0;

  processor.set_noop_callback([&](const NoOpMessage&) { basic_count++; });
  processor.set_control_callback([&](const ChannelNegotiation&) { control_count++; });
  processor.set_uart_credit_callback([&](const UartStreamCreditUpdate&) { uart_count++; });

  // Send one of each type
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  processor.process_dword(serialize_no_op_message(nop), 0);

  ChannelNegotiation cn{};
  cn.channel_command = 0b0011;  // Pending
  cn.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  processor.process_dword(serialize_channel_negotiation(cn), 1);

  UartStreamCreditUpdate uart{};
  uart.stream_id = 0;
  uart.data_fc_seq = 100;
  uart.common = make_common(DlUartMessageType::kStreamCreditUpdate);
  processor.process_dword(serialize_uart_stream_credit_update(uart), 2);

  assert(basic_count == 1);
  assert(control_count == 1);
  assert(uart_count == 1);

  const auto stats = processor.get_stats();
  assert(stats.basic_received == 1);
  assert(stats.control_received == 1);
  assert(stats.uart_received == 1);

  std::cout << "test_multiple_message_types: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_process_noop_message();
  test_process_tl_rate_notification();
  test_process_device_id_message();
  test_basic_timeout_tracking();
  test_basic_timeout_cancellation();
  test_channel_negotiation_state_machine();
  test_channel_negotiation_nack();
  test_uart_reset_messages();
  test_uart_credit_update();
  test_invalid_message_class();
  test_stats_reset();
  test_port_id_message();
  test_multiple_message_types();

  std::cout << "\nAll DlMessageProcessor tests passed!\n";
  return 0;
}
