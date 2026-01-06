#include "ualink/dl_pacing.h"

#include <cassert>
#include <iostream>

#include "ualink/dl_flit.h"
#include "ualink/trace.h"

using namespace ualink::dl;

static TlFlit make_test_flit(std::uint8_t seed) {
  UALINK_TRACE_SCOPED(__func__);
  TlFlit flit{};
  for (std::size_t byte_index = 0; byte_index < flit.data.size(); ++byte_index) {
    flit.data[byte_index] = std::byte{static_cast<unsigned char>(seed + byte_index)};
  }
  return flit;
}

static void test_pacing_controller_no_callbacks() {
  UALINK_TRACE_SCOPED(__func__);
  DlPacingController pacing;

  assert(!pacing.has_tx_callback());
  assert(!pacing.has_rx_callback());

  // Without callbacks, should allow
  const PacingDecision decision = pacing.check_tx_pacing(10, 640);
  assert(decision == PacingDecision::kAllow);

  // Notify should be no-op
  pacing.notify_rx(10, 640, true);

  std::cout << "test_pacing_controller_no_callbacks: PASS\n";
}

static void test_pacing_controller_tx_callback() {
  UALINK_TRACE_SCOPED(__func__);
  DlPacingController pacing;

  std::size_t callback_count = 0;
  pacing.set_tx_callback([&callback_count](std::size_t flit_count, std::size_t total_bytes) {
    callback_count++;
    if (flit_count > 5) {
      return PacingDecision::kThrottle;
    }
    return PacingDecision::kAllow;
  });

  assert(pacing.has_tx_callback());

  // Should allow small count
  PacingDecision decision = pacing.check_tx_pacing(3, 192);
  assert(decision == PacingDecision::kAllow);
  assert(callback_count == 1);

  // Should throttle large count
  decision = pacing.check_tx_pacing(10, 640);
  assert(decision == PacingDecision::kThrottle);
  assert(callback_count == 2);

  std::cout << "test_pacing_controller_tx_callback: PASS\n";
}

static void test_pacing_controller_rx_callback() {
  UALINK_TRACE_SCOPED(__func__);
  DlPacingController pacing;

  std::size_t callback_count = 0;
  std::size_t total_flits = 0;
  bool last_crc_valid = false;

  pacing.set_rx_callback([&](std::size_t flit_count, std::size_t /* total_bytes */, bool crc_valid) {
    callback_count++;
    total_flits += flit_count;
    last_crc_valid = crc_valid;
  });

  assert(pacing.has_rx_callback());

  pacing.notify_rx(5, 320, true);
  assert(callback_count == 1);
  assert(total_flits == 5);
  assert(last_crc_valid);

  pacing.notify_rx(3, 192, false);
  assert(callback_count == 2);
  assert(total_flits == 8);
  assert(!last_crc_valid);

  std::cout << "test_pacing_controller_rx_callback: PASS\n";
}

static void test_pacing_controller_clear_callbacks() {
  UALINK_TRACE_SCOPED(__func__);
  DlPacingController pacing;

  pacing.set_tx_callback([](std::size_t, std::size_t) { return PacingDecision::kDrop; });
  pacing.set_rx_callback([](std::size_t, std::size_t, bool) {});

  assert(pacing.has_tx_callback());
  assert(pacing.has_rx_callback());

  pacing.clear_callbacks();

  assert(!pacing.has_tx_callback());
  assert(!pacing.has_rx_callback());

  std::cout << "test_pacing_controller_clear_callbacks: PASS\n";
}

static void test_simple_tx_rate_limiter() {
  UALINK_TRACE_SCOPED(__func__);
  SimpleTxRateLimiter limiter(10);  // Max 10 flits per window

  assert(limiter.window_count() == 0);

  // Allow 5 flits
  PacingDecision decision = limiter(5, 320);
  assert(decision == PacingDecision::kAllow);
  assert(limiter.window_count() == 5);

  // Allow 4 more (total 9, under limit)
  decision = limiter(4, 256);
  assert(decision == PacingDecision::kAllow);
  assert(limiter.window_count() == 9);

  // Throttle 2 more (would exceed limit)
  decision = limiter(2, 128);
  assert(decision == PacingDecision::kThrottle);
  assert(limiter.window_count() == 9);  // Didn't increment

  // Reset window
  limiter.reset_window();
  assert(limiter.window_count() == 0);

  // Now can send again
  decision = limiter(10, 640);
  assert(decision == PacingDecision::kAllow);
  assert(limiter.window_count() == 10);

  std::cout << "test_simple_tx_rate_limiter: PASS\n";
}

