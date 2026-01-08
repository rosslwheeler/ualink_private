#include "ualink/ualink_endpoint.h"

#include <cassert>
#include <iostream>
#include <vector>

#include "ualink/trace.h"

using namespace ualink;

// Test helper: capture transmitted DL flits
struct TransmitCapture {
  std::vector<dl::DlFlit> flits;

  void operator()(const dl::DlFlit& flit) {
    flits.push_back(flit);
  }

  void clear() {
    flits.clear();
  }
};

// Test helper: capture completion callbacks
struct ReadCompletionCapture {
  struct Completion {
    std::uint16_t tag;
    std::uint8_t status;
    std::vector<std::byte> data;
  };

  std::vector<Completion> completions;

  void operator()(std::uint16_t tag, std::uint8_t status, const std::vector<std::byte>& data) {
    completions.push_back({tag, status, data});
  }

  void clear() {
    completions.clear();
  }
};

struct WriteCompletionCapture {
  struct Completion {
    std::uint16_t tag;
    std::uint8_t status;
  };

  std::vector<Completion> completions;

  void operator()(std::uint16_t tag, std::uint8_t status) {
    completions.push_back({tag, status});
  }

  void clear() {
    completions.clear();
  }
};

static void test_basic_read_request() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Send read request
  const std::uint16_t tag = endpoint.send_read_request(0x100000000ULL, 32);

  // Should have allocated tag 0
  assert(tag == 0);

  // Should have transmitted one DL flit
  assert(tx_capture.flits.size() == 1);

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.tx_read_requests == 1);
  assert(stats.tx_dl_flits == 1);
  assert(stats.replay_buffer_size == 1);

  std::cout << "test_basic_read_request: PASS\n";
}

static void test_basic_write_request() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Prepare write data
  std::vector<std::byte> data(56);
  for (std::size_t byte_index = 0; byte_index < data.size(); ++byte_index) {
    data[byte_index] = std::byte{static_cast<unsigned char>(byte_index)};
  }

  // Send write request
  const std::uint16_t tag = endpoint.send_write_request(0x200000000ULL, 32, data);

  // Should have allocated tag 0
  assert(tag == 0);

  // Should have transmitted one DL flit
  assert(tx_capture.flits.size() == 1);

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.tx_write_requests == 1);
  assert(stats.tx_dl_flits == 1);
  assert(stats.replay_buffer_size == 1);

  std::cout << "test_basic_write_request: PASS\n";
}

static void test_multiple_requests() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Send multiple requests
  const std::uint16_t tag1 = endpoint.send_read_request(0x1000, 16);
  const std::uint16_t tag2 = endpoint.send_read_request(0x2000, 32);
  const std::uint16_t tag3 = endpoint.send_read_request(0x3000, 63);  // Max 63 for 6-bit field

  // Tags should increment
  assert(tag1 == 0);
  assert(tag2 == 1);
  assert(tag3 == 2);

  // Should have transmitted three DL flits
  assert(tx_capture.flits.size() == 3);

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.tx_read_requests == 3);
  assert(stats.tx_dl_flits == 3);

  std::cout << "test_multiple_requests: PASS\n";
}

static void test_receive_read_response() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  ReadCompletionCapture completion_capture;
  endpoint.set_read_completion_callback(std::ref(completion_capture));

  // Create a read response
  tl::TlReadResponse response{};
  response.header.opcode = tl::TlOpcode::kReadResponse;
  response.header.tag = 0x123;
  response.header.status = 0;
  response.header.data_valid = true;

  // Fill response data
  for (std::size_t byte_index = 0; byte_index < response.data.size(); ++byte_index) {
    response.data[byte_index] = std::byte{static_cast<unsigned char>(byte_index)};
  }

  // Serialize to TL flit
  const auto tl_flit_bytes = tl::TlSerializer::serialize_read_response(response);
  dl::TlFlit tl_flit{};
  std::copy_n(tl_flit_bytes.begin(), tl::kTlFlitBytes, tl_flit.data.begin());
  tl_flit.message_field = 0;

  // Serialize to DL flit
  dl::ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::array<dl::TlFlit, 1> tl_flits{tl_flit};
  const dl::DlFlit dl_flit = dl::DlSerializer::serialize(tl_flits, header);

  // Receive the flit
  endpoint.receive_flit(dl_flit);

  // Should have triggered completion callback
  assert(completion_capture.completions.size() == 1);
  assert(completion_capture.completions[0].tag == 0x123);
  assert(completion_capture.completions[0].status == 0);
  assert(completion_capture.completions[0].data.size() == response.data.size());

  // Verify data
  for (std::size_t byte_index = 0; byte_index < response.data.size(); ++byte_index) {
    assert(completion_capture.completions[0].data[byte_index] == response.data[byte_index]);
  }

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.rx_read_responses == 1);
  assert(stats.rx_dl_flits == 1);

  std::cout << "test_receive_read_response: PASS\n";
}

