#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <variant>
#include <vector>

#include "ualink/dl_messages.h"
#include "ualink/trace.h"

namespace ualink::dl {

// =============================================================================
// DlMessageQueue: Queuing + Round-robin Arbitration for Outbound DL Messages
// =============================================================================

// Discriminated union for all message types
using DlMessage = std::variant<NoOpMessage, TlRateNotification, DeviceIdMessage, PortIdMessage, UartStreamResetRequest,
                                UartStreamResetResponse, UartStreamTransportMessage, UartStreamCreditUpdate,
                                ChannelNegotiation>;

enum class MessageGroup {
  kBasic,
  kControl,
  kUart,
  kNone // No messages available
};

class DlMessageQueue {
public:
  // Enqueue a message (determines group automatically)
  void enqueue(DlMessage msg);

  // Pop next DWord using round-robin arbitration
  // Returns nullopt if no messages available
  std::optional<std::array<std::byte, 4>> pop_next_dword();

  // Check if any messages are pending
  [[nodiscard]] bool has_pending_messages() const;

  // Statistics
  struct Stats {
    std::size_t basic_enqueued{0};
    std::size_t control_enqueued{0};
    std::size_t uart_enqueued{0};
    std::size_t basic_sent{0};
    std::size_t control_sent{0};
    std::size_t uart_sent{0};
    std::size_t uart_multi_flit_count{0};
  };
  [[nodiscard]] Stats get_stats() const { return stats_; }
  void reset_stats();

private:
  std::queue<DlMessage> basic_queue_;
  std::queue<DlMessage> control_queue_;
  std::queue<DlMessage> uart_queue_;

  // Round-robin state: which group was last serviced
  MessageGroup last_served_group_{MessageGroup::kNone};

  // UART Stream Transport multi-DWord state
  struct UartTransportState {
    std::vector<std::uint32_t> remaining_dwords; // Payload DWords not yet sent
    bool in_progress{false};
  };
  UartTransportState uart_transport_state_;

  Stats stats_;

  // Helper: determine which group a message belongs to
  [[nodiscard]] static MessageGroup get_message_group(const DlMessage &msg);

  // Helper: get next group to service (round-robin)
  [[nodiscard]] MessageGroup select_next_group();

  // Helper: pop message from specific group queue
  [[nodiscard]] std::optional<DlMessage> pop_from_group(MessageGroup group);

  // Helper: serialize a message to DWord(s)
  [[nodiscard]] static std::vector<std::array<std::byte, 4>> serialize_message(const DlMessage &msg);
};

} // namespace ualink::dl