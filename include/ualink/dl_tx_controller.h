#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ualink/dl_command.h"
#include "ualink/dl_flit.h"
#include "ualink/dl_replay.h"
#include "ualink/trace.h"

namespace ualink::dl {

// =============================================================================
// DlTxController: Implements Figure 6-21 Tx Flow Chart
// =============================================================================
//
// Responsibilities:
// - Flit sequence number management (1-511, wraps 511→1, 0 reserved)
// - Replay buffer management (add payload flits, retransmit on request)
// - Command flit generation (Ack, Replay Request scheduling)
// - Explicit/Command flit alternation tracking
//
// Sequence Number Rules (spec 6.6.6):
// - Valid sequence numbers: 1 to 511 (0 is reserved)
// - Sequence 511 wraps to 1
// - NOP flits do NOT consume a sequence number (use Tx_last_seq)
// - Payload flits use (Tx_last_seq + 1) when added to TxReplay buffer
//
// Usage:
//   DlTxController tx;
//
//   // Send payload flit
//   auto [seq, should_add_to_replay] = tx.get_next_seq_for_payload();
//   if (should_add_to_replay) {
//     replay_buffer.add_flit(seq, flit);
//   }
//
//   // Check if we should send ACK
//   if (auto ack_flit = tx.maybe_generate_ack(rx_last_seq)) {
//     send(*ack_flit);
//   }

// =============================================================================
// Tx Sequence State
// =============================================================================

struct TxSequenceState {
  std::uint16_t last_seq{0};       // Last transmitted sequence number (Tx_last_seq)
  std::uint16_t explicit_count{0}; // Count until next command flit opportunity (Tx_explicit_count)
  bool in_replay{false};           // Currently replaying (Tx_replay)
  bool first_replay{false};        // First flit of replay sequence (Tx_first_replay)
};

// Wrap sequence number: 511 → 1, otherwise seq + 1
[[nodiscard]] constexpr std::uint16_t wrap_seq(std::uint16_t seq) noexcept {
  return (seq >= 511) ? 1 : (seq + 1);
}

// =============================================================================
// DlTxController
// =============================================================================

class DlTxController {
public:
  DlTxController();

  // Get next sequence number for payload flit
  // Returns (sequence_number, should_add_to_replay_buffer)
  [[nodiscard]] std::pair<std::uint16_t, bool> get_next_seq_for_payload() noexcept;

  // Get sequence number for NOP flit (reuses Tx_last_seq, does not advance)
  [[nodiscard]] std::uint16_t get_seq_for_nop() const noexcept;

  // Notify that we're entering replay mode (Tx_replay = 1)
  void start_replay() noexcept;

  // Notify that replay is complete (all TxReplay flits sent)
  void finish_replay() noexcept;

  // Check if we're currently in replay mode
  [[nodiscard]] bool is_replaying() const noexcept;

  // Decrement explicit count and check if command flit opportunity
  // Call this before deciding to send ACK or continue with payload
  // Returns true if we should send a command flit now
  [[nodiscard]] bool tick_explicit_count() noexcept;

  // Generate ACK command flit for given ack_seq
  // Uses current Tx_last_seq for flit_seq_lo (lower 3 bits)
  [[nodiscard]] DlFlit generate_ack(std::uint16_t ack_seq) noexcept;

  // Generate Replay Request command flit for given replay_seq
  // Uses current Tx_last_seq for flit_seq_lo (lower 3 bits)
  [[nodiscard]] DlFlit generate_replay_request(std::uint16_t replay_seq) noexcept;

  // Get current state (for debugging/monitoring)
  [[nodiscard]] const TxSequenceState& get_state() const noexcept { return state_; }

  // Reset to initial state
  void reset() noexcept;

  // Statistics
  struct Stats {
    std::size_t payload_flits_sent{0};
    std::size_t nop_flits_sent{0};
    std::size_t ack_flits_sent{0};
    std::size_t replay_req_flits_sent{0};
    std::size_t replay_sequences{0}; // Number of times we entered replay mode
  };

  [[nodiscard]] Stats get_stats() const noexcept { return stats_; }
  void reset_stats() noexcept;

private:
  TxSequenceState state_{};
  Stats stats_{};
};

} // namespace ualink::dl
