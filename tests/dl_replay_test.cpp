#include "ualink/dl_replay.h"

#include <cassert>
#include <iostream>

#include "ualink/trace.h"

using namespace ualink::dl;

static DlFlit make_test_flit(std::uint8_t seed) {
  UALINK_TRACE_SCOPED(__func__);
  DlFlit flit{};
  for (std::size_t byte_index = 0; byte_index < flit.payload.size(); ++byte_index) {
    flit.payload[byte_index] = std::byte{static_cast<unsigned char>(seed + byte_index)};
  }
  return flit;
}

static void test_replay_buffer_empty() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;

  assert(buffer.is_empty());
  assert(!buffer.is_full());
  assert(buffer.size() == 0);
  assert(!buffer.oldest_seq().has_value());
  assert(!buffer.newest_seq().has_value());

  std::cout << "test_replay_buffer_empty: PASS\n";
}

static void test_replay_buffer_add_single() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;
  const DlFlit flit = make_test_flit(0x10);

  const bool added = buffer.add_flit(0, flit);
  assert(added);
  assert(!buffer.is_empty());
  assert(buffer.size() == 1);
  assert(buffer.oldest_seq() == 0);
  assert(buffer.newest_seq() == 0);

  std::cout << "test_replay_buffer_add_single: PASS\n";
}

static void test_replay_buffer_add_multiple() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;

  for (std::uint16_t seq = 0; seq < 10; ++seq) {
    const DlFlit flit = make_test_flit(static_cast<std::uint8_t>(seq));
    const bool added = buffer.add_flit(seq, flit);
    assert(added);
  }

  assert(buffer.size() == 10);
  assert(buffer.oldest_seq() == 0);
  assert(buffer.newest_seq() == 9);

  std::cout << "test_replay_buffer_add_multiple: PASS\n";
}

static void test_replay_buffer_full() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;

  // Fill the buffer
  for (std::size_t seq = 0; seq < kReplayBufferSize; ++seq) {
    const DlFlit flit = make_test_flit(static_cast<std::uint8_t>(seq & 0xFF));
    const bool added = buffer.add_flit(static_cast<std::uint16_t>(seq), flit);
    assert(added);
  }

  assert(buffer.is_full());
  assert(buffer.size() == kReplayBufferSize);

  // Try to add one more - should fail
  const DlFlit flit = make_test_flit(0xFF);
  const bool added = buffer.add_flit(kReplayBufferSize, flit);
  assert(!added);

  std::cout << "test_replay_buffer_full: PASS\n";
}

static void test_replay_buffer_process_ack_single() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;

  const DlFlit flit = make_test_flit(0x10);
  const bool added = buffer.add_flit(0, flit);
  assert(added);

  const std::size_t retired = buffer.process_ack(0);
  assert(retired == 1);
  assert(buffer.is_empty());

  std::cout << "test_replay_buffer_process_ack_single: PASS\n";
}

static void test_replay_buffer_process_ack_multiple() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;

  // Add 10 flits
  for (std::uint16_t seq = 0; seq < 10; ++seq) {
    const DlFlit flit = make_test_flit(static_cast<std::uint8_t>(seq));
    const bool added = buffer.add_flit(seq, flit);
    assert(added);
  }

  // ACK up to sequence 4
  const std::size_t retired = buffer.process_ack(4);
  assert(retired == 5);  // Retired seq 0,1,2,3,4
  assert(buffer.size() == 5);  // Remaining seq 5,6,7,8,9
  assert(buffer.oldest_seq() == 5);
  assert(buffer.newest_seq() == 9);

  std::cout << "test_replay_buffer_process_ack_multiple: PASS\n";
}

static void test_replay_buffer_process_ack_all() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;

  // Add 10 flits
  for (std::uint16_t seq = 0; seq < 10; ++seq) {
    const DlFlit flit = make_test_flit(static_cast<std::uint8_t>(seq));
    const bool added = buffer.add_flit(seq, flit);
    assert(added);
  }

  // ACK all
  const std::size_t retired = buffer.process_ack(9);
  assert(retired == 10);
  assert(buffer.is_empty());

  std::cout << "test_replay_buffer_process_ack_all: PASS\n";
}

