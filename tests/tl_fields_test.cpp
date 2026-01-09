#include "ualink/tl_fields.h"
#include "ualink/trace.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

using namespace ualink::tl;

int main() {
  UALINK_TRACE_SCOPED(__func__);

  {
    UncompressedRequestField f{};
    f.cmd = 0x15;
    f.vchan = 0x2;
    f.asi = 0x1;
    f.tag = 0x321;
    f.pool = true;
    f.attr = 0xA5;
    f.len = 0x2A;
    f.metadata = 0xCC;
    f.addr = 0x123456789ABCULL & ((1ULL << 55) - 1ULL);
    f.srcaccid = 0x155;
    f.dstaccid = 0x2AA;
    f.cload = true;
    f.cway = 0x1;
    f.numbeats = 0x2;

    const auto bytes = serialize_uncompressed_request_field(f);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kUncompressedRequestFieldFormat);
    const std::array<bit_fields::ExpectedField, 15> expected{{
        {"ftype", static_cast<std::uint8_t>(TlFieldType::kUncompressedRequest)},
        {"cmd", f.cmd},
        {"vchan", f.vchan},
        {"asi", f.asi},
        {"tag", f.tag},
        {"pool", 1U},
        {"attr", f.attr},
        {"len", f.len},
        {"metadata", f.metadata},
        {"addr", f.addr},
        {"srcaccid", f.srcaccid},
        {"dstaccid", f.dstaccid},
        {"cload", 1U},
        {"cway", f.cway},
        {"numbeats", f.numbeats},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_uncompressed_request_field(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->cmd == f.cmd);
    assert(roundtrip->vchan == f.vchan);
    assert(roundtrip->asi == f.asi);
    assert(roundtrip->tag == f.tag);
    assert(roundtrip->pool == f.pool);
    assert(roundtrip->attr == f.attr);
    assert(roundtrip->len == f.len);
    assert(roundtrip->metadata == f.metadata);
    assert(roundtrip->addr == f.addr);
    assert(roundtrip->srcaccid == f.srcaccid);
    assert(roundtrip->dstaccid == f.dstaccid);
    assert(roundtrip->cload == f.cload);
    assert(roundtrip->cway == f.cway);
    assert(roundtrip->numbeats == f.numbeats);
  }

  {
    UncompressedResponseField f{};
    f.vchan = 0x1;
    f.tag = 0x3A1;
    f.pool = false;
    f.len = 0x2;
    f.offset = 0x3;
    f.status = 0x9;
    f.rd_wr = true;
    f.last = false;
    f.srcaccid = 0x1;
    f.dstaccid = 0x2;
    f.spares = 0xBEEF;

    const auto bytes = serialize_uncompressed_response_field(f);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kUncompressedResponseFieldFormat);
    const std::array<bit_fields::ExpectedField, 12> expected{{
        {"ftype", static_cast<std::uint8_t>(TlFieldType::kUncompressedResponse)},
        {"vchan", f.vchan},
        {"tag", f.tag},
        {"pool", 0U},
        {"len", f.len},
        {"offset", f.offset},
        {"status", f.status},
        {"rd_wr", 1U},
        {"last", 0U},
        {"srcaccid", f.srcaccid},
        {"dstaccid", f.dstaccid},
        {"spares", f.spares},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_uncompressed_response_field(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->vchan == f.vchan);
    assert(roundtrip->tag == f.tag);
    assert(roundtrip->pool == f.pool);
    assert(roundtrip->len == f.len);
    assert(roundtrip->offset == f.offset);
    assert(roundtrip->status == f.status);
    assert(roundtrip->rd_wr == f.rd_wr);
    assert(roundtrip->last == f.last);
    assert(roundtrip->srcaccid == f.srcaccid);
    assert(roundtrip->dstaccid == f.dstaccid);
    assert(roundtrip->spares == f.spares);
  }

  {
    CompressedRequestField f{};
    f.cmd = 0x5;
    f.vchan = 0x2;
    f.asi = 0x3;
    f.tag = 0x5A5;
    f.pool = true;
    f.len = 0x1;
    f.metadata = 0x6;
    f.addr = 0x2AAA;
    f.srcaccid = 0x12;
    f.dstaccid = 0x34;
    f.cway = 0x2;

    const auto bytes = serialize_compressed_request_field(f);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kCompressedRequestFieldFormat);
    const std::array<bit_fields::ExpectedField, 12> expected{{
        {"ftype", static_cast<std::uint8_t>(TlFieldType::kCompressedRequest)},
        {"cmd", f.cmd},
        {"vchan", f.vchan},
        {"asi", f.asi},
        {"tag", f.tag},
        {"pool", 1U},
        {"len", f.len},
        {"metadata", f.metadata},
        {"addr", f.addr},
        {"srcaccid", f.srcaccid},
        {"dstaccid", f.dstaccid},
        {"cway", f.cway},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_compressed_request_field(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->cmd == f.cmd);
    assert(roundtrip->vchan == f.vchan);
    assert(roundtrip->asi == f.asi);
    assert(roundtrip->tag == f.tag);
    assert(roundtrip->pool == f.pool);
    assert(roundtrip->len == f.len);
    assert(roundtrip->metadata == f.metadata);
    assert(roundtrip->addr == f.addr);
    assert(roundtrip->srcaccid == f.srcaccid);
    assert(roundtrip->dstaccid == f.dstaccid);
    assert(roundtrip->cway == f.cway);
  }

  {
    CompressedSingleBeatReadResponseField f{};
    f.vchan = 0x1;
    f.tag = 0x333;
    f.pool = false;
    f.dstaccid = 0x2AA;
    f.offset = 0x2;
    f.last = true;

    const auto bytes = serialize_compressed_single_beat_read_response_field(f);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kCompressedSingleBeatReadResponseFieldFormat);
    const std::array<bit_fields::ExpectedField, 8> expected{{
        {"ftype", static_cast<std::uint8_t>(TlFieldType::kCompressedResponseSingleBeatRead)},
        {"vchan", f.vchan},
        {"tag", f.tag},
        {"pool", 0U},
        {"dstaccid", f.dstaccid},
        {"offset", f.offset},
        {"last", 1U},
        {"spare", 0U},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_compressed_single_beat_read_response_field(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->vchan == f.vchan);
    assert(roundtrip->tag == f.tag);
    assert(roundtrip->pool == f.pool);
    assert(roundtrip->dstaccid == f.dstaccid);
    assert(roundtrip->offset == f.offset);
    assert(roundtrip->last == f.last);
  }

  {
    CompressedWriteOrMultiBeatReadResponseField f{};
    f.vchan = 0x3;
    f.tag = 0x7;
    f.pool = true;
    f.dstaccid = 0x1;
    f.len = 0x3;
    f.rd_wr = true;

    const auto bytes = serialize_compressed_write_or_multibeat_read_response_field(f);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kCompressedWriteOrMultiBeatReadResponseFieldFormat);
    const std::array<bit_fields::ExpectedField, 8> expected{{
        {"ftype", static_cast<std::uint8_t>(TlFieldType::kCompressedResponseWriteOrMultiBeatRead)},
        {"vchan", f.vchan},
        {"tag", f.tag},
        {"pool", 1U},
        {"dstaccid", f.dstaccid},
        {"len", f.len},
        {"rd_wr", 1U},
        {"spare", 0U},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_compressed_write_or_multibeat_read_response_field(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->vchan == f.vchan);
    assert(roundtrip->tag == f.tag);
    assert(roundtrip->pool == f.pool);
    assert(roundtrip->dstaccid == f.dstaccid);
    assert(roundtrip->len == f.len);
    assert(roundtrip->rd_wr == f.rd_wr);
  }

  {
    FlowControlNopField f{};
    f.req_cmd = 0x2A;
    f.rsp_cmd = 0x12;
    f.req_data = 0xFE;
    f.rsp_data = 0x01;

    const auto bytes = serialize_flow_control_nop_field(f);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kFlowControlNopFieldFormat);
    const std::array<bit_fields::ExpectedField, 5> expected{{
        {"ftype", static_cast<std::uint8_t>(TlFieldType::kFlowControlNop)},
        {"req_cmd", f.req_cmd},
        {"rsp_cmd", f.rsp_cmd},
        {"req_data", f.req_data},
        {"rsp_data", f.rsp_data},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_flow_control_nop_field(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->req_cmd == f.req_cmd);
    assert(roundtrip->rsp_cmd == f.rsp_cmd);
    assert(roundtrip->req_data == f.req_data);
    assert(roundtrip->rsp_data == f.rsp_data);
  }

  {
    // Type mismatch should fail.
    UncompressedResponseField f{};
    const auto bytes = serialize_uncompressed_response_field(f);
    const auto wrong = deserialize_compressed_request_field(bytes);
    assert(!wrong.has_value());
  }

  return 0;
}
