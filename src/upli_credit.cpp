#include "ualink/upli_credit.h"

#include <stdexcept>

using namespace ualink::upli;

UpliCreditManager::UpliCreditManager() {
  UALINK_TRACE_SCOPED(__func__);

  // Initialize with default configuration
  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    PortCreditConfig default_config{};
    port_config_[port_index] = default_config;
  }
}

void UpliCreditManager::configure_port(std::uint8_t port_id,
                                        const PortCreditConfig& config) {
  UALINK_TRACE_SCOPED(__func__);

  if (port_id >= kMaxPorts) {
    throw std::invalid_argument(
        "UpliCreditManager::configure_port: port_id out of range");
  }

  port_config_[port_id] = config;
  initialized_ = false;  // Need to re-initialize with new config
}

void UpliCreditManager::reset() {
  UALINK_TRACE_SCOPED(__func__);

  for (auto& port : port_state_) {
    port = PortCreditState{};
  }
  initialized_ = false;
}

void UpliCreditManager::initialize_credits() {
  UALINK_TRACE_SCOPED(__func__);

  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const auto& config = port_config_[port_index];
    auto& state = port_state_[port_index];

    state.use_pool = config.use_pool;

    if (config.use_pool) {
      // Pool-based credits
      state.pool_initial = config.pool_credits;
      state.pool_available = config.pool_credits;
    } else {
      // Per-VC credits
      for (std::size_t vc_index = 0; vc_index < kMaxVirtualChannels;
           ++vc_index) {
        if (config.vc_config[vc_index].enabled) {
          state.vc_state[vc_index].initial_credits =
              config.vc_config[vc_index].initial_credits;
          state.vc_state[vc_index].available_credits =
              config.vc_config[vc_index].initial_credits;
          state.vc_state[vc_index].init_done = true;
        }
      }
    }

    state.port_init_done = true;
  }

  initialized_ = true;
}

bool UpliCreditManager::has_credit(std::uint8_t port_id,
                                    std::uint8_t vc) const {
  UALINK_TRACE_SCOPED(__func__);

  validate_port_vc(port_id, vc);

  const auto& state = port_state_[port_id];

  if (!state.port_init_done) {
    return false;
  }

  if (state.use_pool) {
    return state.pool_available > 0;
  }

  return state.vc_state[vc].init_done &&
         state.vc_state[vc].available_credits > 0;
}

bool UpliCreditManager::consume_credit(std::uint8_t port_id, std::uint8_t vc) {
  UALINK_TRACE_SCOPED(__func__);

  validate_port_vc(port_id, vc);

  auto& state = port_state_[port_id];

  if (!has_credit(port_id, vc)) {
    // Track blocked sends
    if (state.use_pool) {
      // No per-VC stats for pool mode, skip stats update
    } else {
      state.vc_state[vc].stats.send_blocked_count++;
    }
    return false;
  }

  if (state.use_pool) {
    state.pool_available--;
  } else {
    state.vc_state[vc].available_credits--;
    state.vc_state[vc].stats.credits_consumed++;
    state.vc_state[vc].stats.credits_available =
        state.vc_state[vc].available_credits;
  }

  return true;
}

void UpliCreditManager::process_credit_return(const UpliCreditReturn& credits) {
  UALINK_TRACE_SCOPED(__func__);

  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const auto& port_credit = credits.ports[port_index];

    if (!port_credit.credit_vld) {
      continue;
    }

    // Decode credit count (0-3 encoding means 1-4 actual credits)
    const std::size_t credit_count = port_credit.credit_num + 1;

    auto& state = port_state_[port_index];

    if (port_credit.credit_pool) {
      // Pool credit return
      state.pool_available += credit_count;
      // Cap at initial credits to prevent overflow
      if (state.pool_available > state.pool_initial) {
        state.pool_available = state.pool_initial;
      }
    } else {
      // VC-specific credit return
      const std::uint8_t vc = port_credit.credit_vc;
      if (vc < kMaxVirtualChannels) {
        state.vc_state[vc].available_credits += credit_count;
        // Cap at initial credits
        if (state.vc_state[vc].available_credits >
            state.vc_state[vc].initial_credits) {
          state.vc_state[vc].available_credits =
              state.vc_state[vc].initial_credits;
        }
        state.vc_state[vc].stats.credits_returned += credit_count;
        state.vc_state[vc].stats.credits_available =
            state.vc_state[vc].available_credits;
      }
    }

    // Update init_done status
    if (credits.credit_init_done[port_index]) {
      state.port_init_done = true;
    }
  }
}

