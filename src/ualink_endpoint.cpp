#include "ualink/ualink_endpoint.h"

#include <algorithm>
#include <stdexcept>

using namespace ualink;
using namespace ualink::tl;
using namespace ualink::dl;

UaLinkEndpoint::UaLinkEndpoint(const EndpointConfig &config)
    : enable_crc_check_(config.enable_crc_check), enable_ack_nak_(config.enable_ack_nak) {
  UALINK_TRACE_SCOPED(__func__);

  // Configure pacing if provided
  if (config.tx_pacing_callback) {
    pacing_controller_.set_tx_callback(config.tx_pacing_callback);
  }
  if (config.rx_rate_callback) {
    pacing_controller_.set_rx_callback(config.rx_rate_callback);
  }

  // Configure error injection if provided
  if (config.error_policy) {
    error_injector_.enable();
    error_injector_.set_policy(config.error_policy);
  }

  // Configure ACK/NAK if enabled
  if (enable_ack_nak_) {
    ack_nak_manager_.set_ack_every_n_flits(config.ack_every_n_flits);

    // Set up command processor callbacks
    command_processor_.set_ack_callback([this](std::uint16_t ack_seq) {
      process_ack(ack_seq);
      stats_.rx_acks_received++;
    });

    command_processor_.set_replay_request_callback([this](std::uint16_t replay_seq) {
      replay_from(replay_seq);
      stats_.rx_replay_requests_received++;
      stats_.retransmissions++;
    });
  }
}

std::uint16_t UaLinkEndpoint::send_read_request(std::uint64_t address, std::uint8_t size) {
  UALINK_TRACE_SCOPED(__func__);

  if (!transmit_callback_) {
    throw std::logic_error("send_read_request: transmit_callback not set");
  }

  // Allocate transaction tag
  const std::uint16_t tag = allocate_tag();

  // Build TL read request
  TlReadRequest request{};
  request.header.opcode = TlOpcode::kReadRequest;
  request.header.half_flit = false;
  request.header.size = size;
  request.header.tag = tag;
  request.header.address = address;

  // Serialize to TL flit
  const auto tl_flit_bytes = TlSerializer::serialize_read_request(request);

  // Convert to TlFlit structure (copies the bytes into TlFlit.data)
  TlFlit tl_flit{};
  std::copy_n(tl_flit_bytes.begin(), tl::kTlFlitBytes, tl_flit.data.begin());
  tl_flit.message_field = static_cast<std::uint8_t>(TlMessageType::kNone);

  // Transmit
  transmit_tl_flits({tl_flit});

  stats_.tx_read_requests++;

  return tag;
}

std::uint16_t UaLinkEndpoint::send_write_request(std::uint64_t address, std::uint8_t size, const std::vector<std::byte> &data) {
  UALINK_TRACE_SCOPED(__func__);

  if (!transmit_callback_) {
    throw std::logic_error("send_write_request: transmit_callback not set");
  }

  if (data.size() > 56) {
    throw std::invalid_argument("send_write_request: data size exceeds 56 bytes");
  }

  // Allocate transaction tag
  const std::uint16_t tag = allocate_tag();

  // Build TL write request
  TlWriteRequest request{};
  request.header.opcode = TlOpcode::kWriteRequest;
  request.header.half_flit = false;
  request.header.size = size;
  request.header.tag = tag;
  request.header.address = address;

  // Copy data
  std::copy(data.begin(), data.end(), request.data.begin());

  // Serialize to TL flit
  const auto tl_flit_bytes = TlSerializer::serialize_write_request(request);

  // Convert to TlFlit structure
  TlFlit tl_flit{};
  std::copy_n(tl_flit_bytes.begin(), tl::kTlFlitBytes, tl_flit.data.begin());
  tl_flit.message_field = static_cast<std::uint8_t>(TlMessageType::kNone);

  // Transmit
  transmit_tl_flits({tl_flit});

  stats_.tx_write_requests++;

  return tag;
}

void UaLinkEndpoint::set_transmit_callback(TransmitCallback callback) {
  UALINK_TRACE_SCOPED(__func__);
  transmit_callback_ = std::move(callback);
}

