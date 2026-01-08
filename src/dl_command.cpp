#include "ualink/dl_command.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "ualink/crc.h"

using namespace ualink::dl;

// Command factory implementations
DlFlit CommandFactory::create_ack(std::uint16_t ack_seq, std::uint8_t flit_seq_lo) {
  UALINK_TRACE_SCOPED(__func__);

  DlFlit flit{};

  // Build command header
  CommandFlitHeaderFields header{};
  header.op = static_cast<std::uint8_t>(DlCommandOp::kAck);
  header.payload = false; // Command flits have no payload
  header.ack_req_seq = ack_seq;
  header.flit_seq_lo = flit_seq_lo & 0x7; // Only 3 bits

  flit.flit_header = serialize_command_flit_header(header);

  // Command flits have no segment headers or payload
  // All zeros (already initialized)

  // Calculate CRC over entire flit (header + segment headers + payload)
  constexpr std::size_t kCrcBufferSize = 3 + kDlSegmentCount + kDlPayloadBytes;
  std::array<std::byte, kCrcBufferSize> crc_buffer{};

  std::copy_n(flit.flit_header.begin(), 3, crc_buffer.begin());
  std::copy_n(flit.segment_headers.begin(), kDlSegmentCount, crc_buffer.begin() + 3);
  std::copy_n(flit.payload.begin(), kDlPayloadBytes, crc_buffer.begin() + 3 + kDlSegmentCount);

  flit.crc = compute_crc32(crc_buffer);

  return flit;
}

DlFlit CommandFactory::create_nak(std::uint16_t nak_seq, std::uint8_t flit_seq_lo) {
  UALINK_TRACE_SCOPED(__func__);

  DlFlit flit{};

  // Build command header
  CommandFlitHeaderFields header{};
  header.op = static_cast<std::uint8_t>(DlCommandOp::kNak);
  header.payload = false;
  header.ack_req_seq = nak_seq; // For NAK, this is the first missing sequence number
  header.flit_seq_lo = flit_seq_lo & 0x7;

  flit.flit_header = serialize_command_flit_header(header);

  // Command flits have no segment headers or payload

  // Calculate CRC over entire flit (header + segment headers + payload)
  constexpr std::size_t kCrcBufferSize = 3 + kDlSegmentCount + kDlPayloadBytes;
  std::array<std::byte, kCrcBufferSize> crc_buffer{};

  std::copy_n(flit.flit_header.begin(), 3, crc_buffer.begin());
  std::copy_n(flit.segment_headers.begin(), kDlSegmentCount, crc_buffer.begin() + 3);
  std::copy_n(flit.payload.begin(), kDlPayloadBytes, crc_buffer.begin() + 3 + kDlSegmentCount);

  flit.crc = compute_crc32(crc_buffer);

  return flit;
}

// DlCommandProcessor implementation
DlCommandProcessor::DlCommandProcessor() { UALINK_TRACE_SCOPED(__func__); }

void DlCommandProcessor::set_ack_callback(AckCallback callback) noexcept {
  UALINK_TRACE_SCOPED(__func__);
  ack_callback_ = std::move(callback);
}

void DlCommandProcessor::set_nak_callback(NakCallback callback) noexcept {
  UALINK_TRACE_SCOPED(__func__);
  nak_callback_ = std::move(callback);
}

bool DlCommandProcessor::has_ack_callback() const noexcept { return ack_callback_ != nullptr; }

bool DlCommandProcessor::has_nak_callback() const noexcept { return nak_callback_ != nullptr; }

namespace {
bool verify_command_crc(const ualink::dl::DlFlit &flit) {
  // Same CRC coverage as CommandFactory: header (3) + segment headers + payload.
  constexpr std::size_t kCrcBufferSize = 3 + ualink::dl::kDlSegmentCount + ualink::dl::kDlPayloadBytes;
  std::array<std::byte, kCrcBufferSize> crc_buffer{};

  std::copy_n(flit.flit_header.begin(), 3, crc_buffer.begin());
  std::copy_n(flit.segment_headers.begin(), ualink::dl::kDlSegmentCount, crc_buffer.begin() + 3);
  std::copy_n(flit.payload.begin(), ualink::dl::kDlPayloadBytes, crc_buffer.begin() + 3 + ualink::dl::kDlSegmentCount);

  return flit.crc == ualink::dl::compute_crc32(crc_buffer);
}
} // namespace