void UpliCreditManager::return_credits(std::uint8_t port_id, std::uint8_t vc,
                                        std::size_t count) {
  UALINK_TRACE_SCOPED(__func__);

  validate_port_vc(port_id, vc);

  auto& state = port_state_[port_id];

  if (state.use_pool) {
    state.pool_available += count;
    if (state.pool_available > state.pool_initial) {
      state.pool_available = state.pool_initial;
    }
  } else {
    state.vc_state[vc].available_credits += count;
    if (state.vc_state[vc].available_credits >
        state.vc_state[vc].initial_credits) {
      state.vc_state[vc].available_credits =
          state.vc_state[vc].initial_credits;
    }
    state.vc_state[vc].stats.credits_returned += count;
    state.vc_state[vc].stats.credits_available =
        state.vc_state[vc].available_credits;
  }
}

std::optional<UpliCreditReturn> UpliCreditManager::generate_credit_return() {
  UALINK_TRACE_SCOPED(__func__);

  // Simple implementation: return credits for VCs that have consumed credits
  // In a real implementation, this would be more sophisticated with batching

  UpliCreditReturn credits{};
  bool has_credits = false;

  for (std::size_t port_index = 0; port_index < kMaxPorts; ++port_index) {
    const auto& state = port_state_[port_index];

    if (!state.port_init_done) {
      continue;
    }

    credits.credit_init_done[port_index] = true;

    if (state.use_pool) {
      // For pool mode, could return pool credits
      // For now, skip in this simple implementation
      continue;
    }

    // Return credits for first VC that has consumed credits
    for (std::size_t vc_index = 0; vc_index < kMaxVirtualChannels;
         ++vc_index) {
      const auto& vc_state = state.vc_state[vc_index];
      if (vc_state.stats.credits_consumed > 0) {
        const std::size_t to_return =
            std::min(vc_state.stats.credits_consumed, std::size_t{4});
        if (to_return > 0) {
          credits.ports[port_index].credit_vld = true;
          credits.ports[port_index].credit_pool = false;
          credits.ports[port_index].credit_vc =
              static_cast<std::uint8_t>(vc_index);
          credits.ports[port_index].credit_num =
              static_cast<std::uint8_t>(to_return - 1);  // 0-3 encoding
          has_credits = true;
          break;  // Only one return per port in this simple implementation
        }
      }
    }
  }

  if (has_credits) {
    return credits;
  }

  return std::nullopt;
}

std::size_t UpliCreditManager::get_available_credits(std::uint8_t port_id,
                                                      std::uint8_t vc) const {
  UALINK_TRACE_SCOPED(__func__);

  validate_port_vc(port_id, vc);

  const auto& state = port_state_[port_id];

  if (!state.port_init_done) {
    return 0;
  }

  if (state.use_pool) {
    return state.pool_available;
  }

  if (!state.vc_state[vc].init_done) {
    return 0;
  }

  return state.vc_state[vc].available_credits;
}

bool UpliCreditManager::is_initialized(std::uint8_t port_id) const {
  UALINK_TRACE_SCOPED(__func__);

  if (port_id >= kMaxPorts) {
    throw std::invalid_argument(
        "UpliCreditManager::is_initialized: port_id out of range");
  }

  return port_state_[port_id].port_init_done;
}

const CreditStats& UpliCreditManager::get_stats(std::uint8_t port_id,
                                                 std::uint8_t vc) const {
  UALINK_TRACE_SCOPED(__func__);

  validate_port_vc(port_id, vc);

  return port_state_[port_id].vc_state[vc].stats;
}

const PortCreditState& UpliCreditManager::get_port_state(
    std::uint8_t port_id) const {
  UALINK_TRACE_SCOPED(__func__);

  if (port_id >= kMaxPorts) {
    throw std::invalid_argument(
        "UpliCreditManager::get_port_state: port_id out of range");
  }

  return port_state_[port_id];
}

void UpliCreditManager::validate_port_vc(std::uint8_t port_id,
                                          std::uint8_t vc) const {
  if (port_id >= kMaxPorts) {
    throw std::invalid_argument(
        "UpliCreditManager: port_id out of range");
  }
  if (vc >= kMaxVirtualChannels) {
    throw std::invalid_argument("UpliCreditManager: vc out of range");
  }
}