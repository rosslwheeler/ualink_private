#include "ualink/dl_replay.h"

#include <algorithm>

using namespace ualink::dl;

DlReplayBuffer::DlReplayBuffer() { UALINK_TRACE_SCOPED(__func__); }

bool DlReplayBuffer::add_flit(std::uint16_t seq_no, const DlFlit &flit) {
  UALINK_TRACE_SCOPED(__func__);

  if (is_full()) {
    return false;
  }

  buffer_[tail_].seq_no = seq_no;
  buffer_[tail_].flit = flit;
  buffer_[tail_].valid = true;

  tail_ = (tail_ + 1) % kReplayBufferSize;
  count_++;

  return true;
}

std::size_t DlReplayBuffer::process_ack(std::uint16_t ack_seq) {
  UALINK_TRACE_SCOPED(__func__);

  std::size_t retired = 0;

  while (!is_empty()) {
    const std::uint16_t oldest = buffer_[head_].seq_no;

    // Check if this flit is acknowledged
    // Handle wrap-around: ACK acknowledges all flits with seq <= ack_seq (modulo)
    const bool is_acked = [&]() {
      if (ack_seq >= oldest) {
        return true;
      }
      // Wrap-around case: ack_seq wrapped but oldest hasn't
      // Example: oldest=510, ack_seq=5 should ack 510,511,0,1,2,3,4,5
      const std::uint16_t distance_forward = (ack_seq + kSequenceModulo - oldest) % kSequenceModulo;
      return distance_forward < (kSequenceModulo / 2);
    }();

    if (!is_acked) {
      break;
    }

    // Retire this flit
    buffer_[head_].valid = false;
    head_ = (head_ + 1) % kReplayBufferSize;
    count_--;
    retired++;

    // If we just retired the ACKed sequence, we're done
    if (oldest == ack_seq) {
      break;
    }
  }

  return retired;
}

std::span<const DlFlit> DlReplayBuffer::process_nak(std::uint16_t nak_seq) {
  UALINK_TRACE_SCOPED(__func__);

  if (is_empty()) {
    return {};
  }

  // Find the entry with nak_seq
  std::size_t index = head_;
  for (std::size_t entry_index = 0; entry_index < count_; ++entry_index) {
    if (buffer_[index].seq_no == nak_seq) {
      // Return span from this point to the end of valid entries
      // Note: This only works if entries are contiguous, which they are in our circular buffer
      // For simplicity, we'll return empty for now and handle retransmission differently
      // TODO: Implement proper retransmission span
      return {};
    }
    index = (index + 1) % kReplayBufferSize;
  }

  return {};
}

std::size_t DlReplayBuffer::size() const noexcept { return count_; }

bool DlReplayBuffer::is_full() const noexcept { return count_ == kReplayBufferSize; }

bool DlReplayBuffer::is_empty() const noexcept { return count_ == 0; }

std::optional<std::uint16_t> DlReplayBuffer::oldest_seq() const noexcept {
  if (is_empty()) {
    return std::nullopt;
  }
  return buffer_[head_].seq_no;
}

std::optional<std::uint16_t> DlReplayBuffer::newest_seq() const noexcept {
  if (is_empty()) {
    return std::nullopt;
  }
  const std::size_t last_index = (tail_ + kReplayBufferSize - 1) % kReplayBufferSize;
  return buffer_[last_index].seq_no;
}

void DlReplayBuffer::clear() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  for (std::size_t entry_index = 0; entry_index < buffer_.size(); ++entry_index) {
    buffer_[entry_index].valid = false;
  }
  head_ = 0;
  tail_ = 0;
  count_ = 0;
}

DlSequenceTracker::DlSequenceTracker() { UALINK_TRACE_SCOPED(__func__); }

bool DlSequenceTracker::is_expected(std::uint16_t seq_no) const noexcept { return seq_no == expected_seq_; }

bool DlSequenceTracker::is_duplicate(std::uint16_t seq_no) const noexcept {
  // A duplicate is a sequence number that is before the expected one
  // Handle wrap-around
  if (seq_no < expected_seq_) {
    const std::uint16_t distance = expected_seq_ - seq_no;
    // If distance is small, it's a duplicate. If large, it's wrapped ahead
    return distance < (kSequenceModulo / 2);
  }

  if (seq_no > expected_seq_) {
    const std::uint16_t distance = seq_no - expected_seq_;
    // If distance is large, it's wrapped behind (duplicate)
    return distance >= (kSequenceModulo / 2);
  }

  // seq_no == expected_seq_, not a duplicate
  return false;
}

void DlSequenceTracker::advance() noexcept { expected_seq_ = (expected_seq_ + 1) % kSequenceModulo; }

std::uint16_t DlSequenceTracker::expected_seq() const noexcept { return expected_seq_; }

void DlSequenceTracker::reset() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  expected_seq_ = 1;
}
