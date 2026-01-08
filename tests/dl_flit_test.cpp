#include "ualink/dl_flit.h"
#include "ualink/trace.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

using namespace ualink::dl;

static TlFlit make_flit(std::uint8_t seed, std::uint8_t message_bits) {
  UALINK_TRACE_SCOPED(__func__);
  TlFlit flit{};
  for (std::size_t byte_index = 0; byte_index < flit.data.size(); ++byte_index) {
    flit.data[byte_index] = std::byte{static_cast<unsigned char>(seed + byte_index)};
  }
  flit.message_field = message_bits & 0x3U;
  return flit;
}

static std::array<SegmentHeaderFields, kDlSegmentCount> expected_segments_for_flits(std::span<const TlFlit> tl_flits) {
  UALINK_TRACE_SCOPED(__func__);
  std::array<SegmentHeaderFields, kDlSegmentCount> segments{};

  for (std::size_t flit_index = 0; flit_index < tl_flits.size(); ++flit_index) {
    const std::size_t payload_offset = flit_index * kTlFlitBytes;
    std::size_t segment_index = kDlSegmentCount;
    for (std::size_t index = 0; index < kSegmentPayloadOffsets.size(); ++index) {
      const std::size_t segment_start = kSegmentPayloadOffsets[index];
      const std::size_t segment_end = segment_start + kSegmentPayloadBytes[index];
      if (payload_offset >= segment_start && payload_offset < segment_end) {
        segment_index = index;
        break;
      }
    }
    assert(segment_index < kDlSegmentCount);
    const std::size_t segment_start = kSegmentPayloadOffsets[segment_index];
    const std::size_t delta = payload_offset - segment_start;
    if (delta == 0) {
      segments[segment_index].tl_flit0_present = true;
      segments[segment_index].message0 = tl_flits[flit_index].message_field & 0x3U;
    } else {
      segments[segment_index].tl_flit1_present = true;
      segments[segment_index].message1 = tl_flits[flit_index].message_field & 0x3U;
    }
  }

  return segments;
}

