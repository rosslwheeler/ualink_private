#include "ualink/dl_link_state.h"
#include "ualink/trace.h"

#include <cassert>

using namespace ualink::dl;

int main() {
  UALINK_TRACE_SCOPED(__func__);

  static_assert(is_dl_up(DlLinkState::kUp));
  static_assert(is_dl_up(DlLinkState::kNop));
  static_assert(is_dl_up(DlLinkState::kIdle));
  static_assert(is_dl_up(DlLinkState::kFault));

  assert(static_cast<unsigned>(DlLinkState::kUp) == 0U);
  assert(static_cast<unsigned>(DlLinkState::kNop) == 1U);
  assert(static_cast<unsigned>(DlLinkState::kIdle) == 2U);
  assert(static_cast<unsigned>(DlLinkState::kFault) == 3U);

  return 0;
}
