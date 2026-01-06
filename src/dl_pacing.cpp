#include "ualink/dl_pacing.h"

#include <algorithm>

using namespace ualink::dl;

DlPacingController::DlPacingController() {
  UALINK_TRACE_SCOPED(__func__);
}

void DlPacingController::set_tx_callback(TxPacingCallback callback) noexcept {
  tx_callback_ = std::move(callback);
}

void DlPacingController::set_rx_callback(RxRateCallback callback) noexcept {
  rx_callback_ = std::move(callback);
}

PacingDecision DlPacingController::check_tx_pacing(std::size_t flit_count,
                                                    std::size_t total_bytes) const {
  UALINK_TRACE_SCOPED(__func__);
  if (tx_callback_) {
    return tx_callback_(flit_count, total_bytes);
  }
  return PacingDecision::kAllow;
}

void DlPacingController::notify_rx(std::size_t flit_count,
                                    std::size_t total_bytes,
                                    bool crc_valid) const {
  UALINK_TRACE_SCOPED(__func__);
  if (rx_callback_) {
    rx_callback_(flit_count, total_bytes, crc_valid);
  }
}

void DlPacingController::clear_callbacks() noexcept {
  tx_callback_ = nullptr;
  rx_callback_ = nullptr;
}

bool DlPacingController::has_tx_callback() const noexcept {
  return static_cast<bool>(tx_callback_);
}

bool DlPacingController::has_rx_callback() const noexcept {
  return static_cast<bool>(rx_callback_);
}

SimpleTxRateLimiter::SimpleTxRateLimiter(std::size_t max_flits_per_window)
    : max_flits_per_window_(max_flits_per_window) {
  UALINK_TRACE_SCOPED(__func__);
}

PacingDecision SimpleTxRateLimiter::operator()(std::size_t flit_count,
                                                std::size_t /* total_bytes */) {
  UALINK_TRACE_SCOPED(__func__);

  if (current_window_count_ + flit_count > max_flits_per_window_) {
    return PacingDecision::kThrottle;
  }

  current_window_count_ += flit_count;
  return PacingDecision::kAllow;
}

void SimpleTxRateLimiter::reset_window() noexcept {
  current_window_count_ = 0;
}

std::size_t SimpleTxRateLimiter::window_count() const noexcept {
  return current_window_count_;
}

ByteBasedRateLimiter::ByteBasedRateLimiter(std::size_t max_bytes_per_window)
    : max_bytes_per_window_(max_bytes_per_window) {
  UALINK_TRACE_SCOPED(__func__);
}

PacingDecision ByteBasedRateLimiter::operator()(std::size_t /* flit_count */,
                                                 std::size_t total_bytes) {
  UALINK_TRACE_SCOPED(__func__);

  if (current_window_bytes_ + total_bytes > max_bytes_per_window_) {
    return PacingDecision::kThrottle;
  }

  current_window_bytes_ += total_bytes;
  return PacingDecision::kAllow;
}

void ByteBasedRateLimiter::reset_window() noexcept {
  current_window_bytes_ = 0;
}

std::size_t ByteBasedRateLimiter::window_bytes() const noexcept {
  return current_window_bytes_;
}

RxBackpressureTracker::RxBackpressureTracker(std::size_t buffer_capacity)
    : buffer_capacity_(buffer_capacity),
      backpressure_threshold_((buffer_capacity * 3) / 4) {
  UALINK_TRACE_SCOPED(__func__);
}

void RxBackpressureTracker::operator()(std::size_t flit_count,
                                        std::size_t /* total_bytes */,
                                        bool /* crc_valid */) {
  UALINK_TRACE_SCOPED(__func__);

  current_occupancy_ = std::min(current_occupancy_ + flit_count, buffer_capacity_);
}

bool RxBackpressureTracker::should_signal_backpressure() const noexcept {
  return current_occupancy_ >= backpressure_threshold_;
}

void RxBackpressureTracker::consume_flits(std::size_t count) noexcept {
  if (count > current_occupancy_) {
    current_occupancy_ = 0;
  } else {
    current_occupancy_ -= count;
  }
}

std::size_t RxBackpressureTracker::buffer_occupancy() const noexcept {
  return current_occupancy_;
}

void RxBackpressureTracker::reset() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  current_occupancy_ = 0;
}
