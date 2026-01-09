#include "ualink/dl_message_queue.h"
#include "ualink/trace.h"

#include <cassert>
#include <iostream>

using namespace ualink::dl;

static void test_enqueue_and_stats() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Enqueue messages from different groups
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  queue.enqueue(nop);

  ChannelNegotiation cn{};
  cn.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  queue.enqueue(cn);

  UartStreamResetRequest uart{};
  uart.common = make_common(DlUartMessageType::kStreamResetRequest);
  queue.enqueue(uart);

  const auto stats = queue.get_stats();
  assert(stats.basic_enqueued == 1);
  assert(stats.control_enqueued == 1);
  assert(stats.uart_enqueued == 1);
  assert(queue.has_pending_messages());

  std::cout << "test_enqueue_and_stats: PASS\n";
}

static void test_round_robin_basic_order() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Enqueue one from each group
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  queue.enqueue(nop);

  ChannelNegotiation cn{};
  cn.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  queue.enqueue(cn);

  UartStreamCreditUpdate uart{};
  uart.common = make_common(DlUartMessageType::kStreamCreditUpdate);
  queue.enqueue(uart);

  // First pop should be Basic (starting from kNone)
  auto dword1 = queue.pop_next_dword();
  assert(dword1.has_value());
  auto stats = queue.get_stats();
  assert(stats.basic_sent == 1);

  // Second pop should be Control (round-robin after Basic)
  auto dword2 = queue.pop_next_dword();
  assert(dword2.has_value());
  stats = queue.get_stats();
  assert(stats.control_sent == 1);

  // Third pop should be UART (round-robin after Control)
  auto dword3 = queue.pop_next_dword();
  assert(dword3.has_value());
  stats = queue.get_stats();
  assert(stats.uart_sent == 1);

  // Queue should be empty
  assert(!queue.has_pending_messages());

  std::cout << "test_round_robin_basic_order: PASS\n";
}

static void test_round_robin_continues_after_wrap() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Add messages: Basic, Control, Uart, then another Basic
  for (int i = 0; i < 2; ++i) {
    NoOpMessage nop{};
    nop.common = make_common(DlBasicMessageType::kNoOp);
    queue.enqueue(nop);
  }

  ChannelNegotiation cn{};
  cn.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  queue.enqueue(cn);

  UartStreamCreditUpdate uart{};
  uart.common = make_common(DlUartMessageType::kStreamCreditUpdate);
  queue.enqueue(uart);

  // Pop 3 times: Basic, Control, Uart
  queue.pop_next_dword();
  queue.pop_next_dword();
  queue.pop_next_dword();

  // Next pop should wrap around to Basic again
  auto dword4 = queue.pop_next_dword();
  assert(dword4.has_value());
  const auto stats = queue.get_stats();
  assert(stats.basic_sent == 2);

  std::cout << "test_round_robin_continues_after_wrap: PASS\n";
}

static void test_empty_queue() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  assert(!queue.has_pending_messages());

  auto dword = queue.pop_next_dword();
  assert(!dword.has_value());

  std::cout << "test_empty_queue: PASS\n";
}

static void test_uart_stream_transport_multi_dword() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Create UART Stream Transport with 3 payload DWords
  UartStreamTransportMessage msg{};
  msg.stream_id = 0x1;
  msg.common = make_common(DlUartMessageType::kStreamTransportMessage);
  msg.payload_dwords = {0x11111111U, 0x22222222U, 0x33333333U};

  queue.enqueue(msg);

  // First pop: header DWord
  auto dword1 = queue.pop_next_dword();
  assert(dword1.has_value());
  assert(queue.has_pending_messages()); // Payload DWords still pending

  // Second pop: first payload DWord (blocking other groups)
  auto dword2 = queue.pop_next_dword();
  assert(dword2.has_value());
  assert(queue.has_pending_messages());

  // Third pop: second payload DWord
  auto dword3 = queue.pop_next_dword();
  assert(dword3.has_value());
  assert(queue.has_pending_messages());

  // Fourth pop: third payload DWord
  auto dword4 = queue.pop_next_dword();
  assert(dword4.has_value());
  assert(!queue.has_pending_messages()); // Now empty

  const auto stats = queue.get_stats();
  assert(stats.uart_sent == 1); // One message sent
  assert(stats.uart_multi_flit_count == 1);

  std::cout << "test_uart_stream_transport_multi_dword: PASS\n";
}