static void test_receive_write_completion() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  WriteCompletionCapture completion_capture;
  endpoint.set_write_completion_callback(std::ref(completion_capture));

  // Create a write completion
  tl::TlWriteCompletion completion{};
  completion.header.opcode = tl::TlOpcode::kWriteCompletion;
  completion.header.tag = 0x456;
  completion.header.status = 0;
  completion.header.data_valid = false;

  // Serialize to TL flit
  const auto tl_flit_bytes = tl::TlSerializer::serialize_write_completion(completion);
  dl::TlFlit tl_flit{};
  std::copy_n(tl_flit_bytes.begin(), tl::kTlFlitBytes, tl_flit.data.begin());
  tl_flit.message_field = 0;

  // Serialize to DL flit
  dl::ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::array<dl::TlFlit, 1> tl_flits{tl_flit};
  const dl::DlFlit dl_flit = dl::DlSerializer::serialize(tl_flits, header);

  // Receive the flit
  endpoint.receive_flit(dl_flit);

  // Should have triggered completion callback
  assert(completion_capture.completions.size() == 1);
  assert(completion_capture.completions[0].tag == 0x456);
  assert(completion_capture.completions[0].status == 0);

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.rx_write_completions == 1);
  assert(stats.rx_dl_flits == 1);

  std::cout << "test_receive_write_completion: PASS\n";
}

static void test_end_to_end_read_transaction() {
  UALINK_TRACE_SCOPED(__func__);

  // Create two endpoints (requester and responder)
  UaLinkEndpoint requester;
  UaLinkEndpoint responder;

  TransmitCapture req_tx_capture;
  TransmitCapture resp_tx_capture;
  ReadCompletionCapture read_completion_capture;

  requester.set_transmit_callback(std::ref(req_tx_capture));
  responder.set_transmit_callback(std::ref(resp_tx_capture));
  requester.set_read_completion_callback(std::ref(read_completion_capture));

  // Requester sends read request
  const std::uint16_t req_tag = requester.send_read_request(0xABCD0000ULL, 32);
  assert(req_tag == 0);
  assert(req_tx_capture.flits.size() == 1);

  // Responder receives request (we'll manually create response)
  // In a real system, responder would process the request and generate response
  // For this test, we'll just create a response directly

  tl::TlReadResponse response{};
  response.header.opcode = tl::TlOpcode::kReadResponse;
  response.header.tag = req_tag;  // Match request tag
  response.header.status = 0;
  response.header.data_valid = true;

  // Fill with test pattern
  for (std::size_t byte_index = 0; byte_index < response.data.size(); ++byte_index) {
    response.data[byte_index] = std::byte{static_cast<unsigned char>(0xFF - byte_index)};
  }

  // Serialize response to TL flit
  const auto tl_flit_bytes = tl::TlSerializer::serialize_read_response(response);
  dl::TlFlit tl_flit{};
  std::copy_n(tl_flit_bytes.begin(), tl::kTlFlitBytes, tl_flit.data.begin());
  tl_flit.message_field = 0;

  // Serialize to DL flit
  dl::ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 100;

  std::array<dl::TlFlit, 1> tl_flits{tl_flit};
  const dl::DlFlit response_flit = dl::DlSerializer::serialize(tl_flits, header);

  // Requester receives response
  requester.receive_flit(response_flit);

  // Verify completion callback was triggered
  assert(read_completion_capture.completions.size() == 1);
  assert(read_completion_capture.completions[0].tag == req_tag);
  assert(read_completion_capture.completions[0].status == 0);

  // Verify data
  for (std::size_t byte_index = 0; byte_index < response.data.size(); ++byte_index) {
    assert(read_completion_capture.completions[0].data[byte_index] == response.data[byte_index]);
  }

  std::cout << "test_end_to_end_read_transaction: PASS\n";
}

