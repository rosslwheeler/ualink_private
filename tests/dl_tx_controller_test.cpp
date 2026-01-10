#include "ualink/dl_tx_controller.h"

#include <cassert>
#include <iostream>

using namespace ualink::dl;

static void test_sequence_wrap() {
  // Test wrap_seq function
  assert(wrap_seq(0) == 1);
  assert(wrap_seq(1) == 2);
  assert(wrap_seq(510) == 511);
  assert(wrap_seq(511) == 1); // Wraps to 1
  assert(wrap_seq(512) == 1); // Invalid input but should wrap

  std::cout << "test_sequence_wrap: PASS\n";
}

static void test_initial_state() {
  DlTxController tx;

  const auto& state = tx.get_state();
  assert(state.last_seq == 0);
  assert(state.explicit_count == 0x1F);
  assert(!state.in_replay);
  assert(!state.first_replay);

  assert(!tx.is_replaying());

  const auto stats = tx.get_stats();
  assert(stats.payload_flits_sent == 0);
  assert(stats.nop_flits_sent == 0);
  assert(stats.ack_flits_sent == 0);
  assert(stats.replay_req_flits_sent == 0);
  assert(stats.replay_sequences == 0);

  std::cout << "test_initial_state: PASS\n";
}

static void test_payload_sequence_numbering() {
  DlTxController tx;

  // First payload should get seq=1
  auto [seq1, should_add1] = tx.get_next_seq_for_payload();
  assert(seq1 == 1);
  assert(should_add1 == true); // Should add to replay buffer

  // Second payload should get seq=2
  auto [seq2, should_add2] = tx.get_next_seq_for_payload();
  assert(seq2 == 2);
  assert(should_add2 == true);

  // Verify state
  assert(tx.get_state().last_seq == 2);

  const auto stats = tx.get_stats();
  assert(stats.payload_flits_sent == 2);

  std::cout << "test_payload_sequence_numbering: PASS\n";
}

static void test_sequence_511_wraps_to_1() {
  DlTxController tx;

  // Manually set last_seq to 510
  for (int i = 0; i < 510; ++i) {
    tx.get_next_seq_for_payload();
  }

  assert(tx.get_state().last_seq == 510);

  // Next should be 511
  auto [seq511, _] = tx.get_next_seq_for_payload();
  assert(seq511 == 511);

  // Next should wrap to 1
  auto [seq1, __] = tx.get_next_seq_for_payload();
  assert(seq1 == 1);

  std::cout << "test_sequence_511_wraps_to_1: PASS\n";
}

static void test_nop_reuses_sequence() {
  DlTxController tx;

  // Send first payload (seq=1)
  auto [seq1, _] = tx.get_next_seq_for_payload();
  assert(seq1 == 1);

  // NOP should reuse seq=1
  std::uint16_t nop_seq = tx.get_seq_for_nop();
  assert(nop_seq == 1);

  // Next payload should be seq=2 (NOP didn't advance)
  auto [seq2, __] = tx.get_next_seq_for_payload();
  assert(seq2 == 2);

  // NOP should now reuse seq=2
  nop_seq = tx.get_seq_for_nop();
  assert(nop_seq == 2);

  std::cout << "test_nop_reuses_sequence: PASS\n";
}

static void test_replay_mode() {
  DlTxController tx;

  // Send some payloads normally
  tx.get_next_seq_for_payload(); // seq=1
  tx.get_next_seq_for_payload(); // seq=2

  // Enter replay mode
  tx.start_replay();
  assert(tx.is_replaying());
  assert(tx.get_state().first_replay);

  const auto stats1 = tx.get_stats();
  assert(stats1.replay_sequences == 1);

  // During replay, payloads should NOT be added to replay buffer
  auto [seq3, should_add] = tx.get_next_seq_for_payload();
  assert(seq3 == 3);
  assert(should_add == false); // Don't add during replay

  // Finish replay
  tx.finish_replay();
  assert(!tx.is_replaying());
  assert(!tx.get_state().first_replay);

  // After replay, should add to buffer again
  auto [seq4, should_add4] = tx.get_next_seq_for_payload();
  assert(seq4 == 4);
  assert(should_add4 == true);

  std::cout << "test_replay_mode: PASS\n";
}