void UaLinkEndpoint::receive_flit(const DlFlit &flit) {
  UALINK_TRACE_SCOPED(__func__);

  stats_.rx_dl_flits++;

  // First check if this is a command flit (ACK/NAK)
  if (enable_ack_nak_ && command_processor_.process_flit(flit)) {
    // Was a command flit - already processed by callbacks
    return;
  }

  // Extract sequence number from flit header for ACK/NAK generation
  std::uint16_t received_seq = 0;
  if (enable_ack_nak_) {
    // Deserialize explicit flit header to get sequence number
    const ExplicitFlitHeaderFields header = deserialize_explicit_flit_header(flit.flit_header);
    received_seq = header.flit_seq_no;
  }

  // Deserialize DL → TL with optional CRC check and pacing
  std::vector<TlFlit> tl_flits;

  if (enable_crc_check_ && pacing_controller_.has_rx_callback()) {
    // Both CRC check and pacing
    const auto result = DlDeserializer::deserialize_with_crc_and_pacing(flit, pacing_controller_);
    if (!result.has_value()) {
      stats_.rx_crc_errors++;
      return; // CRC check failed
    }
    tl_flits = std::move(*result);
    stats_.rx_flits_with_pacing++;
  } else if (enable_crc_check_) {
    // CRC check only
    const auto result = DlDeserializer::deserialize_with_crc_check(flit);
    if (!result.has_value()) {
      stats_.rx_crc_errors++;
      return; // CRC check failed
    }
    tl_flits = std::move(*result);
  } else if (pacing_controller_.has_rx_callback()) {
    // Pacing only
    tl_flits = DlDeserializer::deserialize_with_pacing(flit, pacing_controller_);
    stats_.rx_flits_with_pacing++;
  } else {
    // No CRC check or pacing
    tl_flits = DlDeserializer::deserialize(flit);
  }

  // Generate ACK/NAK if enabled
  if (enable_ack_nak_ && transmit_callback_) {
    const std::uint8_t our_tx_seq_lo = tx_last_seq_ & 0x7;
    const auto command_flit = ack_nak_manager_.process_received_flit(received_seq, our_tx_seq_lo);
    if (command_flit.has_value()) {
      // Send ACK or NAK
      transmit_callback_(*command_flit);

      // Update stats based on command type
      const DlCommandOp op = command_processor_.deserialize_command_op(command_flit->flit_header);
      if (op == DlCommandOp::kAck) {
        stats_.tx_acks_sent++;
      } else if (op == DlCommandOp::kReplayRequest) {
        stats_.tx_replay_requests_sent++;
      }
    }
  }

  // Process each TL flit
  for (const auto &tl_flit : tl_flits) {
    handle_tl_flit(tl_flit);
  }
}

void UaLinkEndpoint::set_read_completion_callback(ReadCompletionCallback callback) {
  UALINK_TRACE_SCOPED(__func__);
  read_completion_callback_ = std::move(callback);
}

void UaLinkEndpoint::set_write_completion_callback(WriteCompletionCallback callback) {
  UALINK_TRACE_SCOPED(__func__);
  write_completion_callback_ = std::move(callback);
}

void UaLinkEndpoint::process_ack(std::uint16_t ack_seq) {
  UALINK_TRACE_SCOPED(__func__);
  [[maybe_unused]] const std::size_t retired = replay_buffer_.process_ack(ack_seq);
  stats_.replay_buffer_size = replay_buffer_.size();
}

void UaLinkEndpoint::replay_from(std::uint16_t seq) {
  UALINK_TRACE_SCOPED(__func__);

  if (!transmit_callback_) {
    throw std::logic_error("replay_from: transmit_callback not set");
  }

  const auto flits = replay_buffer_.process_nak(seq);
  for (const auto &flit : flits) {
    transmit_callback_(flit);
  }
}

void UaLinkEndpoint::reset_stats() {
  UALINK_TRACE_SCOPED(__func__);
  stats_ = Stats{};
}

void UaLinkEndpoint::enable_error_injection() {
  UALINK_TRACE_SCOPED(__func__);
  error_injector_.enable();
}

void UaLinkEndpoint::disable_error_injection() {
  UALINK_TRACE_SCOPED(__func__);
  error_injector_.disable();
}

void UaLinkEndpoint::set_error_policy(std::function<ErrorType()> policy) {
  UALINK_TRACE_SCOPED(__func__);
  error_injector_.set_policy(std::move(policy));
}

void UaLinkEndpoint::set_tx_pacing_callback(TxPacingCallback callback) {
  UALINK_TRACE_SCOPED(__func__);
  pacing_controller_.set_tx_callback(std::move(callback));
}

void UaLinkEndpoint::set_rx_rate_callback(RxRateCallback callback) {
  UALINK_TRACE_SCOPED(__func__);
  pacing_controller_.set_rx_callback(std::move(callback));
}

void UaLinkEndpoint::clear_pacing_callbacks() {
  UALINK_TRACE_SCOPED(__func__);
  pacing_controller_.clear_callbacks();
}