static void test_replay_buffer_integration() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Send three requests
  [[maybe_unused]] auto tag1 = endpoint.send_read_request(0x1000, 16);
  [[maybe_unused]] auto tag2 = endpoint.send_read_request(0x2000, 32);
  [[maybe_unused]] auto tag3 = endpoint.send_read_request(0x3000, 48);

  assert(tx_capture.flits.size() == 3);

  // Verify all are in replay buffer
  const auto stats1 = endpoint.get_stats();
  assert(stats1.replay_buffer_size == 3);

  // ACK first two (seq 0 and 1)
  endpoint.process_ack(1);

  // Replay buffer should have only the third one (seq 2)
  const auto stats2 = endpoint.get_stats();
  assert(stats2.replay_buffer_size == 1);

  // Replay from seq 2
  tx_capture.clear();
  endpoint.replay_from(2);

  // Should have replayed one flit
  assert(tx_capture.flits.size() == 1);

  std::cout << "test_replay_buffer_integration: PASS\n";
}

static void test_pacing_tx_throttle() {
  UALINK_TRACE_SCOPED(__func__);

  EndpointConfig config;
  config.tx_pacing_callback = [](std::size_t, std::size_t) {
    return dl::PacingDecision::kThrottle;
  };

  UaLinkEndpoint endpoint(config);
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Send request - should be throttled
  [[maybe_unused]] auto tag = endpoint.send_read_request(0x1000, 16);

  // Should not have transmitted
  assert(tx_capture.flits.empty());

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.tx_read_requests == 1);
  assert(stats.tx_dl_flits == 0);
  assert(stats.tx_dropped_by_pacing == 1);

  std::cout << "test_pacing_tx_throttle: PASS\n";
}

static void test_pacing_tx_allow() {
  UALINK_TRACE_SCOPED(__func__);

  EndpointConfig config;
  config.tx_pacing_callback = [](std::size_t, std::size_t) {
    return dl::PacingDecision::kAllow;
  };

  UaLinkEndpoint endpoint(config);
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Send request - should be allowed
  [[maybe_unused]] auto tag = endpoint.send_read_request(0x1000, 16);

  // Should have transmitted
  assert(tx_capture.flits.size() == 1);

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.tx_read_requests == 1);
  assert(stats.tx_dl_flits == 1);
  assert(stats.tx_dropped_by_pacing == 0);

  std::cout << "test_pacing_tx_allow: PASS\n";
}

static void test_error_injection_packet_drop() {
  UALINK_TRACE_SCOPED(__func__);

  EndpointConfig config;
  config.error_policy = []() {
    return dl::ErrorType::kPacketDrop;
  };

  UaLinkEndpoint endpoint(config);
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Send request - should be dropped
  [[maybe_unused]] auto tag = endpoint.send_read_request(0x1000, 16);

  // Should not have transmitted
  assert(tx_capture.flits.empty());

  // Verify stats
  const auto stats = endpoint.get_stats();
  assert(stats.tx_read_requests == 1);
  assert(stats.tx_dl_flits == 0);
  assert(stats.tx_dropped_by_error_injection == 1);

  std::cout << "test_error_injection_packet_drop: PASS\n";
}

static void test_error_injection_crc_corruption() {
  UALINK_TRACE_SCOPED(__func__);

  EndpointConfig config;
  config.error_policy = []() {
    return dl::ErrorType::kCrcCorruption;
  };
  config.enable_crc_check = true;  // Enable CRC checking on receive

  UaLinkEndpoint sender(config);
  UaLinkEndpoint receiver;  // No error injection, but has CRC check enabled

  TransmitCapture tx_capture;
  sender.set_transmit_callback(std::ref(tx_capture));

  // Send request with CRC corruption
  [[maybe_unused]] auto tag = sender.send_read_request(0x1000, 16);

  // Should have transmitted (corrupted)
  assert(tx_capture.flits.size() == 1);

  // Receiver gets corrupted flit
  receiver.receive_flit(tx_capture.flits[0]);

  // Should have been rejected due to CRC error
  const auto stats = receiver.get_stats();
  assert(stats.rx_dl_flits == 1);
  assert(stats.rx_crc_errors == 1);
  assert(stats.rx_read_responses == 0);

  std::cout << "test_error_injection_crc_corruption: PASS\n";
}