static void test_uart_blocking_other_groups() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Enqueue UART Stream Transport (multi-DWord)
  UartStreamTransportMessage uart{};
  uart.stream_id = 0x2;
  uart.common = make_common(DlUartMessageType::kStreamTransportMessage);
  uart.payload_dwords = {0xAAAAAAAAU, 0xBBBBBBBBU};
  queue.enqueue(uart);

  // Enqueue Basic message (should be blocked during UART transport)
  NoOpMessage nop{};
  nop.common = make_common(DlBasicMessageType::kNoOp);
  queue.enqueue(nop);

  // Enqueue Control message (should also be blocked)
  ChannelNegotiation cn{};
  cn.common = make_common(DlControlMessageType::kChannelOnlineOfflineNegotiation);
  queue.enqueue(cn);

  // Pop 1: UART header
  auto dword1 = queue.pop_next_dword();
  assert(dword1.has_value());

  // Pop 2: UART payload[0] - Basic/Control should still be blocked
  auto dword2 = queue.pop_next_dword();
  assert(dword2.has_value());
  auto stats = queue.get_stats();
  assert(stats.basic_sent == 0); // Not sent yet
  assert(stats.control_sent == 0);

  // Pop 3: UART payload[1] - Last UART DWord
  auto dword3 = queue.pop_next_dword();
  assert(dword3.has_value());

  // Pop 4: Now Basic/Control should be unblocked - round-robin from Basic
  auto dword4 = queue.pop_next_dword();
  assert(dword4.has_value());
  stats = queue.get_stats();
  assert(stats.basic_sent == 1); // Basic sent after UART completes

  // Pop 5: Control
  auto dword5 = queue.pop_next_dword();
  assert(dword5.has_value());
  stats = queue.get_stats();
  assert(stats.control_sent == 1);

  assert(!queue.has_pending_messages());

  std::cout << "test_uart_blocking_other_groups: PASS\n";
}

static void test_serialization_roundtrip() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Enqueue a TL Rate Notification
  TlRateNotification msg{};
  msg.rate = 0x1234;
  msg.ack = true;
  msg.common = make_common(DlBasicMessageType::kTlRateNotification);
  queue.enqueue(msg);

  // Pop and verify serialization
  auto dword = queue.pop_next_dword();
  assert(dword.has_value());

  // Deserialize and verify (basic check: just ensure deserialization succeeds)
  auto deserialized = deserialize_tl_rate_notification(*dword);
  assert(deserialized.has_value());
  assert(deserialized->rate == msg.rate);
  assert(deserialized->ack == msg.ack);

  std::cout << "test_serialization_roundtrip: PASS\n";
}

static void test_multiple_messages_same_group() {
  UALINK_TRACE_SCOPED(__func__);

  DlMessageQueue queue;

  // Enqueue 3 Basic messages
  for (int i = 0; i < 3; ++i) {
    NoOpMessage nop{};
    nop.common = make_common(DlBasicMessageType::kNoOp);
    queue.enqueue(nop);
  }

  // All should be serviced in FIFO order within the Basic group
  for (int i = 0; i < 3; ++i) {
    auto dword = queue.pop_next_dword();
    assert(dword.has_value());
  }

  const auto stats = queue.get_stats();
  assert(stats.basic_sent == 3);
  assert(!queue.has_pending_messages());

  std::cout << "test_multiple_messages_same_group: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_enqueue_and_stats();
  test_round_robin_basic_order();
  test_round_robin_continues_after_wrap();
  test_empty_queue();
  test_uart_stream_transport_multi_dword();
  test_uart_blocking_other_groups();
  test_serialization_roundtrip();
  test_multiple_messages_same_group();

  std::cout << "\nAll DlMessageQueue tests passed!\n";
  return 0;
}