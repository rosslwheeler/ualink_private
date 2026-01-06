#include "ualink/dl_flit.h"

#include <algorithm>
#include <stdexcept>

#include "ualink/crc.h"
#include "ualink/dl_error_injection.h"
#include "ualink/dl_pacing.h"

using namespace ualink::dl;

static std::size_t segment_index_for_offset(std::size_t offset) {
  UALINK_TRACE_SCOPED(__func__);
  for (std::size_t segment_index = 0; segment_index < kSegmentPayloadOffsets.size();
       ++segment_index) {
    const std::size_t segment_start = kSegmentPayloadOffsets[segment_index];
    const std::size_t segment_end = segment_start + kSegmentPayloadBytes[segment_index];
    if (offset >= segment_start && offset < segment_end) {
      return segment_index;
    }
  }
  throw std::out_of_range("segment_index_for_offset: offset out of range");
}

static std::size_t segment_slot_for_offset(std::size_t offset, std::size_t segment_index) {
  UALINK_TRACE_SCOPED(__func__);
  const std::size_t segment_start = kSegmentPayloadOffsets[segment_index];
  const std::size_t delta = offset - segment_start;
  if (delta == 0) {
    return 0;
  }
  if (delta == kTlFlitBytes) {
    return 1;
  }
  throw std::invalid_argument("segment_slot_for_offset: unsupported start offset");
}

std::array<std::byte, 3> ualink::dl::encode_explicit_flit_header(
    const ExplicitFlitHeaderFields& fields) {
  UALINK_TRACE_SCOPED(__func__);
  if (fields.flit_seq_no > 0x1FF) {
    throw std::invalid_argument("encode_explicit_flit_header: flit_seq_no out of range");
  }
  if (fields.op > 0x7) {
    throw std::invalid_argument("encode_explicit_flit_header: op out of range");
  }

  std::array<std::byte, 3> buffer{};
  bit_fields::NetworkBitWriter writer(buffer);
  std::uint8_t payload_bit = 0;
  if (fields.payload) {
    payload_bit = 1;
  }
  writer.serialize(kExplicitFlitHeaderFormat,
                   fields.op,
                   payload_bit,
                   0U,
                   fields.flit_seq_no,
                   0U);
  return buffer;
}

ExplicitFlitHeaderFields ualink::dl::decode_explicit_flit_header(
    std::span<const std::byte, 3> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader reader(bytes);
  ExplicitFlitHeaderFields fields{};
  std::uint8_t payload_bit = 0;
  std::uint8_t reserved0 = 0;
  std::uint8_t reserved1 = 0;
  reader.deserialize_into(kExplicitFlitHeaderFormat,
                          fields.op,
                          payload_bit,
                          reserved0,
                          fields.flit_seq_no,
                          reserved1);
  fields.payload = false;
  if (payload_bit != 0) {
    fields.payload = true;
  }
  return fields;
}

std::array<std::byte, 3> ualink::dl::encode_command_flit_header(
    const CommandFlitHeaderFields& fields) {
  UALINK_TRACE_SCOPED(__func__);
  if (fields.ack_req_seq > 0x1FF) {
    throw std::invalid_argument("encode_command_flit_header: ack_req_seq out of range");
  }
  if (fields.flit_seq_lo > 0x7) {
    throw std::invalid_argument("encode_command_flit_header: flit_seq_lo out of range");
  }
  if (fields.op > 0x7) {
    throw std::invalid_argument("encode_command_flit_header: op out of range");
  }

  std::array<std::byte, 3> buffer{};
  bit_fields::NetworkBitWriter writer(buffer);
  std::uint8_t payload_bit = 0;
  if (fields.payload) {
    payload_bit = 1;
  }
  writer.serialize(kCommandFlitHeaderFormat,
                   fields.op,
                   payload_bit,
                   fields.ack_req_seq,
                   fields.flit_seq_lo,
                   0U);
  return buffer;
}

CommandFlitHeaderFields ualink::dl::decode_command_flit_header(
    std::span<const std::byte, 3> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader reader(bytes);
  CommandFlitHeaderFields fields{};
  std::uint8_t payload_bit = 0;
  std::uint8_t reserved1 = 0;
  reader.deserialize_into(kCommandFlitHeaderFormat,
                          fields.op,
                          payload_bit,
                          fields.ack_req_seq,
                          fields.flit_seq_lo,
                          reserved1);
  fields.payload = false;
  if (payload_bit != 0) {
    fields.payload = true;
  }
  return fields;
}