static void test_replay_buffer_clear() {
  UALINK_TRACE_SCOPED(__func__);
  DlReplayBuffer buffer;

  // Add some flits
  for (std::uint16_t seq = 0; seq < 5; ++seq) {
    const DlFlit flit = make_test_flit(static_cast<std::uint8_t>(seq));
    const bool added = buffer.add_flit(seq, flit);
    assert(added);
  }

  buffer.clear();
  assert(buffer.is_empty());
  assert(buffer.size() == 0);

  std::cout << "test_replay_buffer_clear: PASS\n";
}

static void test_sequence_tracker_initial() {
  UALINK_TRACE_SCOPED(__func__);
  DlSequenceTracker tracker;

  assert(tracker.expected_seq() == 0);
  assert(tracker.is_expected(0));
  assert(!tracker.is_expected(1));
  assert(!tracker.is_duplicate(0));

  std::cout << "test_sequence_tracker_initial: PASS\n";
}

static void test_sequence_tracker_advance() {
  UALINK_TRACE_SCOPED(__func__);
  DlSequenceTracker tracker;

  tracker.advance();
  assert(tracker.expected_seq() == 1);
  assert(tracker.is_expected(1));
  assert(!tracker.is_expected(0));
  assert(tracker.is_duplicate(0));

  std::cout << "test_sequence_tracker_advance: PASS\n";
}

static void test_sequence_tracker_multiple_advances() {
  UALINK_TRACE_SCOPED(__func__);
  DlSequenceTracker tracker;

  for (std::uint16_t seq = 0; seq < 100; ++seq) {
    assert(tracker.expected_seq() == seq);
    assert(tracker.is_expected(seq));
    tracker.advance();
  }

  assert(tracker.expected_seq() == 100);

  std::cout << "test_sequence_tracker_multiple_advances: PASS\n";
}

static void test_sequence_tracker_wraparound() {
  UALINK_TRACE_SCOPED(__func__);
  DlSequenceTracker tracker;

  // Advance to near the end of sequence space
  for (std::uint16_t seq = 0; seq < 511; ++seq) {
    tracker.advance();
  }

  assert(tracker.expected_seq() == 511);
  tracker.advance();
  assert(tracker.expected_seq() == 0);  // Wrapped around

  std::cout << "test_sequence_tracker_wraparound: PASS\n";
}

static void test_sequence_tracker_reset() {
  UALINK_TRACE_SCOPED(__func__);
  DlSequenceTracker tracker;

  for (std::uint16_t seq = 0; seq < 50; ++seq) {
    tracker.advance();
  }

  tracker.reset();
  assert(tracker.expected_seq() == 0);
  assert(tracker.is_expected(0));

  std::cout << "test_sequence_tracker_reset: PASS\n";
}

static void test_sequence_tracker_duplicate_detection() {
  UALINK_TRACE_SCOPED(__func__);
  DlSequenceTracker tracker;

  // Advance to seq 10
  for (std::uint16_t seq = 0; seq < 10; ++seq) {
    tracker.advance();
  }

  // Expected is 10
  assert(tracker.expected_seq() == 10);

  // Previous sequences are duplicates
  assert(tracker.is_duplicate(9));
  assert(tracker.is_duplicate(8));
  assert(tracker.is_duplicate(0));

  // Current expected is not duplicate
  assert(!tracker.is_duplicate(10));

  // Future sequences are not duplicates
  assert(!tracker.is_duplicate(11));
  assert(!tracker.is_duplicate(20));

  std::cout << "test_sequence_tracker_duplicate_detection: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_replay_buffer_empty();
  test_replay_buffer_add_single();
  test_replay_buffer_add_multiple();
  test_replay_buffer_full();
  test_replay_buffer_process_ack_single();
  test_replay_buffer_process_ack_multiple();
  test_replay_buffer_process_ack_all();
  test_replay_buffer_clear();

  test_sequence_tracker_initial();
  test_sequence_tracker_advance();
  test_sequence_tracker_multiple_advances();
  test_sequence_tracker_wraparound();
  test_sequence_tracker_reset();
  test_sequence_tracker_duplicate_detection();

  std::cout << "\nAll replay buffer tests passed!\n";
  return 0;
}
