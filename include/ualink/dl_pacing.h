#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "ualink/trace.h"

namespace ualink::dl {

// Pacing decision - whether to allow or throttle transmission
enum class PacingDecision {
  kAllow,     // Transmission allowed
  kThrottle,  // Transmission should be delayed
  kDrop,      // Transmission should be dropped (buffer full, etc.)
};

// Tx pacing callback - called before packing a flit
// Parameters: (flit_count, total_bytes)
// Returns: PacingDecision
using TxPacingCallback = std::function<PacingDecision(std::size_t, std::size_t)>;

// Rx rate adaptation callback - called after unpacking a flit
// Parameters: (flit_count, total_bytes, crc_valid)
// Can be used to signal backpressure or adjust receiver processing
using RxRateCallback = std::function<void(std::size_t, std::size_t, bool)>;

// Pacing controller for modeling flow control and rate limits
class DlPacingController {
public:
  DlPacingController();

  // Set Tx pacing callback
  void set_tx_callback(TxPacingCallback callback) noexcept;

  // Set Rx rate callback
  void set_rx_callback(RxRateCallback callback) noexcept;

  // Check if transmission is allowed (Tx pacing)
  [[nodiscard]] PacingDecision check_tx_pacing(std::size_t flit_count,
                                                 std::size_t total_bytes) const;

  // Notify of received flit (Rx rate adaptation)
  void notify_rx(std::size_t flit_count, std::size_t total_bytes, bool crc_valid) const;

  // Clear callbacks
  void clear_callbacks() noexcept;

  // Get whether Tx callback is set
  [[nodiscard]] bool has_tx_callback() const noexcept;

  // Get whether Rx callback is set
  [[nodiscard]] bool has_rx_callback() const noexcept;

private:
  TxPacingCallback tx_callback_{};
  RxRateCallback rx_callback_{};
};

// Built-in pacing policies for common scenarios

// Simple rate limiter - allows N flits per window
class SimpleTxRateLimiter {
public:
  explicit SimpleTxRateLimiter(std::size_t max_flits_per_window);

  [[nodiscard]] PacingDecision operator()(std::size_t flit_count,
                                           std::size_t total_bytes);

  // Reset the window (e.g., on timer tick)
  void reset_window() noexcept;

  // Get current window count
  [[nodiscard]] std::size_t window_count() const noexcept;

private:
  std::size_t max_flits_per_window_{0};
  std::size_t current_window_count_{0};
};

// Byte-based rate limiter - allows N bytes per window
class ByteBasedRateLimiter {
public:
  explicit ByteBasedRateLimiter(std::size_t max_bytes_per_window);

  [[nodiscard]] PacingDecision operator()(std::size_t flit_count,
                                           std::size_t total_bytes);

  // Reset the window
  void reset_window() noexcept;

  // Get current window bytes
  [[nodiscard]] std::size_t window_bytes() const noexcept;

private:
  std::size_t max_bytes_per_window_{0};
  std::size_t current_window_bytes_{0};
};

// Rx backpressure tracker - tracks receive rate and signals backpressure
class RxBackpressureTracker {
public:
  explicit RxBackpressureTracker(std::size_t buffer_capacity);

  void operator()(std::size_t flit_count, std::size_t total_bytes, bool crc_valid);

  // Check if backpressure should be signaled
  [[nodiscard]] bool should_signal_backpressure() const noexcept;

  // Consume flits from buffer (simulates processing)
  void consume_flits(std::size_t count) noexcept;

  // Get current buffer occupancy
  [[nodiscard]] std::size_t buffer_occupancy() const noexcept;

  // Reset state
  void reset() noexcept;

private:
  std::size_t buffer_capacity_{0};
  std::size_t current_occupancy_{0};
  std::size_t backpressure_threshold_{0};
};

}  // namespace ualink::dl
