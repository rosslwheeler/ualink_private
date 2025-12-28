#pragma once

#include <cstring>

#ifdef TRACY_ENABLE
#include <Tracy.hpp>
#endif

// UAlINK trace macros
#ifdef TRACY_ENABLE
#define UALINK_TRACE_SCOPED(name) ZoneScopedN(name)
#define UALINK_TRACE_FRAME_MARK() FrameMark
#else
#define UALINK_TRACE_SCOPED(name) ((void)0)
#define UALINK_TRACE_FRAME_MARK() ((void)0)
#endif