static void test_byte_based_rate_limiter() {
  UALINK_TRACE_SCOPED(__func__);
  ByteBasedRateLimiter limiter(1000);  // Max 1000 bytes per window

  assert(limiter.window_bytes() == 0);

  // Allow 500 bytes
  PacingDecision decision = limiter(5, 500);
  assert(decision == PacingDecision::kAllow);
  assert(limiter.window_bytes() == 500);

  // Allow 400 more (total 900, under limit)
  decision = limiter(4, 400);
  assert(decision == PacingDecision::kAllow);
  assert(limiter.window_bytes() == 900);

  // Throttle 200 more (would exceed limit)
  decision = limiter(2, 200);
  assert(decision == PacingDecision::kThrottle);
  assert(limiter.window_bytes() == 900);  // Didn't increment

  // Reset window
  limiter.reset_window();
  assert(limiter.window_bytes() == 0);

  std::cout << "test_byte_based_rate_limiter: PASS\n";
}

static void test_rx_backpressure_tracker() {
  UALINK_TRACE_SCOPED(__func__);
  RxBackpressureTracker tracker(100);  // 100 flit capacity, threshold at 75

  assert(tracker.buffer_occupancy() == 0);
  assert(!tracker.should_signal_backpressure());

  // Add 50 flits
  tracker(50, 3200, true);
  assert(tracker.buffer_occupancy() == 50);
  assert(!tracker.should_signal_backpressure());

  // Add 30 more (total 80, above threshold)
  tracker(30, 1920, true);
  assert(tracker.buffer_occupancy() == 80);
  assert(tracker.should_signal_backpressure());

  // Consume 40 flits
  tracker.consume_flits(40);
  assert(tracker.buffer_occupancy() == 40);
  assert(!tracker.should_signal_backpressure());

  // Reset
  tracker.reset();
  assert(tracker.buffer_occupancy() == 0);

  std::cout << "test_rx_backpressure_tracker: PASS\n";
}

static void test_rx_backpressure_tracker_saturation() {
  UALINK_TRACE_SCOPED(__func__);
  RxBackpressureTracker tracker(50);

  // Try to add more than capacity
  tracker(100, 6400, true);
  assert(tracker.buffer_occupancy() == 50);  // Saturates at capacity

  std::cout << "test_rx_backpressure_tracker_saturation: PASS\n";
}

static void test_pack_with_pacing_allow() {
  UALINK_TRACE_SCOPED(__func__);
  DlPacingController pacing;
  SimpleTxRateLimiter limiter(10);

  pacing.set_tx_callback([&limiter](std::size_t flit_count, std::size_t total_bytes) {
    return limiter(flit_count, total_bytes);
  });

  std::array<TlFlit, 3> tl_flits{make_test_flit(0x10), make_test_flit(0x20), make_test_flit(0x30)};

  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::size_t packed = 0;
  const DlFlit flit = DlSerializer::serialize_with_pacing(tl_flits, header, pacing, &packed);

  assert(packed == 3);
  assert(limiter.window_count() == 3);

  std::cout << "test_pack_with_pacing_allow: PASS\n";
}

static void test_pack_with_pacing_drop() {
  UALINK_TRACE_SCOPED(__func__);
  DlPacingController pacing;

  pacing.set_tx_callback([](std::size_t, std::size_t) {
    return PacingDecision::kDrop;
  });

  std::array<TlFlit, 3> tl_flits{make_test_flit(0x10), make_test_flit(0x20), make_test_flit(0x30)};

  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::size_t packed = 0;
  const DlFlit flit = DlSerializer::serialize_with_pacing(tl_flits, header, pacing, &packed);

  assert(packed == 0);  // Dropped

  std::cout << "test_pack_with_pacing_drop: PASS\n";
}

static void test_unpack_with_pacing() {
  UALINK_TRACE_SCOPED(__func__);
  DlPacingController pacing;

  std::size_t rx_count = 0;
  pacing.set_rx_callback([&rx_count](std::size_t flit_count, std::size_t, bool) {
    rx_count = flit_count;
  });

  // Create and pack flits
  std::array<TlFlit, 2> tl_flits{make_test_flit(0x10), make_test_flit(0x20)};
  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  const DlFlit flit = DlSerializer::serialize(tl_flits, header);

  // Unpack with pacing
  const std::vector<TlFlit> unpacked = DlDeserializer::deserialize_with_pacing(flit, pacing);

  assert(unpacked.size() == 2);
  assert(rx_count == 2);

  std::cout << "test_unpack_with_pacing: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_pacing_controller_no_callbacks();
  test_pacing_controller_tx_callback();
  test_pacing_controller_rx_callback();
  test_pacing_controller_clear_callbacks();

  test_simple_tx_rate_limiter();
  test_byte_based_rate_limiter();

  test_rx_backpressure_tracker();
  test_rx_backpressure_tracker_saturation();

  test_pack_with_pacing_allow();
  test_pack_with_pacing_drop();
  test_unpack_with_pacing();

  std::cout << "\nAll pacing tests passed!\n";
  return 0;
}
