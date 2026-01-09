#pragma once

#include <cstdint>

namespace ualink::dl {

// DL link state (spec Figure 6-23: DL Link State).
// Note: The spec depicts "DL Up" as a super-state with sub-states (NOP/Idle/Fault).
// For the behavioral model we represent the named states explicitly.
enum class DlLinkState : std::uint8_t {
  kUp = 0,
  kNop = 1,
  kIdle = 2,
  kFault = 3,
};

[[nodiscard]] constexpr bool is_dl_up(DlLinkState s) noexcept {
  return s == DlLinkState::kUp || s == DlLinkState::kNop || s == DlLinkState::kIdle || s == DlLinkState::kFault;
}

} // namespace ualink::dl
