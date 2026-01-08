#include "ualink/upli_channel.h"

#include <algorithm>
#include <stdexcept>

using namespace ualink::upli;

// =============================================================================
// Request Channel Serialize/Deserialize
// =============================================================================

std::vector<std::byte> ualink::upli::serialize_upli_request(
    const UpliRequestFields& fields) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges
  if (fields.req_port_id > 0x3) {
    throw std::invalid_argument(
        "serialize_upli_request: req_port_id out of range");
  }
  if (fields.req_src_phys_acc_id > 0x3FF) {
    throw std::invalid_argument(
        "serialize_upli_request: req_src_phys_acc_id out of range");
  }
  if (fields.req_dst_phys_acc_id > 0x3FF) {
    throw std::invalid_argument(
        "serialize_upli_request: req_dst_phys_acc_id out of range");
  }
  if (fields.req_tag > 0x7FF) {
    throw std::invalid_argument("serialize_upli_request: req_tag out of range");
  }
  if (fields.req_addr > 0x1FFFFFFFFFFFFFF) {  // 57 bits
    throw std::invalid_argument("serialize_upli_request: req_addr out of range");
  }
  if (fields.req_cmd > 0x3F) {
    throw std::invalid_argument("serialize_upli_request: req_cmd out of range");
  }
  if (fields.req_len > 0x3F) {
    throw std::invalid_argument("serialize_upli_request: req_len out of range");
  }
  if (fields.req_num_beats > 0x3) {
    throw std::invalid_argument(
        "serialize_upli_request: req_num_beats out of range");
  }
  if (fields.req_vc > 0x3) {
    throw std::invalid_argument("serialize_upli_request: req_vc out of range");
  }

  // Calculate buffer size
  const std::size_t total_bits = kUpliRequestFormat.total_bits();
  const std::size_t total_bytes = (total_bits + 7) / 8;
  std::vector<std::byte> buffer(total_bytes);

  // Serialize using NetworkBitWriter
  bit_fields::NetworkBitWriter writer(buffer);
  writer.serialize(kUpliRequestFormat,
                   fields.req_vld ? 1U : 0U,
                   fields.req_port_id,
                   fields.req_src_phys_acc_id,
                   fields.req_dst_phys_acc_id,
                   fields.req_tag,
                   fields.req_addr,
                   fields.req_cmd,
                   fields.req_len,
                   fields.req_num_beats,
                   fields.req_attr,
                   fields.req_meta_data,
                   fields.req_vc,
                   fields.req_auth_tag);

  return buffer;
}

UpliRequestFields ualink::upli::deserialize_upli_request(
    std::span<const std::byte> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  bit_fields::NetworkBitReader reader(bytes);
  UpliRequestFields fields{};

  std::uint8_t req_vld_bit = 0;
  reader.deserialize_into(kUpliRequestFormat,
                          req_vld_bit,
                          fields.req_port_id,
                          fields.req_src_phys_acc_id,
                          fields.req_dst_phys_acc_id,
                          fields.req_tag,
                          fields.req_addr,
                          fields.req_cmd,
                          fields.req_len,
                          fields.req_num_beats,
                          fields.req_attr,
                          fields.req_meta_data,
                          fields.req_vc,
                          fields.req_auth_tag);

  fields.req_vld = (req_vld_bit != 0);
  return fields;
}

// =============================================================================
// Originator Data Channel Serialize/Deserialize
// =============================================================================

std::vector<std::byte> ualink::upli::serialize_upli_orig_data(
    const UpliOrigDataFields& fields) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges
  if (fields.orig_data_port_id > 0x3) {
    throw std::invalid_argument(
        "serialize_upli_orig_data: orig_data_port_id out of range");
  }

  // Calculate control header size
  const std::size_t ctrl_bits = kUpliOrigDataControlFormat.total_bits();
  const std::size_t ctrl_bytes = (ctrl_bits + 7) / 8;

  // Total size: control header + 64-byte data
  std::vector<std::byte> buffer(ctrl_bytes + kUpliDataBeatBytes);

  // Serialize control fields
  bit_fields::NetworkBitWriter writer(std::span(buffer.data(), ctrl_bytes));
  writer.serialize(kUpliOrigDataControlFormat,
                   fields.orig_data_vld ? 1U : 0U,
                   fields.orig_data_port_id,
                   fields.orig_data_error ? 1U : 0U,
                   0U);  // reserved

  // Copy data payload
  std::copy(fields.data.begin(), fields.data.end(),
            buffer.begin() + ctrl_bytes);

  return buffer;
}