static void test_explicit_count_tracking() {
  DlTxController tx;

  // Initial explicit_count is 0x1F (31)
  assert(tx.get_state().explicit_count == 0x1F);

  // Tick 31 times - should return false until last one
  for (int i = 0; i < 30; ++i) {
    bool should_send_command = tx.tick_explicit_count();
    assert(!should_send_command);
  }

  // 31st tick should return true and reset count
  bool should_send_command = tx.tick_explicit_count();
  assert(should_send_command);
  assert(tx.get_state().explicit_count == 0x1F);

  std::cout << "test_explicit_count_tracking: PASS\n";
}

static void test_first_replay_resets_explicit_count() {
  DlTxController tx;

  // Tick down the count a bit
  for (int i = 0; i < 10; ++i) {
    tx.tick_explicit_count();
  }
  assert(tx.get_state().explicit_count < 0x1F);

  // Enter replay mode
  tx.start_replay();
  assert(tx.get_state().first_replay);

  // First tick after replay should return true and reset count
  bool should_send_command = tx.tick_explicit_count();
  assert(should_send_command);
  assert(!tx.get_state().first_replay); // Cleared
  assert(tx.get_state().explicit_count == 0x1F); // Reset

  std::cout << "test_first_replay_resets_explicit_count: PASS\n";
}

static void test_generate_ack() {
  DlTxController tx;

  // Send some payloads to advance last_seq
  tx.get_next_seq_for_payload(); // seq=1
  tx.get_next_seq_for_payload(); // seq=2
  tx.get_next_seq_for_payload(); // seq=3

  // Generate ACK
  DlFlit ack_flit = tx.generate_ack(100);

  // Verify it's a command flit
  auto header = deserialize_command_flit_header(ack_flit.flit_header);
  assert(header.op == 0b010); // ACK opcode
  assert(header.ack_req_seq == 100);
  assert(header.flit_seq_lo == (3 & 0x7)); // Lower 3 bits of last_seq=3

  const auto stats = tx.get_stats();
  assert(stats.ack_flits_sent == 1);

  std::cout << "test_generate_ack: PASS\n";
}

static void test_generate_replay_request() {
  DlTxController tx;

  // Send payloads
  tx.get_next_seq_for_payload(); // seq=1
  tx.get_next_seq_for_payload(); // seq=2

  // Generate Replay Request
  DlFlit replay_flit = tx.generate_replay_request(50);

  // Verify it's a command flit
  auto header = deserialize_command_flit_header(replay_flit.flit_header);
  assert(header.op == 0b011); // Replay Request opcode
  assert(header.ack_req_seq == 50);
  assert(header.flit_seq_lo == (2 & 0x7)); // Lower 3 bits of last_seq=2

  const auto stats = tx.get_stats();
  assert(stats.replay_req_flits_sent == 1);

  std::cout << "test_generate_replay_request: PASS\n";
}

static void test_stats_tracking() {
  DlTxController tx;

  tx.get_next_seq_for_payload();
  tx.get_next_seq_for_payload();
  tx.get_next_seq_for_payload();

  tx.generate_ack(10);
  tx.generate_ack(20);

  tx.generate_replay_request(5);

  tx.start_replay();
  tx.start_replay(); // Two replay sequences

  auto stats = tx.get_stats();
  assert(stats.payload_flits_sent == 3);
  assert(stats.ack_flits_sent == 2);
  assert(stats.replay_req_flits_sent == 1);
  assert(stats.replay_sequences == 2);

  // Reset stats
  tx.reset_stats();
  stats = tx.get_stats();
  assert(stats.payload_flits_sent == 0);
  assert(stats.ack_flits_sent == 0);
  assert(stats.replay_req_flits_sent == 0);
  assert(stats.replay_sequences == 0);

  std::cout << "test_stats_tracking: PASS\n";
}

static void test_reset() {
  DlTxController tx;

  // Modify state
  tx.get_next_seq_for_payload();
  tx.get_next_seq_for_payload();
  tx.start_replay();
  tx.tick_explicit_count();

  // Reset
  tx.reset();

  // Verify back to initial state
  const auto& state = tx.get_state();
  assert(state.last_seq == 0);
  assert(state.explicit_count == 0x1F);
  assert(!state.in_replay);
  assert(!state.first_replay);

  std::cout << "test_reset: PASS\n";
}

int main() {
  test_sequence_wrap();
  test_initial_state();
  test_payload_sequence_numbering();
  test_sequence_511_wraps_to_1();
  test_nop_reuses_sequence();
  test_replay_mode();
  test_explicit_count_tracking();
  test_first_replay_resets_explicit_count();
  test_generate_ack();
  test_generate_replay_request();
  test_stats_tracking();
  test_reset();

  std::cout << "\nAll DlTxController tests passed!\n";
  return 0;
}
