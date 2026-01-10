#include "ualink/dl_tx_controller.h"

#include <stdexcept>

namespace ualink::dl {

DlTxController::DlTxController() {
  UALINK_TRACE_SCOPED(__func__);
  reset();
}

std::pair<std::uint16_t, bool> DlTxController::get_next_seq_for_payload() noexcept {
  UALINK_TRACE_SCOPED(__func__);

  // Payload flits always use (Tx_last_seq + 1)
  const std::uint16_t next_seq = wrap_seq(state_.last_seq);
  state_.last_seq = next_seq;

  // Payload flits are added to replay buffer (unless we're already replaying)
  const bool should_add_to_replay = !state_.in_replay;

  stats_.payload_flits_sent++;

  return {next_seq, should_add_to_replay};
}

std::uint16_t DlTxController::get_seq_for_nop() const noexcept {
  UALINK_TRACE_SCOPED(__func__);
  // NOP flits reuse Tx_last_seq (do not consume a sequence number)
  return state_.last_seq;
}

void DlTxController::start_replay() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  state_.in_replay = true;
  state_.first_replay = true;
  stats_.replay_sequences++;
}

void DlTxController::finish_replay() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  state_.in_replay = false;
  state_.first_replay = false;
}

bool DlTxController::is_replaying() const noexcept {
  return state_.in_replay;
}

bool DlTxController::tick_explicit_count() noexcept {
  UALINK_TRACE_SCOPED(__func__);

  // Handle first replay flit special case
  if (state_.first_replay) {
    state_.first_replay = false;
    state_.explicit_count = 0x1F; // Reset count after first replay
    return true; // First replay flit is a command flit opportunity
  }

  // Decrement explicit count
  if (state_.explicit_count > 0) {
    state_.explicit_count--;
  }

  // Check if we hit command flit opportunity
  if (state_.explicit_count == 0) {
    state_.explicit_count = 0x1F; // Reset for next cycle
    return true;
  }

  return false;
}

DlFlit DlTxController::generate_ack(std::uint16_t ack_seq) noexcept {
  UALINK_TRACE_SCOPED(__func__);

  // Use lower 3 bits of Tx_last_seq for flit_seq_lo
  const std::uint8_t flit_seq_lo = static_cast<std::uint8_t>(state_.last_seq & 0x7U);

  stats_.ack_flits_sent++;

  return CommandFactory::create_ack(ack_seq, flit_seq_lo);
}

DlFlit DlTxController::generate_replay_request(std::uint16_t replay_seq) noexcept {
  UALINK_TRACE_SCOPED(__func__);

  // Use lower 3 bits of Tx_last_seq for flit_seq_lo
  const std::uint8_t flit_seq_lo = static_cast<std::uint8_t>(state_.last_seq & 0x7U);

  stats_.replay_req_flits_sent++;

  return CommandFactory::create_replay_request(replay_seq, flit_seq_lo);
}

void DlTxController::reset() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  state_ = TxSequenceState{};
  state_.last_seq = 0;           // Will advance to 1 on first payload
  state_.explicit_count = 0x1F;  // Initial value per spec
  state_.in_replay = false;
  state_.first_replay = false;
}

void DlTxController::reset_stats() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  stats_ = Stats{};
}

} // namespace ualink::dl
