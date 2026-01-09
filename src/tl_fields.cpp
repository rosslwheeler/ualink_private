#include "ualink/tl_fields.h"

#include <cstdint>
#include <stdexcept>

namespace ualink::tl {

static constexpr bool fits_in_bits(std::uint64_t value, std::uint8_t bits) {
  if (bits >= 64) {
    return true;
  }
  return value <= ((1ULL << bits) - 1ULL);
}

std::array<std::byte, 16> serialize_uncompressed_request_field(const UncompressedRequestField &f) {
  UALINK_TRACE_SCOPED(__func__);
  if (!fits_in_bits(f.cmd, 6) || !fits_in_bits(f.vchan, 2) || !fits_in_bits(f.asi, 2) || !fits_in_bits(f.tag, 11) ||
      !fits_in_bits(f.attr, 8) || !fits_in_bits(f.len, 6) || !fits_in_bits(f.metadata, 8) || !fits_in_bits(f.addr, 55) ||
      !fits_in_bits(f.srcaccid, 10) || !fits_in_bits(f.dstaccid, 10) || !fits_in_bits(f.cway, 2) || !fits_in_bits(f.numbeats, 2)) {
    throw std::invalid_argument("UncompressedRequestField: field out of range");
  }

  std::array<std::byte, 16> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kUncompressedRequestFieldFormat, static_cast<std::uint8_t>(TlFieldType::kUncompressedRequest), f.cmd, f.vchan, f.asi,
              f.tag, f.pool ? 1U : 0U, f.attr, f.len, f.metadata, f.addr, f.srcaccid, f.dstaccid, f.cload ? 1U : 0U, f.cway,
              f.numbeats);
  return out;
}

std::optional<UncompressedRequestField> deserialize_uncompressed_request_field(std::span<const std::byte, 16> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  UncompressedRequestField f{};
  std::uint8_t ftype = 0;
  std::uint8_t pool = 0;
  std::uint8_t cload = 0;

  r.deserialize_into(kUncompressedRequestFieldFormat, ftype, f.cmd, f.vchan, f.asi, f.tag, pool, f.attr, f.len, f.metadata, f.addr,
                     f.srcaccid, f.dstaccid, cload, f.cway, f.numbeats);

  if (ftype != static_cast<std::uint8_t>(TlFieldType::kUncompressedRequest)) {
    return std::nullopt;
  }

  f.pool = (pool != 0);
  f.cload = (cload != 0);
  return f;
}

std::array<std::byte, 8> serialize_uncompressed_response_field(const UncompressedResponseField &f) {
  UALINK_TRACE_SCOPED(__func__);
  if (!fits_in_bits(f.vchan, 2) || !fits_in_bits(f.tag, 11) || !fits_in_bits(f.len, 2) || !fits_in_bits(f.offset, 2) ||
      !fits_in_bits(f.status, 4) || !fits_in_bits(f.srcaccid, 10) || !fits_in_bits(f.dstaccid, 10) || !fits_in_bits(f.spares, 16)) {
    throw std::invalid_argument("UncompressedResponseField: field out of range");
  }

  std::array<std::byte, 8> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kUncompressedResponseFieldFormat, static_cast<std::uint8_t>(TlFieldType::kUncompressedResponse), f.vchan, f.tag,
              f.pool ? 1U : 0U, f.len, f.offset, f.status, f.rd_wr ? 1U : 0U, f.last ? 1U : 0U, f.srcaccid, f.dstaccid, f.spares);
  return out;
}

std::optional<UncompressedResponseField> deserialize_uncompressed_response_field(std::span<const std::byte, 8> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  UncompressedResponseField f{};
  std::uint8_t ftype = 0;
  std::uint8_t pool = 0;
  std::uint8_t rd_wr = 0;
  std::uint8_t last = 0;

  r.deserialize_into(kUncompressedResponseFieldFormat, ftype, f.vchan, f.tag, pool, f.len, f.offset, f.status, rd_wr, last,
                     f.srcaccid, f.dstaccid, f.spares);

  if (ftype != static_cast<std::uint8_t>(TlFieldType::kUncompressedResponse)) {
    return std::nullopt;
  }

  f.pool = (pool != 0);
  f.rd_wr = (rd_wr != 0);
  f.last = (last != 0);
  return f;
}

std::array<std::byte, 8> serialize_compressed_request_field(const CompressedRequestField &f) {
  UALINK_TRACE_SCOPED(__func__);
  if (!fits_in_bits(f.cmd, 3) || !fits_in_bits(f.vchan, 2) || !fits_in_bits(f.asi, 2) || !fits_in_bits(f.tag, 11) ||
      !fits_in_bits(f.len, 2) || !fits_in_bits(f.metadata, 3) || !fits_in_bits(f.addr, 14) || !fits_in_bits(f.srcaccid, 10) ||
      !fits_in_bits(f.dstaccid, 10) || !fits_in_bits(f.cway, 2)) {
    throw std::invalid_argument("CompressedRequestField: field out of range");
  }

  std::array<std::byte, 8> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kCompressedRequestFieldFormat, static_cast<std::uint8_t>(TlFieldType::kCompressedRequest), f.cmd, f.vchan, f.asi,
              f.tag, f.pool ? 1U : 0U, f.len, f.metadata, f.addr, f.srcaccid, f.dstaccid, f.cway);
  return out;
}

