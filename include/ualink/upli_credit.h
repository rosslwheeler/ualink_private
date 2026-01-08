#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "ualink/trace.h"
#include "ualink/upli_channel.h"

namespace ualink::upli {

// UPLI credit management constants
constexpr std::size_t kMaxVirtualChannels = 4;
constexpr std::size_t kDefaultCreditsPerVC = 16;
constexpr std::size_t kDefaultPoolCredits = 32;

// Credit configuration per virtual channel
struct VcCreditConfig {
  std::size_t initial_credits{kDefaultCreditsPerVC};
  bool enabled{true};
};

// Credit configuration per port
struct PortCreditConfig {
  std::array<VcCreditConfig, kMaxVirtualChannels> vc_config{};
  std::size_t pool_credits{kDefaultPoolCredits};
  bool use_pool{false};  // If true, use pool credits instead of per-VC
};

// Credit statistics for monitoring
struct CreditStats {
  std::size_t credits_consumed{0};
  std::size_t credits_returned{0};
  std::size_t credits_available{0};
  std::size_t send_blocked_count{0};  // Times send was blocked due to no credits
};

// Per-VC credit state
struct VcCreditState {
  std::size_t available_credits{0};
  std::size_t initial_credits{0};
  bool init_done{false};
  CreditStats stats{};
};

// Per-port credit state
struct PortCreditState {
  std::array<VcCreditState, kMaxVirtualChannels> vc_state{};
  std::size_t pool_available{0};
  std::size_t pool_initial{0};
  bool use_pool{false};
  bool port_init_done{false};
};

// UPLI Credit Manager
// Manages credit-based flow control for all ports and virtual channels
class UpliCreditManager {
 public:
  UpliCreditManager();

  // Configuration
  void configure_port(std::uint8_t port_id, const PortCreditConfig& config);
  void reset();
  void initialize_credits();

  // Credit consumption (when sending)
  [[nodiscard]] bool has_credit(std::uint8_t port_id, std::uint8_t vc) const;
  [[nodiscard]] bool consume_credit(std::uint8_t port_id, std::uint8_t vc);

  // Credit return processing (when receiving credit returns from remote)
  void process_credit_return(const UpliCreditReturn& credits);
  void return_credits(std::uint8_t port_id, std::uint8_t vc, std::size_t count);

  // Generate credit return message to send to remote
  [[nodiscard]] std::optional<UpliCreditReturn> generate_credit_return();

  // Query state
  [[nodiscard]] std::size_t get_available_credits(std::uint8_t port_id,
                                                   std::uint8_t vc) const;
  [[nodiscard]] bool is_initialized(std::uint8_t port_id) const;
  [[nodiscard]] const CreditStats& get_stats(std::uint8_t port_id,
                                              std::uint8_t vc) const;
  [[nodiscard]] const PortCreditState& get_port_state(
      std::uint8_t port_id) const;

 private:
  void validate_port_vc(std::uint8_t port_id, std::uint8_t vc) const;

  std::array<PortCreditState, kMaxPorts> port_state_{};
  std::array<PortCreditConfig, kMaxPorts> port_config_{};
  bool initialized_{false};
};

}  // namespace ualink::upli