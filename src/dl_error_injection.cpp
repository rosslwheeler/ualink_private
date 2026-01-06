#include "ualink/dl_error_injection.h"

#include <algorithm>

using namespace ualink::dl;

DlErrorInjector::DlErrorInjector() {
  UALINK_TRACE_SCOPED(__func__);
}

void DlErrorInjector::enable() noexcept {
  enabled_ = true;
}

void DlErrorInjector::disable() noexcept {
  enabled_ = false;
}

bool DlErrorInjector::is_enabled() const noexcept {
  return enabled_;
}

void DlErrorInjector::set_policy(ErrorInjectionPolicy policy) noexcept {
  policy_ = std::move(policy);
}

ErrorType DlErrorInjector::get_next_error() {
  UALINK_TRACE_SCOPED(__func__);

  if (!enabled_ || !policy_) {
    return ErrorType::kNone;
  }

  return policy_();
}

DlFlit DlErrorInjector::inject_error(const DlFlit& flit, ErrorType error_type) {
  UALINK_TRACE_SCOPED(__func__);

  if (error_type == ErrorType::kNone) {
    return flit;
  }

  DlFlit corrupted = flit;

  if (error_type == ErrorType::kCrcCorruption) {
    // Flip bits in CRC to corrupt it
    corrupted.crc[0] = static_cast<std::byte>(std::to_integer<std::uint8_t>(corrupted.crc[0]) ^ 0xFF);
    corrupted.crc[1] = static_cast<std::byte>(std::to_integer<std::uint8_t>(corrupted.crc[1]) ^ 0xFF);
  }

  return corrupted;
}

bool DlErrorInjector::should_drop_flit() {
  UALINK_TRACE_SCOPED(__func__);

  if (!enabled_) {
    return false;
  }

  const ErrorType error = get_next_error();
  return error == ErrorType::kPacketDrop;
}

std::uint16_t DlErrorInjector::modify_sequence(std::uint16_t seq_no, ErrorType error_type) {
  UALINK_TRACE_SCOPED(__func__);

  if (error_type == ErrorType::kSequenceDup) {
    // Return last sequence (duplicate)
    return last_seq_;
  }

  if (error_type == ErrorType::kSequenceSkip) {
    // Skip ahead by 1
    last_seq_ = seq_no;
    return (seq_no + 1) % 512;
  }

  last_seq_ = seq_no;
  return seq_no;
}

RandomErrorPolicy::RandomErrorPolicy(double error_probability)
    : crc_corruption_prob_(error_probability),
      packet_drop_prob_(error_probability),
      sequence_error_prob_(error_probability) {
  UALINK_TRACE_SCOPED(__func__);
}

ErrorType RandomErrorPolicy::operator()() {
  UALINK_TRACE_SCOPED(__func__);

  const double rand_value = dist_(rng_);

  if (rand_value < crc_corruption_prob_) {
    return ErrorType::kCrcCorruption;
  }

  if (rand_value < crc_corruption_prob_ + packet_drop_prob_) {
    return ErrorType::kPacketDrop;
  }

  if (rand_value < crc_corruption_prob_ + packet_drop_prob_ + sequence_error_prob_) {
    // Randomly choose between duplicate and skip
    if (dist_(rng_) < 0.5) {
      return ErrorType::kSequenceDup;
    }
    return ErrorType::kSequenceSkip;
  }

  return ErrorType::kNone;
}

void RandomErrorPolicy::set_crc_corruption_probability(double prob) noexcept {
  crc_corruption_prob_ = prob;
}

void RandomErrorPolicy::set_packet_drop_probability(double prob) noexcept {
  packet_drop_prob_ = prob;
}

void RandomErrorPolicy::set_sequence_error_probability(double prob) noexcept {
  sequence_error_prob_ = prob;
}

PeriodicErrorPolicy::PeriodicErrorPolicy(std::size_t period, ErrorType error_type)
    : period_(period), error_type_(error_type) {
  UALINK_TRACE_SCOPED(__func__);
}

ErrorType PeriodicErrorPolicy::operator()() {
  UALINK_TRACE_SCOPED(__func__);

  counter_++;

  if (counter_ % period_ == 0) {
    return error_type_;
  }

  return ErrorType::kNone;
}

void PeriodicErrorPolicy::reset() noexcept {
  counter_ = 0;
}

BurstErrorPolicy::BurstErrorPolicy(std::size_t burst_start,
                                   std::size_t burst_length,
                                   ErrorType error_type)
    : burst_start_(burst_start),
      burst_length_(burst_length),
      error_type_(error_type) {
  UALINK_TRACE_SCOPED(__func__);
}

ErrorType BurstErrorPolicy::operator()() {
  UALINK_TRACE_SCOPED(__func__);

  counter_++;

  if (counter_ >= burst_start_ && counter_ < burst_start_ + burst_length_) {
    return error_type_;
  }

  return ErrorType::kNone;
}

void BurstErrorPolicy::reset() noexcept {
  counter_ = 0;
}