std::optional<CompressedRequestField> deserialize_compressed_request_field(std::span<const std::byte, 8> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  CompressedRequestField f{};
  std::uint8_t ftype = 0;
  std::uint8_t pool = 0;

  r.deserialize_into(kCompressedRequestFieldFormat, ftype, f.cmd, f.vchan, f.asi, f.tag, pool, f.len, f.metadata, f.addr,
                     f.srcaccid, f.dstaccid, f.cway);

  if (ftype != static_cast<std::uint8_t>(TlFieldType::kCompressedRequest)) {
    return std::nullopt;
  }

  f.pool = (pool != 0);
  return f;
}

std::array<std::byte, 4> serialize_compressed_single_beat_read_response_field(const CompressedSingleBeatReadResponseField &f) {
  UALINK_TRACE_SCOPED(__func__);
  if (!fits_in_bits(f.vchan, 2) || !fits_in_bits(f.tag, 11) || !fits_in_bits(f.dstaccid, 10) || !fits_in_bits(f.offset, 2)) {
    throw std::invalid_argument("CompressedSingleBeatReadResponseField: field out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kCompressedSingleBeatReadResponseFieldFormat,
              static_cast<std::uint8_t>(TlFieldType::kCompressedResponseSingleBeatRead), f.vchan, f.tag, f.pool ? 1U : 0U,
              f.dstaccid, f.offset, f.last ? 1U : 0U, 0U);
  return out;
}

std::optional<CompressedSingleBeatReadResponseField>
deserialize_compressed_single_beat_read_response_field(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  CompressedSingleBeatReadResponseField f{};
  std::uint8_t ftype = 0;
  std::uint8_t pool = 0;
  std::uint8_t last = 0;
  std::uint8_t spare = 0;

  r.deserialize_into(kCompressedSingleBeatReadResponseFieldFormat, ftype, f.vchan, f.tag, pool, f.dstaccid, f.offset, last, spare);

  if (ftype != static_cast<std::uint8_t>(TlFieldType::kCompressedResponseSingleBeatRead)) {
    return std::nullopt;
  }

  f.pool = (pool != 0);
  f.last = (last != 0);
  return f;
}

std::array<std::byte, 4>
serialize_compressed_write_or_multibeat_read_response_field(const CompressedWriteOrMultiBeatReadResponseField &f) {
  UALINK_TRACE_SCOPED(__func__);
  if (!fits_in_bits(f.vchan, 2) || !fits_in_bits(f.tag, 11) || !fits_in_bits(f.dstaccid, 10) || !fits_in_bits(f.len, 2)) {
    throw std::invalid_argument("CompressedWriteOrMultiBeatReadResponseField: field out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kCompressedWriteOrMultiBeatReadResponseFieldFormat,
              static_cast<std::uint8_t>(TlFieldType::kCompressedResponseWriteOrMultiBeatRead), f.vchan, f.tag, f.pool ? 1U : 0U,
              f.dstaccid, f.len, f.rd_wr ? 1U : 0U, 0U);
  return out;
}

std::optional<CompressedWriteOrMultiBeatReadResponseField>
deserialize_compressed_write_or_multibeat_read_response_field(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  CompressedWriteOrMultiBeatReadResponseField f{};
  std::uint8_t ftype = 0;
  std::uint8_t pool = 0;
  std::uint8_t rd_wr = 0;
  std::uint8_t spare = 0;

  r.deserialize_into(kCompressedWriteOrMultiBeatReadResponseFieldFormat, ftype, f.vchan, f.tag, pool, f.dstaccid, f.len, rd_wr,
                     spare);

  if (ftype != static_cast<std::uint8_t>(TlFieldType::kCompressedResponseWriteOrMultiBeatRead)) {
    return std::nullopt;
  }

  f.pool = (pool != 0);
  f.rd_wr = (rd_wr != 0);
  return f;
}

std::array<std::byte, 4> serialize_flow_control_nop_field(const FlowControlNopField &f) {
  UALINK_TRACE_SCOPED(__func__);
  if (!fits_in_bits(f.req_cmd, 6) || !fits_in_bits(f.rsp_cmd, 6) || !fits_in_bits(f.req_data, 8) || !fits_in_bits(f.rsp_data, 8)) {
    throw std::invalid_argument("FlowControlNopField: field out of range");
  }

  std::array<std::byte, 4> out{};
  bit_fields::NetworkBitWriter w(out);
  w.serialize(kFlowControlNopFieldFormat, static_cast<std::uint8_t>(TlFieldType::kFlowControlNop), f.req_cmd, f.rsp_cmd, f.req_data,
              f.rsp_data);
  return out;
}

std::optional<FlowControlNopField> deserialize_flow_control_nop_field(std::span<const std::byte, 4> bytes) {
  UALINK_TRACE_SCOPED(__func__);
  bit_fields::NetworkBitReader r(bytes);

  FlowControlNopField f{};
  std::uint8_t ftype = 0;
  r.deserialize_into(kFlowControlNopFieldFormat, ftype, f.req_cmd, f.rsp_cmd, f.req_data, f.rsp_data);

  if (ftype != static_cast<std::uint8_t>(TlFieldType::kFlowControlNop)) {
    return std::nullopt;
  }
  return f;
}

} // namespace ualink::tl
