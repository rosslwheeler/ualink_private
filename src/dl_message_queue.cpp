#include "ualink/dl_message_queue.h"

#include <algorithm>
#include <stdexcept>

namespace ualink::dl {

// Helper: determine which group a message belongs to
MessageGroup DlMessageQueue::get_message_group(const DlMessage &msg) {
  return std::visit(
      [](auto &&m) -> MessageGroup {
        using T = std::decay_t<decltype(m)>;

        // Basic messages
        if constexpr (std::is_same_v<T, NoOpMessage> || std::is_same_v<T, TlRateNotification> ||
                      std::is_same_v<T, DeviceIdMessage> || std::is_same_v<T, PortIdMessage>) {
          return MessageGroup::kBasic;
        }
        // Control messages
        else if constexpr (std::is_same_v<T, ChannelNegotiation>) {
          return MessageGroup::kControl;
        }
        // UART messages
        else if constexpr (std::is_same_v<T, UartStreamResetRequest> || std::is_same_v<T, UartStreamResetResponse> ||
                           std::is_same_v<T, UartStreamTransportMessage> || std::is_same_v<T, UartStreamCreditUpdate>) {
          return MessageGroup::kUart;
        } else {
          return MessageGroup::kNone;
        }
      },
      msg);
}

// Helper: serialize a message to DWord(s)
std::vector<std::array<std::byte, 4>> DlMessageQueue::serialize_message(const DlMessage &msg) {
  UALINK_TRACE_SCOPED(__func__);

  return std::visit(
      [](auto &&m) -> std::vector<std::array<std::byte, 4>> {
        using T = std::decay_t<decltype(m)>;

        if constexpr (std::is_same_v<T, NoOpMessage>) {
          return {serialize_no_op_message(m)};
        } else if constexpr (std::is_same_v<T, TlRateNotification>) {
          return {serialize_tl_rate_notification(m)};
        } else if constexpr (std::is_same_v<T, DeviceIdMessage>) {
          return {serialize_device_id_message(m)};
        } else if constexpr (std::is_same_v<T, PortIdMessage>) {
          return {serialize_port_id_message(m)};
        } else if constexpr (std::is_same_v<T, UartStreamResetRequest>) {
          return {serialize_uart_stream_reset_request(m)};
        } else if constexpr (std::is_same_v<T, UartStreamResetResponse>) {
          return {serialize_uart_stream_reset_response(m)};
        } else if constexpr (std::is_same_v<T, UartStreamTransportMessage>) {
          // Multi-DWord message: header + payload
          std::vector<std::byte> all_bytes = serialize_uart_stream_transport_message(m);
          std::vector<std::array<std::byte, 4>> dwords;

          for (std::size_t i = 0; i < all_bytes.size(); i += 4) {
            std::array<std::byte, 4> dword{};
            std::copy_n(all_bytes.begin() + static_cast<std::ptrdiff_t>(i), 4, dword.begin());
            dwords.push_back(dword);
          }
          return dwords;
        } else if constexpr (std::is_same_v<T, UartStreamCreditUpdate>) {
          return {serialize_uart_stream_credit_update(m)};
        } else if constexpr (std::is_same_v<T, ChannelNegotiation>) {
          return {serialize_channel_negotiation(m)};
        } else {
          throw std::logic_error("DlMessageQueue: unknown message type in variant");
        }
      },
      msg);
}

void DlMessageQueue::enqueue(DlMessage msg) {
  UALINK_TRACE_SCOPED(__func__);

  const MessageGroup group = get_message_group(msg);

  switch (group) {
  case MessageGroup::kBasic:
    basic_queue_.push(std::move(msg));
    stats_.basic_enqueued++;
    break;
  case MessageGroup::kControl:
    control_queue_.push(std::move(msg));
    stats_.control_enqueued++;
    break;
  case MessageGroup::kUart:
    uart_queue_.push(std::move(msg));
    stats_.uart_enqueued++;
    break;
  case MessageGroup::kNone:
    throw std::invalid_argument("DlMessageQueue::enqueue: invalid message group");
  }
}

MessageGroup DlMessageQueue::select_next_group() {
  UALINK_TRACE_SCOPED(__func__);

  // If UART transport in progress, block other messages (spec: "other DL messages shall be blocked")
  if (uart_transport_state_.in_progress) {
    return MessageGroup::kUart;
  }

  // Round-robin: check each group starting after the last served
  const std::array<MessageGroup, 3> group_order = {MessageGroup::kBasic, MessageGroup::kControl, MessageGroup::kUart};

  // Find starting point (after last served)
  std::size_t start_index = 0;
  if (last_served_group_ != MessageGroup::kNone) {
    for (std::size_t i = 0; i < group_order.size(); ++i) {
      if (group_order[i] == last_served_group_) {
        start_index = (i + 1) % group_order.size();
        break;
      }
    }
  }

  // Check each group in round-robin order
  for (std::size_t i = 0; i < group_order.size(); ++i) {
    const std::size_t index = (start_index + i) % group_order.size();
    const MessageGroup group = group_order[index];

    bool has_messages = false;
    switch (group) {
    case MessageGroup::kBasic:
      has_messages = !basic_queue_.empty();
      break;
    case MessageGroup::kControl:
      has_messages = !control_queue_.empty();
      break;
    case MessageGroup::kUart:
      has_messages = !uart_queue_.empty();
      break;
    case MessageGroup::kNone:
      break;
    }

    if (has_messages) {
      return group;
    }
  }

  return MessageGroup::kNone;
}

std::optional<DlMessage> DlMessageQueue::pop_from_group(MessageGroup group) {
  UALINK_TRACE_SCOPED(__func__);

  switch (group) {
  case MessageGroup::kBasic:
    if (!basic_queue_.empty()) {
      DlMessage msg = std::move(basic_queue_.front());
      basic_queue_.pop();
      return msg;
    }
    break;
  case MessageGroup::kControl:
    if (!control_queue_.empty()) {
      DlMessage msg = std::move(control_queue_.front());
      control_queue_.pop();
      return msg;
    }
    break;
  case MessageGroup::kUart:
    if (!uart_queue_.empty()) {
      DlMessage msg = std::move(uart_queue_.front());
      uart_queue_.pop();
      return msg;
    }
    break;
  case MessageGroup::kNone:
    break;
  }

  return std::nullopt;
}

std::optional<std::array<std::byte, 4>> DlMessageQueue::pop_next_dword() {
  UALINK_TRACE_SCOPED(__func__);

  // Step 1: If UART transport in progress, return next DWord from remaining payload
  if (uart_transport_state_.in_progress) {
    if (!uart_transport_state_.remaining_dwords.empty()) {
      const std::uint32_t dword_value = uart_transport_state_.remaining_dwords.front();
      uart_transport_state_.remaining_dwords.erase(uart_transport_state_.remaining_dwords.begin());

      // Convert uint32 to byte array (big-endian)
      std::array<std::byte, 4> dword{};
      dword[0] = std::byte{static_cast<unsigned char>((dword_value >> 24) & 0xFFU)};
      dword[1] = std::byte{static_cast<unsigned char>((dword_value >> 16) & 0xFFU)};
      dword[2] = std::byte{static_cast<unsigned char>((dword_value >> 8) & 0xFFU)};
      dword[3] = std::byte{static_cast<unsigned char>(dword_value & 0xFFU)};

      // If no more payload, clear in_progress flag
      if (uart_transport_state_.remaining_dwords.empty()) {
        uart_transport_state_.in_progress = false;
      }

      return dword;
    } else {
      // Should not happen (in_progress but no remaining DWords)
      uart_transport_state_.in_progress = false;
    }
  }

  // Step 2: Select next group using round-robin
  const MessageGroup group = select_next_group();
  if (group == MessageGroup::kNone) {
    return std::nullopt;
  }

  // Step 3: Pop message from selected group
  std::optional<DlMessage> msg_opt = pop_from_group(group);
  if (!msg_opt.has_value()) {
    return std::nullopt;
  }

  // Step 4: Serialize message to DWord(s)
  std::vector<std::array<std::byte, 4>> dwords = serialize_message(msg_opt.value());
  if (dwords.empty()) {
    return std::nullopt;
  }

  // Step 5: If multi-DWord (UART Stream Transport), save remaining DWords for later
  if (dwords.size() > 1) {
    // Extract payload DWords (skip header DWord)
    for (std::size_t i = 1; i < dwords.size(); ++i) {
      const auto &dword = dwords[i];
      const std::uint32_t dword_value = (static_cast<std::uint32_t>(std::to_integer<unsigned char>(dword[0])) << 24) |
                                        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(dword[1])) << 16) |
                                        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(dword[2])) << 8) |
                                        static_cast<std::uint32_t>(std::to_integer<unsigned char>(dword[3]));
      uart_transport_state_.remaining_dwords.push_back(dword_value);
    }
    uart_transport_state_.in_progress = true;
    stats_.uart_multi_flit_count++;
  }

  // Step 6: Update stats and last served group
  switch (group) {
  case MessageGroup::kBasic:
    stats_.basic_sent++;
    break;
  case MessageGroup::kControl:
    stats_.control_sent++;
    break;
  case MessageGroup::kUart:
    stats_.uart_sent++;
    break;
  case MessageGroup::kNone:
    break;
  }

  last_served_group_ = group;

  // Step 7: Return first DWord
  return dwords[0];
}

bool DlMessageQueue::has_pending_messages() const {
  return !basic_queue_.empty() || !control_queue_.empty() || !uart_queue_.empty() || uart_transport_state_.in_progress;
}

void DlMessageQueue::reset_stats() {
  stats_ = Stats{};
}

} // namespace ualink::dl