UpliOrigDataFields ualink::upli::deserialize_upli_orig_data(
    std::span<const std::byte> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  const std::size_t ctrl_bits = kUpliOrigDataControlFormat.total_bits();
  const std::size_t ctrl_bytes = (ctrl_bits + 7) / 8;

  if (bytes.size() < ctrl_bytes + kUpliDataBeatBytes) {
    throw std::invalid_argument(
        "deserialize_upli_orig_data: insufficient bytes");
  }

  UpliOrigDataFields fields{};

  // Deserialize control fields
  bit_fields::NetworkBitReader reader(bytes.subspan(0, ctrl_bytes));
  std::uint8_t orig_data_vld_bit = 0;
  std::uint8_t orig_data_error_bit = 0;
  std::uint8_t reserved = 0;

  reader.deserialize_into(kUpliOrigDataControlFormat,
                          orig_data_vld_bit,
                          fields.orig_data_port_id,
                          orig_data_error_bit,
                          reserved);

  fields.orig_data_vld = (orig_data_vld_bit != 0);
  fields.orig_data_error = (orig_data_error_bit != 0);

  // Copy data payload
  std::copy_n(bytes.data() + ctrl_bytes, kUpliDataBeatBytes,
              fields.data.begin());

  return fields;
}

// =============================================================================
// Read Response Channel Serialize/Deserialize
// =============================================================================

std::vector<std::byte> ualink::upli::serialize_upli_rd_rsp(
    const UpliRdRspFields& fields) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges
  if (fields.rd_rsp_port_id > 0x3) {
    throw std::invalid_argument(
        "serialize_upli_rd_rsp: rd_rsp_port_id out of range");
  }
  if (fields.rd_rsp_tag > 0x7FF) {
    throw std::invalid_argument(
        "serialize_upli_rd_rsp: rd_rsp_tag out of range");
  }
  if (fields.rd_rsp_status > 0xF) {
    throw std::invalid_argument(
        "serialize_upli_rd_rsp: rd_rsp_status out of range");
  }

  // Calculate control header size
  const std::size_t ctrl_bits = kUpliRdRspFormat.total_bits();
  const std::size_t ctrl_bytes = (ctrl_bits + 7) / 8;

  // Total size: control header + 64-byte data
  std::vector<std::byte> buffer(ctrl_bytes + kUpliDataBeatBytes);

  // Serialize control fields
  bit_fields::NetworkBitWriter writer(std::span(buffer.data(), ctrl_bytes));
  writer.serialize(kUpliRdRspFormat,
                   fields.rd_rsp_vld ? 1U : 0U,
                   fields.rd_rsp_port_id,
                   fields.rd_rsp_tag,
                   fields.rd_rsp_status,
                   fields.rd_rsp_attr,
                   fields.rd_rsp_data_error ? 1U : 0U,
                   fields.rd_rsp_auth_tag,
                   0U);  // reserved

  // Copy data payload
  std::copy(fields.data.begin(), fields.data.end(),
            buffer.begin() + ctrl_bytes);

  return buffer;
}

UpliRdRspFields ualink::upli::deserialize_upli_rd_rsp(
    std::span<const std::byte> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  const std::size_t ctrl_bits = kUpliRdRspFormat.total_bits();
  const std::size_t ctrl_bytes = (ctrl_bits + 7) / 8;

  if (bytes.size() < ctrl_bytes + kUpliDataBeatBytes) {
    throw std::invalid_argument("deserialize_upli_rd_rsp: insufficient bytes");
  }

  UpliRdRspFields fields{};

  // Deserialize control fields
  bit_fields::NetworkBitReader reader(bytes.subspan(0, ctrl_bytes));
  std::uint8_t rd_rsp_vld_bit = 0;
  std::uint8_t rd_rsp_data_error_bit = 0;
  std::uint8_t reserved = 0;

  reader.deserialize_into(kUpliRdRspFormat,
                          rd_rsp_vld_bit,
                          fields.rd_rsp_port_id,
                          fields.rd_rsp_tag,
                          fields.rd_rsp_status,
                          fields.rd_rsp_attr,
                          rd_rsp_data_error_bit,
                          fields.rd_rsp_auth_tag,
                          reserved);

  fields.rd_rsp_vld = (rd_rsp_vld_bit != 0);
  fields.rd_rsp_data_error = (rd_rsp_data_error_bit != 0);

  // Copy data payload
  std::copy_n(bytes.data() + ctrl_bytes, kUpliDataBeatBytes,
              fields.data.begin());

  return fields;
}

// =============================================================================
// Write Response Channel Serialize/Deserialize
// =============================================================================

