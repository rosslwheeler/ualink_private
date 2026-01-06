#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>

#include "ualink/dl_flit.h"
#include "ualink/trace.h"

namespace ualink::dl {

// Error injection types
enum class ErrorType {
  kNone,           // No error
  kCrcCorruption,  // Corrupt CRC field
  kPacketDrop,     // Drop entire packet
  kSequenceDup,    // Duplicate sequence number
  kSequenceSkip,   // Skip sequence number
};

// Error injection policy - determines when/how to inject errors
using ErrorInjectionPolicy = std::function<ErrorType()>;

// Error injector for negative testing
class DlErrorInjector {
public:
  DlErrorInjector();

  // Enable/disable error injection
  void enable() noexcept;
  void disable() noexcept;
  [[nodiscard]] bool is_enabled() const noexcept;

  // Set error injection policy
  void set_policy(ErrorInjectionPolicy policy) noexcept;

  // Get next error to inject (based on policy)
  [[nodiscard]] ErrorType get_next_error();

  // Inject error into a DL flit
  [[nodiscard]] DlFlit inject_error(const DlFlit& flit, ErrorType error_type);

  // Check if flit should be dropped
  [[nodiscard]] bool should_drop_flit();

  // Modify sequence number for sequence errors
  [[nodiscard]] std::uint16_t modify_sequence(std::uint16_t seq_no, ErrorType error_type);

private:
  bool enabled_{false};
  ErrorInjectionPolicy policy_{};
  std::uint16_t last_seq_{0};
};

// Built-in error injection policies

// Random error policy - injects errors with specified probability
class RandomErrorPolicy {
public:
  explicit RandomErrorPolicy(double error_probability);

  [[nodiscard]] ErrorType operator()();

  // Set probabilities for specific error types
  void set_crc_corruption_probability(double prob) noexcept;
  void set_packet_drop_probability(double prob) noexcept;
  void set_sequence_error_probability(double prob) noexcept;

private:
  std::mt19937 rng_{std::random_device{}()};
  std::uniform_real_distribution<double> dist_{0.0, 1.0};

  double crc_corruption_prob_{0.0};
  double packet_drop_prob_{0.0};
  double sequence_error_prob_{0.0};
};

// Periodic error policy - injects error every N flits
class PeriodicErrorPolicy {
public:
  explicit PeriodicErrorPolicy(std::size_t period, ErrorType error_type);

  [[nodiscard]] ErrorType operator()();

  void reset() noexcept;

private:
  std::size_t period_{0};
  std::size_t counter_{0};
  ErrorType error_type_{ErrorType::kNone};
};

// Burst error policy - injects multiple consecutive errors
class BurstErrorPolicy {
public:
  explicit BurstErrorPolicy(std::size_t burst_start,
                            std::size_t burst_length,
                            ErrorType error_type);

  [[nodiscard]] ErrorType operator()();

  void reset() noexcept;

private:
  std::size_t burst_start_{0};
  std::size_t burst_length_{0};
  std::size_t counter_{0};
  ErrorType error_type_{ErrorType::kNone};
};

}  // namespace ualink::dl
