#include "ualink/dl_messages.h"
#include "ualink/trace.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace ualink::dl;

int main() {
  UALINK_TRACE_SCOPED(__func__);

  {
    UartStreamResetRequest msg{};
    msg.all_streams = true;
    msg.stream_id = 0;
    msg.common = make_common(DlUartMessageType::kStreamResetRequest);

    const auto bytes = serialize_uart_stream_reset_request(msg);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kUartStreamResetRequestFormat);
    const std::array<bit_fields::ExpectedField, 7> expected{{
        {"_reserved_hi", 0U},
        {"all_streams", 1U},
        {"stream_id", msg.stream_id},
        {"mtype", msg.common.mtype},
        {"mclass", msg.common.mclass},
        {"_reserved", 0U},
        {"compressed", 0U},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_uart_stream_reset_request(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->all_streams == msg.all_streams);
    assert(roundtrip->stream_id == msg.stream_id);
    assert(roundtrip->common.mtype == msg.common.mtype);
    assert(roundtrip->common.mclass == msg.common.mclass);
  }

  {
    UartStreamResetResponse msg{};
    msg.status = 0x6;
    msg.all_streams = false;
    msg.stream_id = 0;
    msg.common = make_common(DlUartMessageType::kStreamResetResponse);

    const auto bytes = serialize_uart_stream_reset_response(msg);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kUartStreamResetResponseFormat);
    const std::array<bit_fields::ExpectedField, 8> expected{{
        {"_reserved_hi", 0U},
        {"status", msg.status},
        {"all_streams", 0U},
        {"stream_id", msg.stream_id},
        {"mtype", msg.common.mtype},
        {"mclass", msg.common.mclass},
        {"_reserved", 0U},
        {"compressed", 0U},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_uart_stream_reset_response(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->status == msg.status);
    assert(roundtrip->all_streams == msg.all_streams);
    assert(roundtrip->stream_id == msg.stream_id);
    assert(roundtrip->common.mtype == msg.common.mtype);
    assert(roundtrip->common.mclass == msg.common.mclass);
  }

  {
    UartStreamCreditUpdate msg{};
    msg.data_fc_seq = 0xAAA;
    msg.stream_id = 0;
    msg.common = make_common(DlUartMessageType::kStreamCreditUpdate);

    const auto bytes = serialize_uart_stream_credit_update(msg);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kUartStreamCreditUpdateFormat);
    const std::array<bit_fields::ExpectedField, 7> expected{{
        {"data_fc_seq", msg.data_fc_seq},
        {"_reserved_hi", 0U},
        {"stream_id", msg.stream_id},
        {"mtype", msg.common.mtype},
        {"mclass", msg.common.mclass},
        {"_reserved", 0U},
        {"compressed", 0U},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_uart_stream_credit_update(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->data_fc_seq == msg.data_fc_seq);
    assert(roundtrip->stream_id == msg.stream_id);
    assert(roundtrip->common.mtype == msg.common.mtype);
    assert(roundtrip->common.mclass == msg.common.mclass);
  }

  {
    UartStreamTransportMessage msg{};
    msg.stream_id = 0;
    msg.common = make_common(DlUartMessageType::kStreamTransportMessage);
    msg.payload_dwords = {0x11223344U, 0xAABBCCDDU};

    const auto bytes = serialize_uart_stream_transport_message(msg);
    assert(bytes.size() == 12);

    // Verify DW0 header bits.
    std::array<std::byte, 4> dw0{};
    for (std::size_t i = 0; i < 4; ++i) {
      dw0[i] = bytes[i];
    }

    bit_fields::NetworkBitReader r(dw0);
    const auto parsed = r.deserialize(kUartStreamTransportHeaderFormat);
    const std::array<bit_fields::ExpectedField, 7> expected{{
        {"length", 1U},
        {"_reserved_hi", 0U},
        {"stream_id", msg.stream_id},
        {"mtype", msg.common.mtype},
        {"mclass", msg.common.mclass},
        {"_reserved", 0U},
        {"compressed", 0U},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_uart_stream_transport_message(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->stream_id == msg.stream_id);
    assert(roundtrip->common.mtype == msg.common.mtype);
    assert(roundtrip->common.mclass == msg.common.mclass);
    assert(roundtrip->payload_dwords == msg.payload_dwords);
  }

  {
    TlRateNotification msg{};
    msg.rate = 0x4321;
    msg.ack = true;
    msg.common = make_common(DlBasicMessageType::kTlRateNotification);

    const auto bytes = serialize_tl_rate_notification(msg);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kTlRateNotificationFormat);
    const std::array<bit_fields::ExpectedField, 8> expected{{
        {"rate", msg.rate},
        {"_reserved0", 0U},
        {"ack", 1U},
        {"_reserved1", 0U},
        {"mtype", msg.common.mtype},
        {"mclass", msg.common.mclass},
        {"_reserved2", 0U},
        {"compressed", 0U},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_tl_rate_notification(bytes);
    assert(roundtrip.has_value());
    assert(roundtrip->rate == msg.rate);
    assert(roundtrip->ack == msg.ack);
    assert(roundtrip->common.mtype == msg.common.mtype);
    assert(roundtrip->common.mclass == msg.common.mclass);
  }

  {
    VendorDefinedPacketTypeLength msg{};
    msg.vendor_id = 0xBEEF;
    msg.type = 0x12;
    msg.length = 0x34;

    const auto bytes = serialize_vendor_defined_packet_type_length(msg);

    bit_fields::NetworkBitReader r(bytes);
    const auto parsed = r.deserialize(kVendorDefinedPacketTypeLengthFormat);
    const std::array<bit_fields::ExpectedField, 3> expected{{
        {"vendor_id", msg.vendor_id},
        {"type", msg.type},
        {"length", msg.length},
    }};
    r.assert_expected(parsed, expected);

    const auto roundtrip = deserialize_vendor_defined_packet_type_length(bytes);
    assert(roundtrip.vendor_id == msg.vendor_id);
    assert(roundtrip.type == msg.type);
    assert(roundtrip.length == msg.length);
  }

  return 0;
}