static void expect_invalid_argument(void (*fn)()) {
  bool threw = false;
  try {
    fn();
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

int main() {
  UALINK_TRACE_SCOPED(__func__);
  const TlFlit first = make_flit(0x10, 1);
  const TlFlit second = make_flit(0x80, 2);

  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::array<TlFlit, 2> tl_flits{first, second};
  std::size_t packed = 0;
  const DlFlit flit = DlSerializer::serialize(tl_flits, header, &packed);

  assert(packed == 2);
  {
    std::array<std::byte, 1> buffer{flit.segment_headers[0]};
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kSegmentHeaderFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"tl_flit1", 1U},
        {"message1", 2U},
        {"tl_flit0", 1U},
        {"message0", 1U},
        {"dl_alt_sector", 0U},
    }};
    reader.assert_expected(parsed, expected);
  }

  const auto unpacked = DlDeserializer::deserialize(flit);
  assert(unpacked.size() == 2);
  assert(unpacked[0].message_field == 1);
  assert(unpacked[1].message_field == 2);
  assert(unpacked[0].data == first.data);
  assert(unpacked[1].data == second.data);

  // Verify deserialized segment header matches original by re-serializing and validating with bit_fields
  {
    // Reconstruct segment header from deserialized flits (both in segment 0)
    SegmentHeaderFields reconstructed{};
    reconstructed.tl_flit0_present = true;
    reconstructed.message0 = unpacked[0].message_field;
    reconstructed.tl_flit1_present = true;
    reconstructed.message1 = unpacked[1].message_field;
    reconstructed.dl_alt_sector = false;

    const std::byte re_serialized = serialize_segment_header(reconstructed);
    std::array<std::byte, 1> buffer{re_serialized};
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kSegmentHeaderFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"tl_flit1", 1U},
        {"message1", 2U},
        {"tl_flit0", 1U},
        {"message0", 1U},
        {"dl_alt_sector", 0U},
    }};
    reader.assert_expected(parsed, expected);
  }

  const auto header_bytes = serialize_explicit_flit_header(header);
  {
    std::array<std::byte, 3> buffer = header_bytes;
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kExplicitFlitHeaderFormat);
    const std::array<bit_fields::ExpectedField, 3> expected{{
        {"op", header.op},
        {"payload", header.payload ? 1U : 0U},
        {"flit_seq_no", header.flit_seq_no},
    }};
    reader.assert_expected(parsed, expected);
  }
  {
    std::array<std::byte, 3> buffer = header_bytes;
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kExplicitFlitHeaderFormat);
    const std::array<bit_fields::ExpectedField, 3> expected{{
        {"op", header.op},
        {"payload", header.payload ? 1U : 0U},
        {"flit_seq_no", header.flit_seq_no},
    }};
    reader.assert_expected(parsed, expected);
  }

  CommandFlitHeaderFields command_header{};
  command_header.op = 3;
  command_header.payload = false;
  command_header.ack_req_seq = 0x1FF;
  command_header.flit_seq_lo = 5;

  const auto command_bytes = serialize_command_flit_header(command_header);
  {
    std::array<std::byte, 3> buffer = command_bytes;
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kCommandFlitHeaderFormat);
    const std::array<bit_fields::ExpectedField, 4> expected{{
        {"op", command_header.op},
        {"payload", command_header.payload ? 1U : 0U},
        {"ack_req_seq", command_header.ack_req_seq},
        {"flit_seq_lo", command_header.flit_seq_lo},
    }};
    reader.assert_expected(parsed, expected);
  }
  {
    std::array<std::byte, 3> buffer = command_bytes;
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kCommandFlitHeaderFormat);
    const std::array<bit_fields::ExpectedField, 4> expected{{
        {"op", command_header.op},
        {"payload", command_header.payload ? 1U : 0U},
        {"ack_req_seq", command_header.ack_req_seq},
        {"flit_seq_lo", command_header.flit_seq_lo},
    }};
    reader.assert_expected(parsed, expected);
  }

  for (std::uint8_t tl_flit1 = 0; tl_flit1 <= 1; ++tl_flit1) {
    for (std::uint8_t tl_flit0 = 0; tl_flit0 <= 1; ++tl_flit0) {
      for (std::uint8_t message1 = 0; message1 <= 3; ++message1) {
        for (std::uint8_t message0 = 0; message0 <= 3; ++message0) {
          for (std::uint8_t dl_alt_sector = 0; dl_alt_sector <= 1; ++dl_alt_sector) {
            SegmentHeaderFields segment{};
            segment.tl_flit1_present = (tl_flit1 != 0);
            segment.tl_flit0_present = (tl_flit0 != 0);
            segment.message1 = message1;
            segment.message0 = message0;
            segment.dl_alt_sector = (dl_alt_sector != 0);

            const std::byte encoded = serialize_segment_header(segment);
            {
              std::array<std::byte, 1> buffer{encoded};
              bit_fields::NetworkBitReader reader(buffer);
              const auto parsed = reader.deserialize(kSegmentHeaderFormat);
              const std::array<bit_fields::ExpectedField, 5> expected{{
                  {"tl_flit1", tl_flit1},
                  {"message1", message1},
                  {"tl_flit0", tl_flit0},
                  {"message0", message0},
                  {"dl_alt_sector", dl_alt_sector},
              }};
              reader.assert_expected(parsed, expected);
            }
          }
        }
      }
    }
  }

  std::array<TlFlit, 8> many_flits{};
  for (std::size_t flit_index = 0; flit_index < many_flits.size(); ++flit_index) {
    many_flits[flit_index] =
        make_flit(static_cast<std::uint8_t>(0x10 + flit_index * 7), static_cast<std::uint8_t>(flit_index & 0x3U));
  }
  std::size_t packed_many = 0;
  const DlFlit packed_flit = DlSerializer::serialize(many_flits, header, &packed_many);
  assert(packed_many == many_flits.size());

  const auto expected_segments = expected_segments_for_flits(many_flits);
  for (std::size_t segment_index = 0; segment_index < kDlSegmentCount; ++segment_index) {
    std::array<std::byte, 1> buffer{packed_flit.segment_headers[segment_index]};
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kSegmentHeaderFormat);
    const SegmentHeaderFields &expected = expected_segments[segment_index];
    const std::array<bit_fields::ExpectedField, 5> expected_fields{{
        {"tl_flit1", expected.tl_flit1_present ? 1U : 0U},
        {"message1", expected.message1},
        {"tl_flit0", expected.tl_flit0_present ? 1U : 0U},
        {"message0", expected.message0},
        {"dl_alt_sector", expected.dl_alt_sector ? 1U : 0U},
    }};
    reader.assert_expected(parsed, expected_fields);
  }

  const auto unpacked_many = DlDeserializer::deserialize(packed_flit);
  assert(unpacked_many.size() == many_flits.size());
  for (std::size_t flit_index = 0; flit_index < many_flits.size(); ++flit_index) {
    assert(unpacked_many[flit_index].message_field == many_flits[flit_index].message_field);
    assert(unpacked_many[flit_index].data == many_flits[flit_index].data);
  }

  // Verify deserialized segment headers match original by reconstructing and validating with bit_fields
  // Build segment headers from deserialized flits
  std::array<SegmentHeaderFields, kDlSegmentCount> reconstructed_segments{};
  for (std::size_t flit_index = 0; flit_index < unpacked_many.size(); ++flit_index) {
    const std::size_t payload_offset = flit_index * kTlFlitBytes;
    std::size_t segment_index = kDlSegmentCount;
    for (std::size_t index = 0; index < kSegmentPayloadOffsets.size(); ++index) {
      const std::size_t segment_start = kSegmentPayloadOffsets[index];
      const std::size_t segment_end = segment_start + kSegmentPayloadBytes[index];
      if (payload_offset >= segment_start && payload_offset < segment_end) {
        segment_index = index;
        break;
      }
    }
    assert(segment_index < kDlSegmentCount);
    const std::size_t segment_start = kSegmentPayloadOffsets[segment_index];
    const std::size_t delta = payload_offset - segment_start;
    if (delta == 0) {
      reconstructed_segments[segment_index].tl_flit0_present = true;
      reconstructed_segments[segment_index].message0 = unpacked_many[flit_index].message_field;
    } else {
      reconstructed_segments[segment_index].tl_flit1_present = true;
      reconstructed_segments[segment_index].message1 = unpacked_many[flit_index].message_field;
    }
  }

  // Re-serialize and validate each segment header
  for (std::size_t segment_index = 0; segment_index < kDlSegmentCount; ++segment_index) {
    const std::byte re_encoded = serialize_segment_header(reconstructed_segments[segment_index]);
    std::array<std::byte, 1> buffer{re_encoded};
    bit_fields::NetworkBitReader reader(buffer);
    const auto parsed = reader.deserialize(kSegmentHeaderFormat);
    const SegmentHeaderFields &expected = expected_segments[segment_index];
    const std::array<bit_fields::ExpectedField, 5> expected_fields{{
        {"tl_flit1", expected.tl_flit1_present ? 1U : 0U},
        {"message1", expected.message1},
        {"tl_flit0", expected.tl_flit0_present ? 1U : 0U},
        {"message0", expected.message0},
        {"dl_alt_sector", expected.dl_alt_sector ? 1U : 0U},
    }};
    reader.assert_expected(parsed, expected_fields);
  }

  expect_invalid_argument([] {
    ExplicitFlitHeaderFields bad{};
    bad.op = 0x8;
    [[maybe_unused]] const auto encoded = serialize_explicit_flit_header(bad);
  });

  expect_invalid_argument([] {
    CommandFlitHeaderFields bad{};
    bad.ack_req_seq = 0x200;
    [[maybe_unused]] const auto encoded = serialize_command_flit_header(bad);
  });

  expect_invalid_argument([] {
    SegmentHeaderFields bad{};
    bad.message0 = 4;
    [[maybe_unused]] const auto encoded = serialize_segment_header(bad);
  });

  return 0;
}
