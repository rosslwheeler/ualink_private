#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "ualink/dl_messages.h"
#include "ualink/trace.h"

namespace ualink::dl {

// =============================================================================
// DlMessageProcessor: Higher-level protocol handling for received DL messages
// =============================================================================
//
// Responsibilities:
// - Deserialize raw DWords into typed messages
// - Track request/response timeouts (1µs requirement for Basic messages)
// - Manage channel negotiation state machine (Control messages)
// - Reassemble multi-DWord UART Stream Transport messages
// - Dispatch to type-specific callbacks
//
// Usage:
//   DlMessageProcessor processor;
//   processor.set_basic_callback([](const auto& msg) { /* handle */ });
//
//   // After receiving DL flit:
//   auto result = DlDeserializer::deserialize_ex(flit);
//   for (const auto& dword : result.dl_message_dwords) {
//     processor.process_dword(dword, current_time_us);
//   }

// =============================================================================
// Timeout Tracking for Basic Messages (Request/Response)
// =============================================================================

struct BasicMessageTimeout {
  std::uint64_t request_time_us{0};
  std::uint16_t sequence_id{0};  // User-defined identifier for matching responses
  bool waiting_for_response{false};
};

enum class TimeoutResult {
  kNoTimeout,
  kTimeoutExpired,
};

// =============================================================================
// Channel Negotiation State Machine (Control Messages)
// =============================================================================

enum class ChannelState {
  kOffline,
  kRequestSent,
  kOnline,
  kOfflineRequested,
};

struct ChannelNegotiationState {
  ChannelState state{ChannelState::kOffline};
  std::uint64_t last_request_time_us{0};
  std::uint8_t pending_command{0};  // Last command sent (Request/Ack/NAck/Pending)
};

// =============================================================================
// UART Stream Reassembly (Multi-DWord Messages)
// =============================================================================

struct UartStreamReassembly {
  std::uint8_t stream_id{0};
  std::vector<std::uint32_t> accumulated_dwords;
  bool in_progress{false};
};

// =============================================================================
// DlMessageProcessor
// =============================================================================

class DlMessageProcessor {
public:
  DlMessageProcessor() = default;

  // Type-specific callbacks
  using BasicCallback = std::function<void(const NoOpMessage&)>;
  using TlRateCallback = std::function<void(const TlRateNotification&)>;
  using DeviceIdCallback = std::function<void(const DeviceIdMessage&)>;
  using PortIdCallback = std::function<void(const PortIdMessage&)>;
  using ControlCallback = std::function<void(const ChannelNegotiation&)>;
  using UartResetReqCallback = std::function<void(const UartStreamResetRequest&)>;
  using UartResetRspCallback = std::function<void(const UartStreamResetResponse&)>;
  using UartTransportCallback = std::function<void(const UartStreamTransportMessage&)>;
  using UartCreditCallback = std::function<void(const UartStreamCreditUpdate&)>;

  // Set callbacks
  void set_noop_callback(BasicCallback callback) { noop_callback_ = std::move(callback); }
  void set_tl_rate_callback(TlRateCallback callback) { tl_rate_callback_ = std::move(callback); }
  void set_device_id_callback(DeviceIdCallback callback) { device_id_callback_ = std::move(callback); }
  void set_port_id_callback(PortIdCallback callback) { port_id_callback_ = std::move(callback); }
  void set_control_callback(ControlCallback callback) { control_callback_ = std::move(callback); }
  void set_uart_reset_req_callback(UartResetReqCallback callback) { uart_reset_req_callback_ = std::move(callback); }
  void set_uart_reset_rsp_callback(UartResetRspCallback callback) { uart_reset_rsp_callback_ = std::move(callback); }
  void set_uart_transport_callback(UartTransportCallback callback) { uart_transport_callback_ = std::move(callback); }
  void set_uart_credit_callback(UartCreditCallback callback) { uart_credit_callback_ = std::move(callback); }

  // Process a received DWord
  // Returns true if successfully processed, false if deserialization failed
  bool process_dword(const std::array<std::byte, 4>& dword, std::uint64_t current_time_us);

  // Timeout tracking for Basic messages
  void start_basic_timeout(std::uint16_t sequence_id, std::uint64_t current_time_us);
  TimeoutResult check_basic_timeout(std::uint64_t current_time_us, std::uint64_t timeout_us = 1);
  void cancel_basic_timeout();

  // Channel negotiation state machine
  [[nodiscard]] ChannelState get_channel_state() const { return channel_state_.state; }
  void transition_channel_state(ChannelState new_state, std::uint64_t current_time_us);

  // UART stream reassembly
  [[nodiscard]] bool is_uart_reassembly_in_progress() const { return uart_reassembly_.in_progress; }
  void reset_uart_reassembly();

  // Statistics
  struct Stats {
    std::size_t basic_received{0};
    std::size_t control_received{0};
    std::size_t uart_received{0};
    std::size_t deserialization_errors{0};
    std::size_t timeouts{0};
  };
  [[nodiscard]] Stats get_stats() const { return stats_; }
  void reset_stats();

private:
  // Callbacks
  BasicCallback noop_callback_;
  TlRateCallback tl_rate_callback_;
  DeviceIdCallback device_id_callback_;
  PortIdCallback port_id_callback_;
  ControlCallback control_callback_;
  UartResetReqCallback uart_reset_req_callback_;
  UartResetRspCallback uart_reset_rsp_callback_;
  UartTransportCallback uart_transport_callback_;
  UartCreditCallback uart_credit_callback_;

  // State tracking
  BasicMessageTimeout basic_timeout_;
  ChannelNegotiationState channel_state_;
  UartStreamReassembly uart_reassembly_;
  Stats stats_;

  // Helper: dispatch to appropriate callback based on message type
  void dispatch_basic_message(const std::array<std::byte, 4>& dword);
  void dispatch_control_message(const std::array<std::byte, 4>& dword);
  void dispatch_uart_message(const std::array<std::byte, 4>& dword);
};

} // namespace ualink::dl