std::byte ualink::dl::encode_segment_header(const SegmentHeaderFields& fields) {
  UALINK_TRACE_SCOPED(__func__);
  if (fields.message0 > 0x3 || fields.message1 > 0x3) {
    throw std::invalid_argument("encode_segment_header: message bits out of range");
  }

  std::array<std::byte, 1> buffer{};
  bit_fields::NetworkBitWriter writer(buffer);
  std::uint8_t tl_flit0 = 0;
  if (fields.tl_flit0_present) {
    tl_flit0 = 1;
  }
  std::uint8_t tl_flit1 = 0;
  if (fields.tl_flit1_present) {
    tl_flit1 = 1;
  }
  std::uint8_t dl_alt_sector = 0;
  if (fields.dl_alt_sector) {
    dl_alt_sector = 1;
  }
  writer.serialize(kSegmentHeaderFormat,
                   tl_flit1,
                   fields.message1,
                   tl_flit0,
                   fields.message0,
                   0U,
                   dl_alt_sector);
  return buffer[0];
}

SegmentHeaderFields ualink::dl::decode_segment_header(std::byte value) {
  UALINK_TRACE_SCOPED(__func__);
  std::array<std::byte, 1> buffer{value};
  bit_fields::NetworkBitReader reader(buffer);
  SegmentHeaderFields fields{};
  std::uint8_t tl_flit1 = 0;
  std::uint8_t tl_flit0 = 0;
  std::uint8_t dl_alt_sector = 0;
  std::uint8_t reserved = 0;
  reader.deserialize_into(kSegmentHeaderFormat,
                          tl_flit1,
                          fields.message1,
                          tl_flit0,
                          fields.message0,
                          reserved,
                          dl_alt_sector);
  fields.tl_flit1_present = false;
  if (tl_flit1 != 0) {
    fields.tl_flit1_present = true;
  }
  fields.tl_flit0_present = false;
  if (tl_flit0 != 0) {
    fields.tl_flit0_present = true;
  }
  fields.dl_alt_sector = false;
  if (dl_alt_sector != 0) {
    fields.dl_alt_sector = true;
  }
  return fields;
}

DlFlit ualink::dl::DlSerializer::serialize(std::span<const TlFlit> tl_flits,
                                 const ExplicitFlitHeaderFields& header,
                                 std::size_t* flits_serialized) {
  UALINK_TRACE_SCOPED(__func__);
  DlFlit flit{};
  flit.flit_header = encode_explicit_flit_header(header);

  const std::size_t max_full_flits = kDlPayloadBytes / kTlFlitBytes;
  const std::size_t packed_count = std::min(tl_flits.size(), max_full_flits);

  std::array<SegmentHeaderFields, kDlSegmentCount> segment_fields{};

  for (std::size_t flit_index = 0; flit_index < packed_count; ++flit_index) {
    const std::size_t payload_offset = flit_index * kTlFlitBytes;
    const std::size_t segment_index = segment_index_for_offset(payload_offset);
    const std::size_t slot = segment_slot_for_offset(payload_offset, segment_index);

    std::copy_n(tl_flits[flit_index].data.begin(),
                kTlFlitBytes,
                flit.payload.begin() + payload_offset);

    const std::uint8_t message_field = tl_flits[flit_index].message_field & 0x3U;
    if (slot == 0) {
      segment_fields[segment_index].tl_flit0_present = true;
      segment_fields[segment_index].message0 = message_field;
    } else {
      segment_fields[segment_index].tl_flit1_present = true;
      segment_fields[segment_index].message1 = message_field;
    }
  }

  for (std::size_t segment_index = 0; segment_index < kDlSegmentCount; ++segment_index) {
    flit.segment_headers[segment_index] = encode_segment_header(segment_fields[segment_index]);
  }

  // Compute CRC over flit_header + segment_headers + payload
  // Total: 3 + 5 + 628 = 636 bytes
  constexpr std::size_t kCrcCoveredBytes = 3 + kDlSegmentCount + kDlPayloadBytes;
  std::array<std::byte, kCrcCoveredBytes> crc_buffer{};

  std::copy_n(flit.flit_header.begin(), 3, crc_buffer.begin());
  std::copy_n(flit.segment_headers.begin(), kDlSegmentCount, crc_buffer.begin() + 3);
  std::copy_n(flit.payload.begin(), kDlPayloadBytes, crc_buffer.begin() + 3 + kDlSegmentCount);

  flit.crc = compute_crc32(crc_buffer);

  if (flits_serialized != nullptr) {
    *flits_serialized = packed_count;
  }

  return flit;
}