void UaLinkEndpoint::transmit_tl_flits(const std::vector<TlFlit> &tl_flits) {
  UALINK_TRACE_SCOPED(__func__);

  if (!transmit_callback_) {
    throw std::logic_error("transmit_tl_flits: transmit_callback not set");
  }

  if (tl_flits.empty()) {
    return;
  }

  // Build DL header
  ExplicitFlitHeaderFields header{};
  header.op = 0; // Explicit flit
  header.payload = true;
  const auto next_seq = [](std::uint16_t last_seq) -> std::uint16_t {
    // Valid sequence numbers are 1..511, 0 reserved. 511 wraps to 1.
    const std::uint16_t mod = static_cast<std::uint16_t>(last_seq % 511U);
    return static_cast<std::uint16_t>(mod + 1U);
  };
  header.flit_seq_no = next_seq(tx_last_seq_);

  // Serialize TL → DL with pacing and error injection
  std::size_t packed = 0;
  DlFlit dl_flit;

  // Determine which serialize method to use based on configuration
  if (error_injector_.is_enabled() && pacing_controller_.has_tx_callback()) {
    // Both error injection and pacing
    // First apply pacing check
    const PacingDecision pacing_decision = pacing_controller_.check_tx_pacing(tl_flits.size(), tl_flits.size() * tl::kTlFlitBytes);

    if (pacing_decision == PacingDecision::kDrop) {
      stats_.tx_dropped_by_pacing++;
      return;
    }
    if (pacing_decision == PacingDecision::kThrottle) {
      stats_.tx_dropped_by_pacing++;
      return; // For now, treat throttle as drop
    }

    // Apply error injection
    dl_flit = DlSerializer::serialize_with_error_injection(tl_flits, header, error_injector_, &packed);

    if (packed == 0) {
      // Packet was dropped by error injection
      stats_.tx_dropped_by_error_injection++;
      return;
    }
  } else if (error_injector_.is_enabled()) {
    // Error injection only
    dl_flit = DlSerializer::serialize_with_error_injection(tl_flits, header, error_injector_, &packed);

    if (packed == 0) {
      // Packet was dropped by error injection
      stats_.tx_dropped_by_error_injection++;
      return;
    }
  } else if (pacing_controller_.has_tx_callback()) {
    // Pacing only
    dl_flit = DlSerializer::serialize_with_pacing(tl_flits, header, pacing_controller_, &packed);

    if (packed == 0) {
      // Packet was dropped/throttled by pacing
      stats_.tx_dropped_by_pacing++;
      return;
    }
  } else {
    // No pacing or error injection
    dl_flit = DlSerializer::serialize(tl_flits, header, &packed);
  }

  // Store in replay buffer
  [[maybe_unused]] const bool added = replay_buffer_.add_flit(header.flit_seq_no, dl_flit);
  stats_.replay_buffer_size = replay_buffer_.size();

  // Transmit
  transmit_callback_(dl_flit);
  stats_.tx_dl_flits++;

  // Increment sequence number
  tx_last_seq_ = header.flit_seq_no;
}

void UaLinkEndpoint::handle_tl_flit(const TlFlit &tl_flit) {
  UALINK_TRACE_SCOPED(__func__);

  // Deserialize opcode from flit data
  const TlOpcode opcode = TlDeserializer::deserialize_opcode(tl_flit.data);

  if (opcode == TlOpcode::kReadResponse) {
    // Handle read response
    const auto response = TlDeserializer::deserialize_read_response(tl_flit.data);
    if (response.has_value()) {
      stats_.rx_read_responses++;

      if (read_completion_callback_) {
        // Convert response data to vector
        std::vector<std::byte> data(response->data.begin(), response->data.end());
        read_completion_callback_(response->header.tag, response->header.status, data);
      }
    }
  } else if (opcode == TlOpcode::kWriteCompletion) {
    // Handle write completion
    const auto completion = TlDeserializer::deserialize_write_completion(tl_flit.data);
    if (completion.has_value()) {
      stats_.rx_write_completions++;

      if (write_completion_callback_) {
        write_completion_callback_(completion->header.tag, completion->header.status);
      }
    }
  }
  // Ignore other opcodes (requests received on this endpoint)
}

std::uint16_t UaLinkEndpoint::allocate_tag() {
  UALINK_TRACE_SCOPED(__func__);
  const std::uint16_t tag = next_tag_;
  next_tag_ = (next_tag_ + 1) & 0xFFF; // 12-bit tag space
  return tag;
}