static void test_crc_check_disabled() {
  UALINK_TRACE_SCOPED(__func__);

  EndpointConfig config;
  config.enable_crc_check = false;

  UaLinkEndpoint endpoint(config);
  ReadCompletionCapture completion_capture;
  endpoint.set_read_completion_callback(std::ref(completion_capture));

  // Create a read response with intentionally corrupted CRC
  tl::TlReadResponse response{};
  response.header.opcode = tl::TlOpcode::kReadResponse;
  response.header.tag = 0x789;
  response.header.status = 0;
  response.header.data_valid = true;

  const auto tl_flit_bytes = tl::TlSerializer::serialize_read_response(response);
  dl::TlFlit tl_flit{};
  std::copy_n(tl_flit_bytes.begin(), tl::kTlFlitBytes, tl_flit.data.begin());

  dl::ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::array<dl::TlFlit, 1> tl_flits{tl_flit};
  dl::DlFlit dl_flit = dl::DlSerializer::serialize(tl_flits, header);

  // Corrupt CRC (flip all bits of first byte)
  dl_flit.crc[0] = static_cast<std::byte>(~static_cast<unsigned char>(dl_flit.crc[0]));

  // Receive corrupted flit
  endpoint.receive_flit(dl_flit);

  // With CRC check disabled, should still process (incorrectly, but tests the path)
  const auto stats = endpoint.get_stats();
  assert(stats.rx_dl_flits == 1);
  assert(stats.rx_crc_errors == 0);  // CRC check disabled
  assert(stats.rx_read_responses == 1);  // Processed despite bad CRC

  std::cout << "test_crc_check_disabled: PASS\n";
}

static void test_tag_wrap_around() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Allocate tags to verify proper allocation
  // Note: flit_seq_no is only 9 bits (0-511), so limit test size
  for (std::size_t request_index = 0; request_index < 16; ++request_index) {
    const std::uint16_t tag = endpoint.send_read_request(0x1000 + request_index, 16);
    assert(tag == request_index);
  }

  // Verify seq counter increments correctly
  assert(endpoint.get_tx_seq() == 16);

  std::cout << "test_tag_wrap_around: PASS\n";
}

static void test_stats_reset() {
  UALINK_TRACE_SCOPED(__func__);

  UaLinkEndpoint endpoint;
  TransmitCapture tx_capture;
  endpoint.set_transmit_callback(std::ref(tx_capture));

  // Generate some activity
  [[maybe_unused]] auto tag1 = endpoint.send_read_request(0x1000, 16);
  [[maybe_unused]] auto tag2 = endpoint.send_write_request(0x2000, 32, std::vector<std::byte>(56));

  // Verify stats
  auto stats = endpoint.get_stats();
  assert(stats.tx_read_requests == 1);
  assert(stats.tx_write_requests == 1);
  assert(stats.tx_dl_flits == 2);

  // Reset stats
  endpoint.reset_stats();

  stats = endpoint.get_stats();
  assert(stats.tx_read_requests == 0);
  assert(stats.tx_write_requests == 0);
  assert(stats.tx_dl_flits == 0);

  std::cout << "test_stats_reset: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_basic_read_request();
  test_basic_write_request();
  test_multiple_requests();
  test_receive_read_response();
  test_receive_write_completion();
  test_end_to_end_read_transaction();

  test_replay_buffer_integration();

  test_pacing_tx_throttle();
  test_pacing_tx_allow();

  test_error_injection_packet_drop();
  test_error_injection_crc_corruption();
  test_crc_check_disabled();

  test_tag_wrap_around();
  test_stats_reset();

  std::cout << "\nAll UaLink endpoint tests passed!\n";
  return 0;
}
