#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <vector>

#include "ualink/dl_command.h"
#include "ualink/dl_error_injection.h"
#include "ualink/dl_flit.h"
#include "ualink/dl_pacing.h"
#include "ualink/dl_replay.h"
#include "ualink/tl_flit.h"
#include "ualink/trace.h"

namespace ualink {

// Transaction completion callback types
using ReadCompletionCallback = std::function<void(std::uint16_t tag, std::uint8_t status, const std::vector<std::byte>& data)>;
using WriteCompletionCallback = std::function<void(std::uint16_t tag, std::uint8_t status)>;

// Transmit callback - called when DL flit is ready to send on wire
using TransmitCallback = std::function<void(const dl::DlFlit& flit)>;

// Configuration for UaLinkEndpoint
struct EndpointConfig {
  // Pacing configuration (optional - nullptr means no pacing)
  dl::TxPacingCallback tx_pacing_callback{nullptr};
  dl::RxRateCallback rx_rate_callback{nullptr};

  // Error injection (optional - nullptr means no error injection)
  std::function<dl::ErrorType()> error_policy{nullptr};

  // Enable CRC checking on receive
  bool enable_crc_check{true};

  // Enable automatic ACK/NAK processing
  bool enable_ack_nak{true};

  // ACK policy: 0 = ACK every flit, N = ACK every N flits
  std::size_t ack_every_n_flits{0};
};

// High-level endpoint for UaLink protocol stack
// Provides simple API for applications: send_read_request(), send_write_request()
// Automatically handles TL→DL serialization, replay buffering, pacing, and error injection
class UaLinkEndpoint {
public:
  explicit UaLinkEndpoint(const EndpointConfig& config = EndpointConfig{});

  // === Transmit API ===

  // Send a read request
  // Returns: transaction tag assigned to this request (for matching with completion)
  [[nodiscard]] std::uint16_t send_read_request(std::uint64_t address, std::uint8_t size);

  // Send a write request
  // Returns: transaction tag assigned to this request
  [[nodiscard]] std::uint16_t send_write_request(std::uint64_t address, std::uint8_t size, const std::vector<std::byte>& data);

  // Set transmit callback - must be set before calling send_*
  void set_transmit_callback(TransmitCallback callback);

  // === Receive API ===

  // Receive a DL flit from the wire
  // Automatically deserializes DL→TL, checks CRC, applies pacing
  // Triggers completion callbacks for matching transactions
  void receive_flit(const dl::DlFlit& flit);

  // Set completion callbacks
  void set_read_completion_callback(ReadCompletionCallback callback);
  void set_write_completion_callback(WriteCompletionCallback callback);

  // === Replay Buffer Management ===

  // Process ACK - removes acknowledged flits from replay buffer
  void process_ack(std::uint16_t ack_seq);

  // Replay flits starting from sequence number (for NACK handling)
  void replay_from(std::uint16_t seq);

  // Get current transmit sequence number
  [[nodiscard]] std::uint16_t get_tx_seq() const { return tx_seq_; }

  // === Statistics ===

  struct Stats {
    std::size_t tx_read_requests{0};
    std::size_t tx_write_requests{0};
    std::size_t tx_dl_flits{0};
    std::size_t tx_dropped_by_pacing{0};
    std::size_t tx_dropped_by_error_injection{0};
    std::size_t tx_acks_sent{0};
    std::size_t tx_naks_sent{0};

    std::size_t rx_read_responses{0};
    std::size_t rx_write_completions{0};
    std::size_t rx_dl_flits{0};
    std::size_t rx_crc_errors{0};
    std::size_t rx_flits_with_pacing{0};
    std::size_t rx_acks_received{0};
    std::size_t rx_naks_received{0};

    std::size_t replay_buffer_size{0};
    std::size_t retransmissions{0};
  };

  [[nodiscard]] Stats get_stats() const { return stats_; }
  void reset_stats();

  // === Testing Support ===

  // Enable/disable error injection
  void enable_error_injection();
  void disable_error_injection();
  void set_error_policy(std::function<dl::ErrorType()> policy);

  // Pacing control
  void set_tx_pacing_callback(dl::TxPacingCallback callback);
  void set_rx_rate_callback(dl::RxRateCallback callback);
  void clear_pacing_callbacks();

private:
  // Internal state
  std::uint16_t tx_seq_{0};       // Current transmit sequence number
  std::uint16_t next_tag_{0};     // Next transaction tag to assign

  // Components
  dl::DlReplayBuffer replay_buffer_;
  dl::DlPacingController pacing_controller_;
  dl::DlErrorInjector error_injector_;
  dl::DlCommandProcessor command_processor_;
  dl::DlAckNakManager ack_nak_manager_;

  // Configuration
  bool enable_crc_check_{true};
  bool enable_ack_nak_{true};

  // Callbacks
  TransmitCallback transmit_callback_;
  ReadCompletionCallback read_completion_callback_;
  WriteCompletionCallback write_completion_callback_;

  // Statistics
  Stats stats_;

  // Helper methods
  void transmit_tl_flits(const std::vector<dl::TlFlit>& tl_flits);
  void handle_tl_flit(const dl::TlFlit& tl_flit);
  std::uint16_t allocate_tag();
};

}  // namespace ualink
