#include "ualink/dl_error_injection.h"

#include <cassert>
#include <iostream>

#include "ualink/crc.h"
#include "ualink/dl_flit.h"
#include "ualink/trace.h"

using namespace ualink::dl;

static TlFlit make_test_flit(std::uint8_t seed) {
  UALINK_TRACE_SCOPED(__func__);
  TlFlit flit{};
  for (std::size_t byte_index = 0; byte_index < flit.data.size(); ++byte_index) {
    flit.data[byte_index] = std::byte{static_cast<unsigned char>(seed + byte_index)};
  }
  return flit;
}

static void test_error_injector_disabled() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;

  assert(!injector.is_enabled());

  const ErrorType error = injector.get_next_error();
  assert(error == ErrorType::kNone);

  std::cout << "test_error_injector_disabled: PASS\n";
}

static void test_error_injector_enable_disable() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;

  injector.enable();
  assert(injector.is_enabled());

  injector.disable();
  assert(!injector.is_enabled());

  std::cout << "test_error_injector_enable_disable: PASS\n";
}

static void test_crc_corruption_injection() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;

  // Create a valid flit
  std::array<TlFlit, 2> tl_flits{make_test_flit(0x10), make_test_flit(0x20)};
  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  const DlFlit original = DlSerializer::serialize(tl_flits, header);

  // Inject CRC corruption
  const DlFlit corrupted = injector.inject_error(original, ErrorType::kCrcCorruption);

  // CRC should be different
  assert(corrupted.crc != original.crc);

  // Payload should be unchanged
  assert(corrupted.payload == original.payload);

  std::cout << "test_crc_corruption_injection: PASS\n";
}

static void test_no_error_injection() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;

  std::array<TlFlit, 2> tl_flits{make_test_flit(0x10), make_test_flit(0x20)};
  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  const DlFlit original = DlSerializer::serialize(tl_flits, header);
  const DlFlit unchanged = injector.inject_error(original, ErrorType::kNone);

  // Should be identical
  assert(unchanged.crc == original.crc);
  assert(unchanged.payload == original.payload);

  std::cout << "test_no_error_injection: PASS\n";
}

static void test_periodic_error_policy() {
  UALINK_TRACE_SCOPED(__func__);
  PeriodicErrorPolicy policy(5, ErrorType::kCrcCorruption);

  // First 4 should be no error
  for (std::size_t call_index = 0; call_index < 4; ++call_index) {
    const ErrorType error = policy();
    assert(error == ErrorType::kNone);
  }

  // 5th should be error
  ErrorType error = policy();
  assert(error == ErrorType::kCrcCorruption);

  // Next 4 should be no error
  for (std::size_t call_index = 0; call_index < 4; ++call_index) {
    error = policy();
    assert(error == ErrorType::kNone);
  }

  // 10th should be error again
  error = policy();
  assert(error == ErrorType::kCrcCorruption);

  std::cout << "test_periodic_error_policy: PASS\n";
}

static void test_periodic_error_policy_reset() {
  UALINK_TRACE_SCOPED(__func__);
  PeriodicErrorPolicy policy(3, ErrorType::kPacketDrop);

  // Advance to 3rd
  ErrorType result1 = policy();
  (void)result1;
  ErrorType result2 = policy();
  (void)result2;
  ErrorType error = policy();
  assert(error == ErrorType::kPacketDrop);

  // Reset
  policy.reset();

  // Should take 3 again
  ErrorType result3 = policy();
  (void)result3;
  ErrorType result4 = policy();
  (void)result4;
  error = policy();
  assert(error == ErrorType::kPacketDrop);

  std::cout << "test_periodic_error_policy_reset: PASS\n";
}

static void test_burst_error_policy() {
  UALINK_TRACE_SCOPED(__func__);
  BurstErrorPolicy policy(5, 3, ErrorType::kCrcCorruption);

  // First 4 should be no error (start=5 is 0-indexed, so positions 0-4 are no error)
  for (std::size_t call_index = 0; call_index < 5; ++call_index) {
    const ErrorType error = policy();
    assert(error == ErrorType::kNone);
  }

  // Next 3 should be errors (positions 5,6,7)
  for (std::size_t call_index = 0; call_index < 3; ++call_index) {
    const ErrorType error = policy();
    assert(error == ErrorType::kCrcCorruption);
  }

  // After burst should be no error
  const ErrorType error = policy();
  assert(error == ErrorType::kNone);

  std::cout << "test_burst_error_policy: PASS\n";
}

