#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "ualink/dl_flit.h"
#include "ualink/dl_replay.h"
#include "ualink/trace.h"

namespace ualink::dl {

// DL command opcodes
enum class DlCommandOp : std::uint8_t {
  // Command flits use Table 6-15 opcodes.
  // Note: explicit (payload/NOP) flits use Table 6-14 and are not command flits.
  kAck = 0b010,           // Ack
  kReplayRequest = 0b011, // Standard Replay Request
};

// Command flit factory functions
namespace CommandFactory {

// Create an ACK command flit
// ack_seq: sequence number of last successfully received flit
// flit_seq_lo: lower 3 bits of our transmit sequence number
[[nodiscard]] DlFlit create_ack(std::uint16_t ack_seq, std::uint8_t flit_seq_lo);

// Create a Standard Replay Request command flit
// replay_seq: sequence number of first payload flit to replay
// flit_seq_lo: lower 3 bits of our transmit sequence number
[[nodiscard]] DlFlit create_replay_request(std::uint16_t replay_seq, std::uint8_t flit_seq_lo);

} // namespace CommandFactory

// Callback types for command processing
using AckCallback = std::function<void(std::uint16_t ack_seq)>;
using ReplayRequestCallback = std::function<void(std::uint16_t replay_seq)>;

// DL Command processor - handles ACK / Replay Request commands
class DlCommandProcessor {
public:
  DlCommandProcessor();

  // Set callbacks for command processing
  void set_ack_callback(AckCallback callback) noexcept;
  void set_replay_request_callback(ReplayRequestCallback callback) noexcept;

  // Check if callbacks are set
  [[nodiscard]] bool has_ack_callback() const noexcept;
  [[nodiscard]] bool has_replay_request_callback() const noexcept;

  // Process received DL flit - extracts and handles commands
  // Returns true if flit was a command (Ack/Replay Request), false if it was data
  [[nodiscard]] bool process_flit(const DlFlit &flit);

  // Deserialize command opcode from flit header
  [[nodiscard]] static DlCommandOp deserialize_command_op(std::span<const std::byte, 3> flit_header);

  // Deserialize ackReqSeq from command flit
  [[nodiscard]] static std::uint16_t deserialize_ack_req_seq(const DlFlit &flit);

  // Clear callbacks
  void clear_callbacks() noexcept;

  // Statistics
  struct Stats {
    std::size_t acks_received{0};
    std::size_t replay_requests_received{0};
    std::size_t acks_sent{0};
    std::size_t replay_requests_sent{0};
  };

  [[nodiscard]] Stats get_stats() const { return stats_; }
  void reset_stats();

private:
  AckCallback ack_callback_{};
  ReplayRequestCallback replay_request_callback_{};
  Stats stats_{};
};

// DL command manager - combines sequence tracking with command generation
class DlAckNakManager {
public:
  DlAckNakManager();

  // Receive side: track received sequence numbers and generate Ack/Replay Request
  // Returns std::nullopt if no command needed, or the command flit to send
  [[nodiscard]] std::optional<DlFlit> process_received_flit(std::uint16_t received_seq, std::uint8_t our_tx_seq_lo);

  // Get expected receive sequence number
  [[nodiscard]] std::uint16_t expected_rx_seq() const noexcept;

  // Reset receive side state
  void reset_rx_state() noexcept;

  // Manually trigger ACK generation
  [[nodiscard]] DlFlit generate_ack(std::uint16_t ack_seq, std::uint8_t flit_seq_lo);

  // Manually trigger Standard Replay Request generation
  [[nodiscard]] DlFlit generate_replay_request(std::uint16_t replay_seq, std::uint8_t flit_seq_lo);

  // Configure ACK policy
  void set_ack_every_n_flits(std::size_t n) noexcept; // 0 = ACK every flit
  [[nodiscard]] std::size_t get_ack_every_n_flits() const noexcept;

private:
  DlSequenceTracker rx_seq_tracker_{};
  std::size_t ack_every_n_{0}; // 0 = ACK immediately, N = ACK every N flits
  std::size_t flits_since_ack_{0};
};

} // namespace ualink::dl
