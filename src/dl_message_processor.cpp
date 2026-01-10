#include "ualink/dl_message_processor.h"

#include <stdexcept>

namespace ualink::dl {

bool DlMessageProcessor::process_dword(const std::array<std::byte, 4>& dword, std::uint64_t current_time_us) {
  UALINK_TRACE_SCOPED(__func__);

  // Extract mclass field (bits 31:30) to determine message group
  const std::uint8_t mclass = (static_cast<std::uint8_t>(dword[0]) >> 6) & 0x3U;

  try {
    switch (mclass) {
    case 0b00: // Basic messages
      dispatch_basic_message(dword);
      stats_.basic_received++;
      break;

    case 0b01: // Control messages
      dispatch_control_message(dword);
      stats_.control_received++;
      break;

    case 0b10: // UART messages
      dispatch_uart_message(dword);
      stats_.uart_received++;
      break;

    case 0b11: // Reserved
      stats_.deserialization_errors++;
      return false;
    }

    return true;

  } catch (const std::exception&) {
    stats_.deserialization_errors++;
    return false;
  }
}

void DlMessageProcessor::dispatch_basic_message(const std::array<std::byte, 4>& dword) {
  UALINK_TRACE_SCOPED(__func__);

  // Extract mtype field (bits 29:27) to determine specific message type
  const std::uint8_t mtype = (static_cast<std::uint8_t>(dword[0]) >> 3) & 0x7U;

  switch (mtype) {
  case 0b000: { // NoOp
    auto msg_opt = deserialize_no_op_message(dword);
    if (msg_opt.has_value() && noop_callback_) {
      noop_callback_(msg_opt.value());
    }
    break;
  }

  case 0b100: { // TL Rate Notification
    auto msg_opt = deserialize_tl_rate_notification(dword);
    if (msg_opt.has_value() && tl_rate_callback_) {
      tl_rate_callback_(msg_opt.value());
      // Cancel timeout if this is a response
      if (basic_timeout_.waiting_for_response) {
        cancel_basic_timeout();
      }
    }
    break;
  }

  case 0b101: { // Device ID Request/Response
    auto msg_opt = deserialize_device_id_message(dword);
    if (msg_opt.has_value() && device_id_callback_) {
      device_id_callback_(msg_opt.value());
      // Cancel timeout if this is a response (ack bit set)
      if (msg_opt->ack && basic_timeout_.waiting_for_response) {
        cancel_basic_timeout();
      }
    }
    break;
  }

  case 0b110: { // Port ID Request/Response
    auto msg_opt = deserialize_port_id_message(dword);
    if (msg_opt.has_value() && port_id_callback_) {
      port_id_callback_(msg_opt.value());
      // Cancel timeout if this is a response (ack bit set)
      if (msg_opt->ack && basic_timeout_.waiting_for_response) {
        cancel_basic_timeout();
      }
    }
    break;
  }

  default:
    // Unknown message type
    throw std::runtime_error("Unknown basic message type");
  }
}

void DlMessageProcessor::dispatch_control_message(const std::array<std::byte, 4>& dword) {
  UALINK_TRACE_SCOPED(__func__);

  // Extract mtype field (bits 29:27)
  const std::uint8_t mtype = (static_cast<std::uint8_t>(dword[0]) >> 3) & 0x7U;

  if (mtype == 0b100) { // Channel Online/Offline Negotiation
    auto msg_opt = deserialize_channel_negotiation(dword);
    if (msg_opt.has_value() && control_callback_) {
      control_callback_(msg_opt.value());

      // Update channel state machine based on received command
      const std::uint8_t command = msg_opt->channel_command;
      switch (command) {
      case 0b0000: // Request
        if (channel_state_.state == ChannelState::kOffline) {
          // Received request while offline - will respond with Ack
          channel_state_.state = ChannelState::kRequestSent;
        }
        break;

      case 0b0001: // Ack
        if (channel_state_.state == ChannelState::kRequestSent) {
          // Our request was acknowledged
          channel_state_.state = ChannelState::kOnline;
        }
        break;

      case 0b0010: // NAck
        if (channel_state_.state == ChannelState::kRequestSent) {
          // Our request was rejected
          channel_state_.state = ChannelState::kOffline;
        }
        break;

      case 0b0011: // Pending
        // Request is still pending, no state change
        break;

      default:
        // Unknown command
        break;
      }
    }
  } else {
    throw std::runtime_error("Unknown control message type");
  }
}

void DlMessageProcessor::dispatch_uart_message(const std::array<std::byte, 4>& dword) {
  UALINK_TRACE_SCOPED(__func__);

  // Extract mtype field (bits 29:27)
  const std::uint8_t mtype = (static_cast<std::uint8_t>(dword[0]) >> 3) & 0x7U;

  switch (mtype) {
  case 0b000: { // Stream Reset Request
    auto msg_opt = deserialize_uart_stream_reset_request(dword);
    if (msg_opt.has_value() && uart_reset_req_callback_) {
      uart_reset_req_callback_(msg_opt.value());
    }
    break;
  }

  case 0b001: { // Stream Reset Response
    auto msg_opt = deserialize_uart_stream_reset_response(dword);
    if (msg_opt.has_value() && uart_reset_rsp_callback_) {
      uart_reset_rsp_callback_(msg_opt.value());
    }
    break;
  }

  case 0b010: { // Stream Transport Message
    // UART Stream Transport is multi-DWord - requires reassembly
    // The header DWord contains stream_id in bits 26:24
    const std::uint8_t stream_id = (static_cast<std::uint8_t>(dword[0]) >> 0) & 0x7U;

    if (!uart_reassembly_.in_progress) {
      // Start new reassembly
      uart_reassembly_.stream_id = stream_id;
      uart_reassembly_.accumulated_dwords.clear();
      uart_reassembly_.in_progress = true;
    }
    // Note: Full reassembly and callback invocation happens when complete message is received
    // This is simplified for now - full implementation would accumulate payload DWords
    break;
  }

  case 0b011: { // Stream Credit Update
    auto msg_opt = deserialize_uart_stream_credit_update(dword);
    if (msg_opt.has_value() && uart_credit_callback_) {
      uart_credit_callback_(msg_opt.value());
    }
    break;
  }

  default:
    throw std::runtime_error("Unknown UART message type");
  }

  // Handle UART Stream Transport payload DWords (accumulated after header)
  if (uart_reassembly_.in_progress && mtype != 0b010) {
    // If we're in reassembly mode but received non-transport message,
    // assume transport is complete
    if (!uart_reassembly_.accumulated_dwords.empty() && uart_transport_callback_) {
      UartStreamTransportMessage complete_msg{};
      complete_msg.stream_id = uart_reassembly_.stream_id;
      complete_msg.payload_dwords = uart_reassembly_.accumulated_dwords;
      complete_msg.common = make_common(DlUartMessageType::kStreamTransportMessage);

      uart_transport_callback_(complete_msg);
      reset_uart_reassembly();
    }
  }
}

void DlMessageProcessor::start_basic_timeout(std::uint16_t sequence_id, std::uint64_t current_time_us) {
  UALINK_TRACE_SCOPED(__func__);
  basic_timeout_.request_time_us = current_time_us;
  basic_timeout_.sequence_id = sequence_id;
  basic_timeout_.waiting_for_response = true;
}

TimeoutResult DlMessageProcessor::check_basic_timeout(std::uint64_t current_time_us, std::uint64_t timeout_us) {
  UALINK_TRACE_SCOPED(__func__);

  if (!basic_timeout_.waiting_for_response) {
    return TimeoutResult::kNoTimeout;
  }

  const std::uint64_t elapsed_us = current_time_us - basic_timeout_.request_time_us;
  if (elapsed_us >= timeout_us) {
    stats_.timeouts++;
    basic_timeout_.waiting_for_response = false;
    return TimeoutResult::kTimeoutExpired;
  }

  return TimeoutResult::kNoTimeout;
}

void DlMessageProcessor::cancel_basic_timeout() {
  UALINK_TRACE_SCOPED(__func__);
  basic_timeout_.waiting_for_response = false;
}

void DlMessageProcessor::transition_channel_state(ChannelState new_state, std::uint64_t current_time_us) {
  UALINK_TRACE_SCOPED(__func__);
  channel_state_.state = new_state;
  channel_state_.last_request_time_us = current_time_us;
}

void DlMessageProcessor::reset_uart_reassembly() {
  UALINK_TRACE_SCOPED(__func__);
  uart_reassembly_.in_progress = false;
  uart_reassembly_.accumulated_dwords.clear();
}

void DlMessageProcessor::reset_stats() {
  UALINK_TRACE_SCOPED(__func__);
  stats_ = Stats{};
}

} // namespace ualink::dl