static void test_burst_error_policy_reset() {
  UALINK_TRACE_SCOPED(__func__);
  BurstErrorPolicy policy(2, 2, ErrorType::kPacketDrop);

  // Advance past burst
  for (std::size_t call_index = 0; call_index < 10; ++call_index) {
    ErrorType result = policy();
    (void)result;
  }

  // Reset
  policy.reset();

  // First 2 should be no error
  ErrorType error = policy();
  assert(error == ErrorType::kNone);
  error = policy();
  assert(error == ErrorType::kNone);

  // Next 2 should be errors
  error = policy();
  assert(error == ErrorType::kPacketDrop);
  error = policy();
  assert(error == ErrorType::kPacketDrop);

  std::cout << "test_burst_error_policy_reset: PASS\n";
}

static void test_pack_with_error_injection_no_error() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;
  injector.enable();

  // Set policy that never injects errors
  injector.set_policy([]() { return ErrorType::kNone; });

  std::array<TlFlit, 2> tl_flits{make_test_flit(0x10), make_test_flit(0x20)};
  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::size_t packed = 0;
  const DlFlit flit = DlSerializer::serialize_with_error_injection(tl_flits, header, injector, &packed);

  assert(packed == 2);

  // Should unpack correctly with CRC check
  const auto unpacked = DlDeserializer::deserialize_with_crc_check(flit);
  assert(unpacked.has_value());
  assert(unpacked->size() == 2);

  std::cout << "test_pack_with_error_injection_no_error: PASS\n";
}

static void test_pack_with_error_injection_crc_corruption() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;
  injector.enable();

  // Set policy that always corrupts CRC
  injector.set_policy([]() { return ErrorType::kCrcCorruption; });

  std::array<TlFlit, 2> tl_flits{make_test_flit(0x10), make_test_flit(0x20)};
  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::size_t packed = 0;
  const DlFlit flit = DlSerializer::serialize_with_error_injection(tl_flits, header, injector, &packed);

  assert(packed == 2);

  // CRC check should fail
  const auto unpacked = DlDeserializer::deserialize_with_crc_check(flit);
  assert(!unpacked.has_value());  // CRC validation failed

  std::cout << "test_pack_with_error_injection_crc_corruption: PASS\n";
}

static void test_pack_with_error_injection_packet_drop() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;
  injector.enable();

  // Set policy that always drops packets
  injector.set_policy([]() { return ErrorType::kPacketDrop; });

  std::array<TlFlit, 2> tl_flits{make_test_flit(0x10), make_test_flit(0x20)};
  ExplicitFlitHeaderFields header{};
  header.op = 0;
  header.payload = true;
  header.flit_seq_no = 1;

  std::size_t packed = 0;
  const DlFlit flit = DlSerializer::serialize_with_error_injection(tl_flits, header, injector, &packed);

  assert(packed == 0);  // Dropped, nothing packed

  std::cout << "test_pack_with_error_injection_packet_drop: PASS\n";
}

static void test_sequence_modification_duplicate() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;

  // First call sets last_seq to 5
  const std::uint16_t seq1 = injector.modify_sequence(5, ErrorType::kNone);
  assert(seq1 == 5);

  // Duplicate should return previous (5)
  const std::uint16_t seq2 = injector.modify_sequence(6, ErrorType::kSequenceDup);
  assert(seq2 == 5);

  std::cout << "test_sequence_modification_duplicate: PASS\n";
}

static void test_sequence_modification_skip() {
  UALINK_TRACE_SCOPED(__func__);
  DlErrorInjector injector;

  // Skip should advance by 1
  const std::uint16_t seq = injector.modify_sequence(10, ErrorType::kSequenceSkip);
  assert(seq == 11);

  std::cout << "test_sequence_modification_skip: PASS\n";
}

int main() {
  UALINK_TRACE_SCOPED(__func__);

  test_error_injector_disabled();
  test_error_injector_enable_disable();
  test_crc_corruption_injection();
  test_no_error_injection();

  test_periodic_error_policy();
  test_periodic_error_policy_reset();
  test_burst_error_policy();
  test_burst_error_policy_reset();

  test_pack_with_error_injection_no_error();
  test_pack_with_error_injection_crc_corruption();
  test_pack_with_error_injection_packet_drop();

  test_sequence_modification_duplicate();
  test_sequence_modification_skip();

  std::cout << "\nAll error injection tests passed!\n";
  return 0;
}
