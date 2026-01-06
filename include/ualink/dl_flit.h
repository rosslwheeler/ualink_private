#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "bit_fields/bit_fields.h"
#include "ualink/trace.h"

namespace ualink::dl {

constexpr std::size_t kDlFlitBytes = 640;
constexpr std::size_t kDlPayloadBytes = 628;
constexpr std::size_t kDlSegmentCount = 5;
constexpr std::size_t kTlFlitBytes = 64;

inline constexpr std::array<std::size_t, kDlSegmentCount> kSegmentPayloadBytes = {
    128,
    128,
    128,
    124,
    120,
};

inline constexpr std::array<std::size_t, kDlSegmentCount> kSegmentPayloadOffsets = {
    0,
    128,
    256,
    384,
    508,
};

inline constexpr bit_fields::PacketFormat<5> kExplicitFlitHeaderFormat{{{
    {"op", 3},
    {"payload", 1},
    {"_reserved0", 3},
    {"flit_seq_no", 9},
    {"_reserved1", 8},
}}};

inline constexpr bit_fields::PacketFormat<5> kCommandFlitHeaderFormat{{{
    {"op", 3},
    {"payload", 1},
    {"ack_req_seq", 9},
    {"flit_seq_lo", 3},
    {"_reserved1", 8},
}}};

inline constexpr bit_fields::PacketFormat<6> kSegmentHeaderFormat{{{
    {"tl_flit1", 1},
    {"message1", 2},
    {"tl_flit0", 1},
    {"message0", 2},
    {"_reserved", 1},
    {"dl_alt_sector", 1},
}}};

struct TlFlit {
  std::array<std::byte, kTlFlitBytes> data{};
  std::uint8_t message_field{0};
};

struct ExplicitFlitHeaderFields {
  std::uint8_t op{0};
  bool payload{true};
  std::uint16_t flit_seq_no{0};
};

struct CommandFlitHeaderFields {
  std::uint8_t op{0};
  bool payload{true};
  std::uint16_t ack_req_seq{0};
  std::uint8_t flit_seq_lo{0};
};

struct SegmentHeaderFields {
  bool dl_alt_sector{false};
  std::uint8_t message0{0};
  bool tl_flit0_present{false};
  std::uint8_t message1{0};
  bool tl_flit1_present{false};
};

struct DlFlit {
  std::array<std::byte, 3> flit_header{};
  std::array<std::byte, kDlSegmentCount> segment_headers{};
  std::array<std::byte, kDlPayloadBytes> payload{};
  std::array<std::byte, 4> crc{};
};

[[nodiscard]] std::array<std::byte, 3> encode_explicit_flit_header(
    const ExplicitFlitHeaderFields& fields);
[[nodiscard]] ExplicitFlitHeaderFields decode_explicit_flit_header(
    std::span<const std::byte, 3> bytes);

[[nodiscard]] std::array<std::byte, 3> encode_command_flit_header(
    const CommandFlitHeaderFields& fields);
[[nodiscard]] CommandFlitHeaderFields decode_command_flit_header(
    std::span<const std::byte, 3> bytes);

[[nodiscard]] std::byte encode_segment_header(const SegmentHeaderFields& fields);
[[nodiscard]] SegmentHeaderFields decode_segment_header(std::byte value);

// Forward declarations to avoid circular dependency
class DlPacingController;
class DlErrorInjector;

class DlSerializer {
public:
  [[nodiscard]] static DlFlit serialize(std::span<const TlFlit> tl_flits,
                                  const ExplicitFlitHeaderFields& header,
                                  std::size_t* flits_serialized = nullptr);

  // Serialize with pacing controller
  [[nodiscard]] static DlFlit serialize_with_pacing(std::span<const TlFlit> tl_flits,
                                                const ExplicitFlitHeaderFields& header,
                                                DlPacingController& pacing,
                                                std::size_t* flits_serialized = nullptr);

  // Serialize with error injection
  [[nodiscard]] static DlFlit serialize_with_error_injection(std::span<const TlFlit> tl_flits,
                                                          const ExplicitFlitHeaderFields& header,
                                                          DlErrorInjector& error_injector,
                                                          std::size_t* flits_serialized = nullptr);
};

class DlDeserializer {
public:
  [[nodiscard]] static std::vector<TlFlit> deserialize(const DlFlit& flit);
  [[nodiscard]] static std::optional<std::vector<TlFlit>> deserialize_with_crc_check(const DlFlit& flit);

  // Deserialize with pacing controller for Rx rate adaptation
  [[nodiscard]] static std::vector<TlFlit> deserialize_with_pacing(const DlFlit& flit,
                                                               DlPacingController& pacing);
  [[nodiscard]] static std::optional<std::vector<TlFlit>> deserialize_with_crc_and_pacing(
      const DlFlit& flit,
      DlPacingController& pacing);
};

}  // namespace ualink::dl