bool DlCommandProcessor::process_flit(const DlFlit &flit) {
  UALINK_TRACE_SCOPED(__func__);

  CommandFlitHeaderFields header{};
  try {
    header = deserialize_command_flit_header(flit.flit_header);
  } catch (...) {
    // Not a command header (likely an explicit flit); let other handlers process it.
    return false;
  }

  // Command flits must not carry payload.
  if (header.payload)
    return false;

  const DlCommandOp op = static_cast<DlCommandOp>(header.op);

  if (op == DlCommandOp::kAck) {
    if (!verify_command_crc(flit))
      return true; // consume/drop bad command
    stats_.acks_received++;
    if (ack_callback_)
      ack_callback_(header.ack_req_seq);
    return true;
  }

  if (op == DlCommandOp::kNak) {
    if (!verify_command_crc(flit))
      return true; // consume/drop bad command
    stats_.naks_received++;
    if (nak_callback_)
      nak_callback_(header.ack_req_seq);
    return true;
  }

  return false;
}

DlCommandOp DlCommandProcessor::deserialize_command_op(std::span<const std::byte, 3> flit_header) {
  UALINK_TRACE_SCOPED(__func__);

  // Deserialize command header to get op field
  const CommandFlitHeaderFields header = deserialize_command_flit_header(flit_header);
  return static_cast<DlCommandOp>(header.op);
}

std::uint16_t DlCommandProcessor::deserialize_ack_req_seq(const DlFlit &flit) {
  UALINK_TRACE_SCOPED(__func__);

  // Deserialize command header to get ack_req_seq
  const CommandFlitHeaderFields header = deserialize_command_flit_header(flit.flit_header);
  return header.ack_req_seq;
}

void DlCommandProcessor::clear_callbacks() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  ack_callback_ = nullptr;
  nak_callback_ = nullptr;
}

void DlCommandProcessor::reset_stats() {
  UALINK_TRACE_SCOPED(__func__);
  stats_ = Stats{};
}

// DlAckNakManager implementation
DlAckNakManager::DlAckNakManager() { UALINK_TRACE_SCOPED(__func__); }

std::optional<DlFlit> DlAckNakManager::process_received_flit(std::uint16_t received_seq, std::uint8_t our_tx_seq_lo) {
  UALINK_TRACE_SCOPED(__func__);

  // Check if this is the expected sequence number
  if (rx_seq_tracker_.is_expected(received_seq)) {
    // Expected - advance tracker
    rx_seq_tracker_.advance();
    flits_since_ack_++;

    // Check if we should send ACK
    if (ack_every_n_ == 0) {
      // ACK immediately
      flits_since_ack_ = 0;
      return CommandFactory::create_ack(received_seq, our_tx_seq_lo);
    } else if (flits_since_ack_ >= ack_every_n_) {
      // ACK after N flits
      flits_since_ack_ = 0;
      return CommandFactory::create_ack(received_seq, our_tx_seq_lo);
    } else {
      // Don't ACK yet
      return std::nullopt;
    }

  } else if (rx_seq_tracker_.is_duplicate(received_seq)) {
    // Duplicate - ignore, but might want to re-ACK
    // For now, just ignore
    return std::nullopt;

  } else {
    // Out of order - send NAK for expected sequence
    const std::uint16_t expected = rx_seq_tracker_.expected_seq();
    return CommandFactory::create_nak(expected, our_tx_seq_lo);
  }
}

std::uint16_t DlAckNakManager::expected_rx_seq() const noexcept { return rx_seq_tracker_.expected_seq(); }

void DlAckNakManager::reset_rx_state() noexcept {
  UALINK_TRACE_SCOPED(__func__);
  rx_seq_tracker_.reset();
  flits_since_ack_ = 0;
}

DlFlit DlAckNakManager::generate_ack(std::uint16_t ack_seq, std::uint8_t flit_seq_lo) {
  UALINK_TRACE_SCOPED(__func__);
  return CommandFactory::create_ack(ack_seq, flit_seq_lo);
}

DlFlit DlAckNakManager::generate_nak(std::uint16_t nak_seq, std::uint8_t flit_seq_lo) {
  UALINK_TRACE_SCOPED(__func__);
  return CommandFactory::create_nak(nak_seq, flit_seq_lo);
}

void DlAckNakManager::set_ack_every_n_flits(std::size_t n) noexcept {
  UALINK_TRACE_SCOPED(__func__);
  ack_every_n_ = n;
}

std::size_t DlAckNakManager::get_ack_every_n_flits() const noexcept { return ack_every_n_; }