std::vector<TlFlit> ualink::dl::DlDeserializer::deserialize(const DlFlit& flit) {
  UALINK_TRACE_SCOPED(__func__);
  std::vector<TlFlit> tl_flits;

  for (std::size_t segment_index = 0; segment_index < kDlSegmentCount; ++segment_index) {
    const SegmentHeaderFields header = decode_segment_header(flit.segment_headers[segment_index]);
    const std::size_t segment_offset = kSegmentPayloadOffsets[segment_index];
    const std::size_t segment_size = kSegmentPayloadBytes[segment_index];

    if (header.tl_flit0_present && segment_size >= kTlFlitBytes) {
      TlFlit tl_flit{};
      std::copy_n(flit.payload.begin() + segment_offset,
                  kTlFlitBytes,
                  tl_flit.data.begin());
      tl_flit.message_field = header.message0;
      tl_flits.push_back(tl_flit);
    }

    if (header.tl_flit1_present && segment_size >= (2 * kTlFlitBytes)) {
      TlFlit tl_flit{};
      std::copy_n(flit.payload.begin() + segment_offset + kTlFlitBytes,
                  kTlFlitBytes,
                  tl_flit.data.begin());
      tl_flit.message_field = header.message1;
      tl_flits.push_back(tl_flit);
    }
  }

  return tl_flits;
}

std::optional<std::vector<TlFlit>> ualink::dl::DlDeserializer::deserialize_with_crc_check(
    const DlFlit& flit) {
  UALINK_TRACE_SCOPED(__func__);

  // Verify CRC over flit_header + segment_headers + payload
  constexpr std::size_t kCrcCoveredBytes = 3 + kDlSegmentCount + kDlPayloadBytes;
  std::array<std::byte, kCrcCoveredBytes> crc_buffer{};

  std::copy_n(flit.flit_header.begin(), 3, crc_buffer.begin());
  std::copy_n(flit.segment_headers.begin(), kDlSegmentCount, crc_buffer.begin() + 3);
  std::copy_n(flit.payload.begin(), kDlPayloadBytes, crc_buffer.begin() + 3 + kDlSegmentCount);

  if (!verify_crc32(crc_buffer, flit.crc)) {
    return std::nullopt;
  }

  return deserialize(flit);
}

DlFlit ualink::dl::DlSerializer::serialize_with_pacing(std::span<const TlFlit> tl_flits,
                                               const ExplicitFlitHeaderFields& header,
                                               DlPacingController& pacing,
                                               std::size_t* flits_serialized) {
  UALINK_TRACE_SCOPED(__func__);

  // Check pacing before packing
  const std::size_t flit_count = tl_flits.size();
  const std::size_t total_bytes = flit_count * kTlFlitBytes;
  const PacingDecision decision = pacing.check_tx_pacing(flit_count, total_bytes);

  if (decision == PacingDecision::kDrop) {
    // Return empty flit if dropped
    if (flits_serialized != nullptr) {
      *flits_serialized = 0;
    }
    return DlFlit{};
  }

  // If throttled, we still pack but signal may be used by caller
  // (In a real system, caller would delay transmission)

  return serialize(tl_flits, header, flits_serialized);
}

std::vector<TlFlit> ualink::dl::DlDeserializer::deserialize_with_pacing(const DlFlit& flit,
                                                                 DlPacingController& pacing) {
  UALINK_TRACE_SCOPED(__func__);

  std::vector<TlFlit> result = deserialize(flit);

  // Notify pacing controller of received flits
  const std::size_t flit_count = result.size();
  const std::size_t total_bytes = flit_count * kTlFlitBytes;
  pacing.notify_rx(flit_count, total_bytes, true);  // CRC not checked in this path

  return result;
}

std::optional<std::vector<TlFlit>> ualink::dl::DlDeserializer::deserialize_with_crc_and_pacing(
    const DlFlit& flit,
    DlPacingController& pacing) {
  UALINK_TRACE_SCOPED(__func__);

  // Verify CRC first
  std::optional<std::vector<TlFlit>> result = deserialize_with_crc_check(flit);

  const bool crc_valid = result.has_value();
  const std::size_t flit_count = crc_valid ? result->size() : 0;
  const std::size_t total_bytes = flit_count * kTlFlitBytes;

  // Notify pacing controller with CRC status
  pacing.notify_rx(flit_count, total_bytes, crc_valid);

  return result;
}

DlFlit ualink::dl::DlSerializer::serialize_with_error_injection(std::span<const TlFlit> tl_flits,
                                                        const ExplicitFlitHeaderFields& header,
                                                        DlErrorInjector& error_injector,
                                                        std::size_t* flits_serialized) {
  UALINK_TRACE_SCOPED(__func__);

  // Check if flit should be dropped
  if (error_injector.should_drop_flit()) {
    if (flits_serialized != nullptr) {
      *flits_serialized = 0;
    }
    return DlFlit{};  // Return empty flit for drop
  }

  // Serialize normally
  DlFlit flit = serialize(tl_flits, header, flits_serialized);

  // Get error type and inject if needed
  const ErrorType error = error_injector.get_next_error();
  if (error != ErrorType::kNone && error != ErrorType::kPacketDrop) {
    flit = error_injector.inject_error(flit, error);
  }

  return flit;
}
