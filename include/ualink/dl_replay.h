#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ualink/dl_flit.h"
#include "ualink/trace.h"

namespace ualink::dl {

// Replay buffer window size - number of flits that can be outstanding
// This is the maximum number of flits that can be sent before receiving an ACK
constexpr std::size_t kReplayBufferSize = 512;

// Sequence number modulo (9 bits for flit_seq_no)
constexpr std::uint16_t kSequenceModulo = 512;

enum class AckNakResult {
  kAckReceived,   // ACK processed, flits retired from buffer
  kNakReceived,   // NAK received, retransmission needed
  kSequenceError, // Sequence number out of range
  kBufferEmpty,   // No flits to acknowledge
};

// Replay buffer for link-level reliability
// Stores transmitted flits until they are acknowledged
class DlReplayBuffer {
public:
  DlReplayBuffer();

  // Add a flit to the replay buffer with its sequence number
  // Returns false if buffer is full
  [[nodiscard]] bool add_flit(std::uint16_t seq_no, const DlFlit &flit);

  // Process an ACK command - retire all flits up to and including ack_seq
  // Returns the number of flits retired
  [[nodiscard]] std::size_t process_ack(std::uint16_t ack_seq);

  // Process a NAK command - get flits for retransmission starting from nak_seq
  // Returns span of flits to retransmit (empty if none)
  [[nodiscard]] std::span<const DlFlit> process_nak(std::uint16_t nak_seq);

  // Get the number of flits currently in the buffer
  [[nodiscard]] std::size_t size() const noexcept;

  // Check if buffer is full
  [[nodiscard]] bool is_full() const noexcept;

  // Check if buffer is empty
  [[nodiscard]] bool is_empty() const noexcept;

  // Get the oldest unacknowledged sequence number
  [[nodiscard]] std::optional<std::uint16_t> oldest_seq() const noexcept;

  // Get the newest sequence number in the buffer
  [[nodiscard]] std::optional<std::uint16_t> newest_seq() const noexcept;

  // Clear all flits from the buffer
  void clear() noexcept;

private:
  struct BufferEntry {
    std::uint16_t seq_no{0};
    DlFlit flit{};
    bool valid{false};
  };

  std::array<BufferEntry, kReplayBufferSize> buffer_{};
  std::size_t head_{0};  // Index of oldest entry
  std::size_t tail_{0};  // Index where next entry will be inserted
  std::size_t count_{0}; // Number of valid entries
};

// Sequence number tracker for received flits
class DlSequenceTracker {
public:
  DlSequenceTracker();

  // Check if the sequence number is expected (next in sequence)
  [[nodiscard]] bool is_expected(std::uint16_t seq_no) const noexcept;

  // Check if the sequence number is duplicate (already received)
  [[nodiscard]] bool is_duplicate(std::uint16_t seq_no) const noexcept;

  // Advance to the next expected sequence number
  void advance() noexcept;

  // Get the current expected sequence number
  [[nodiscard]] std::uint16_t expected_seq() const noexcept;

  // Reset to initial state
  void reset() noexcept;

private:
  std::uint16_t expected_seq_{1};
};

} // namespace ualink::dl