std::vector<std::byte> ualink::upli::serialize_upli_wr_rsp(
    const UpliWrRspFields& fields) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges
  if (fields.wr_rsp_port_id > 0x3) {
    throw std::invalid_argument(
        "serialize_upli_wr_rsp: wr_rsp_port_id out of range");
  }
  if (fields.wr_rsp_tag > 0x7FF) {
    throw std::invalid_argument(
        "serialize_upli_wr_rsp: wr_rsp_tag out of range");
  }
  if (fields.wr_rsp_status > 0xF) {
    throw std::invalid_argument(
        "serialize_upli_wr_rsp: wr_rsp_status out of range");
  }

  // Calculate buffer size
  const std::size_t total_bits = kUpliWrRspFormat.total_bits();
  const std::size_t total_bytes = (total_bits + 7) / 8;
  std::vector<std::byte> buffer(total_bytes);

  // Serialize using NetworkBitWriter
  bit_fields::NetworkBitWriter writer(buffer);
  writer.serialize(kUpliWrRspFormat,
                   fields.wr_rsp_vld ? 1U : 0U,
                   fields.wr_rsp_port_id,
                   fields.wr_rsp_tag,
                   fields.wr_rsp_status,
                   fields.wr_rsp_attr,
                   fields.wr_rsp_auth_tag,
                   0U);  // reserved

  return buffer;
}

UpliWrRspFields ualink::upli::deserialize_upli_wr_rsp(
    std::span<const std::byte> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  bit_fields::NetworkBitReader reader(bytes);
  UpliWrRspFields fields{};

  std::uint8_t wr_rsp_vld_bit = 0;
  std::uint8_t reserved = 0;

  reader.deserialize_into(kUpliWrRspFormat,
                          wr_rsp_vld_bit,
                          fields.wr_rsp_port_id,
                          fields.wr_rsp_tag,
                          fields.wr_rsp_status,
                          fields.wr_rsp_attr,
                          fields.wr_rsp_auth_tag,
                          reserved);

  fields.wr_rsp_vld = (wr_rsp_vld_bit != 0);
  return fields;
}

// =============================================================================
// Credit Return Serialize/Deserialize
// =============================================================================

std::vector<std::byte> ualink::upli::serialize_upli_credit_return(
    const UpliCreditReturn& credits) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges for all ports
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const auto& port = credits.ports[port_index];
    if (port.credit_vc > 0x3) {
      throw std::invalid_argument(
          "serialize_upli_credit_return: credit_vc out of range");
    }
    if (port.credit_num > 0x3) {
      throw std::invalid_argument(
          "serialize_upli_credit_return: credit_num out of range");
    }
  }

  // Calculate total size: 4 ports + 4 init_done flags
  const std::size_t port_bits = kUpliCreditPortFormat.total_bits();
  const std::size_t port_bytes = (port_bits + 7) / 8;
  const std::size_t total_bytes = (kMaxPorts * port_bytes) + 1;  // +1 for init_done flags

  std::vector<std::byte> buffer(total_bytes);

  // Serialize each port's credit info
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const auto& port = credits.ports[port_index];
    const std::size_t offset = port_index * port_bytes;

    bit_fields::NetworkBitWriter writer(
        std::span(buffer.data() + offset, port_bytes));
    writer.serialize(kUpliCreditPortFormat,
                     port.credit_vld ? 1U : 0U,
                     port.credit_pool ? 1U : 0U,
                     port.credit_vc,
                     port.credit_num);
  }

  // Pack init_done flags into last byte (4 bits)
  std::byte init_done_byte{0};
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    if (credits.credit_init_done[port_index]) {
      init_done_byte |= std::byte(1U << port_index);
    }
  }
  buffer[kMaxPorts * port_bytes] = init_done_byte;

  return buffer;
}

UpliCreditReturn ualink::upli::deserialize_upli_credit_return(
    std::span<const std::byte> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  const std::size_t port_bits = kUpliCreditPortFormat.total_bits();
  const std::size_t port_bytes = (port_bits + 7) / 8;
  const std::size_t expected_bytes = (kMaxPorts * port_bytes) + 1;

  if (bytes.size() < expected_bytes) {
    throw std::invalid_argument(
        "deserialize_upli_credit_return: insufficient bytes");
  }

  UpliCreditReturn credits{};

  // Deserialize each port's credit info
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const std::size_t offset = port_index * port_bytes;
    bit_fields::NetworkBitReader reader(
        bytes.subspan(offset, port_bytes));

    auto& port = credits.ports[port_index];
    std::uint8_t credit_vld_bit = 0;
    std::uint8_t credit_pool_bit = 0;

    reader.deserialize_into(kUpliCreditPortFormat,
                            credit_vld_bit,
                            credit_pool_bit,
                            port.credit_vc,
                            port.credit_num);

    port.credit_vld = (credit_vld_bit != 0);
    port.credit_pool = (credit_pool_bit != 0);
  }

  // Unpack init_done flags from last byte
  const std::byte init_done_byte = bytes[kMaxPorts * port_bytes];
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    credits.credit_init_done[port_index] =
        (init_done_byte & std::byte(1U << port_index)) != std::byte{0};
  }

  return credits;
}