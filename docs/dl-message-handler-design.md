# DL Message Handler Design

## Overview

This document describes the design for integrating DL message handling into the UALink Data Link layer. The goal is to enable DL messages to be packed into DLAltSector slots within DL flit segments, with proper arbitration, blocking, and multi-flit support.

## Background

### Current State
- **Messages structures**: All DL message types (Basic, Control, UART) are defined and can serialize/deserialize (see `dl_messages.h`)
- **Segment headers**: The `dl_alt_sector` bit exists in segment headers and is encoded/decoded
- **DL flit packing**: Currently only packs TL flits; no path to insert DL messages

### Requirements (from plan.md lines 110-128)
1. **DLAltSector usage**: Any segment may contain a DL alternative sector to carry DL messages
2. **Message packing**: All DL messages are 1 DWord (4 bytes), except UART Stream Transport (1-33 DWords)
3. **Message blocking**: While UART Stream Transport is transmitting, other DL messages are blocked
4. **Multi-flit spanning**: UART Stream Transport may span multiple flits
5. **Message arbitration**: Round-robin arbitration within groups (Basic/Control/UART), then across groups
6. **Timing requirements**: Basic requests requiring responses should respond within 1us (coarse simulation)

---

## Architecture Options

### Option A: Integrated Message Manager (Comprehensive)

```
┌─────────────────────────────────────────────────────────┐
│                  DlMessageManager                       │
├─────────────────────────────────────────────────────────┤
│ - Message Queues (Basic/Control/UART)                  │
│ - Round-robin Arbitration Logic                        │
│ - Multi-flit UART State Machine                        │
│ - Blocking/Priority Management                         │
│ - Response Timeout Tracking                            │
├─────────────────────────────────────────────────────────┤
│ + queue_message(variant<all message types>)            │
│ + get_next_message_for_segment() -> Option<DWord>      │
│ + process_received_message(DWord)                      │
│ + tick(delta_us) -> handle timeouts                    │
└─────────────────────────────────────────────────────────┘
                    │
                    │ used by
                    ▼
        ┌───────────────────────┐
        │   DlSerializer        │
        │   DlDeserializer      │
        └───────────────────────┘
```

**Pros:**
- Single responsibility for all message logic
- Clean separation from DL flit serialization
- Easy to test message logic independently
- Can evolve arbitration/timing without touching DL flit code

**Cons:**
- More upfront design work
- Adds another layer of abstraction
- May be over-engineered for current needs

---

### Option B: Minimalist Extension (Incremental)

```
┌─────────────────────────────────────────────────────────┐
│              DlSerializer / DlDeserializer              │
├─────────────────────────────────────────────────────────┤
│ Existing: TL flit packing                              │
│                                                         │
│ New:                                                    │
│ + serialize(..., optional<span<DWord>> dl_messages)    │
│ + deserialize(...) -> (TlFlits, vector<DWord>)         │
└─────────────────────────────────────────────────────────┘
                    │
                    │ caller handles
                    ▼
        ┌───────────────────────┐
        │  Application Code     │
        │  (UaLinkEndpoint?)    │
        │                       │
        │  - Manages queues     │
        │  - Calls serialize    │
        │    with messages      │
        └───────────────────────┘
```

**Pros:**
- Minimal change to existing code
- Simple to implement
- Flexible - caller decides arbitration policy
- Easy to understand

**Cons:**
- No built-in arbitration or blocking logic
- Caller must implement complex multi-flit UART handling
- Response timing logic scattered across codebase
- Harder to enforce spec requirements consistently

---

### Option C: Hybrid Approach (Recommended)

```
┌─────────────────────────────────────────────────────────┐
│                DlMessageQueue                           │
├─────────────────────────────────────────────────────────┤
│ Purpose: Queuing + Arbitration ONLY                    │
│                                                         │
│ - std::queue<BasicMsg>  basic_queue                    │
│ - std::queue<ControlMsg> control_queue                 │
│ - std::queue<UartMsg> uart_queue                       │
│ - RoundRobinState arbitration_state                    │
│ - UartTransportState uart_blocking_state               │
├─────────────────────────────────────────────────────────┤
│ + enqueue_basic(BasicMsg)                              │
│ + enqueue_control(ControlMsg)                          │
│ + enqueue_uart(UartMsg)                                │
│ + pop_next_dword() -> Option<DWord>                    │
│   // Returns next DWord using round-robin arbitration  │
│   // Handles UART blocking and multi-DWord messages    │
└─────────────────────────────────────────────────────────┘
                    │
                    │ composed with
                    ▼
┌─────────────────────────────────────────────────────────┐
│           DlSerializer / DlDeserializer                 │
├─────────────────────────────────────────────────────────┤
│ + serialize(TlFlits, DlMessageQueue&)                  │
│   // Calls queue.pop_next_dword() to fill segments     │
│                                                         │
│ + deserialize(DlFlit) -> (TlFlits, vector<DWord>)      │
│   // Extracts DWords from dl_alt_sector segments       │
└─────────────────────────────────────────────────────────┘
                    │
                    │ used by
                    ▼
┌─────────────────────────────────────────────────────────┐
│              DlMessageProcessor (Optional)              │
├─────────────────────────────────────────────────────────┤
│ Purpose: Response handling + Timing ONLY               │
│                                                         │
│ - Tracks pending requests (Device ID, Port ID, etc.)   │
│ - Timeout tracking (respond within 1us)                │
│ - Callbacks for received messages                      │
├─────────────────────────────────────────────────────────┤
│ + process_received_dword(DWord)                        │
│   // Deserialize, invoke callbacks                     │
│ + tick(delta_us)                                       │
│   // Check timeouts, generate responses                │
└─────────────────────────────────────────────────────────┘
```

**Pros:**
- Clean separation of concerns:
  - Queue: arbitration + blocking logic
  - Serializer: DL flit packing mechanics
  - Processor: higher-level protocol logic (optional)
- Each component is independently testable
- Can implement incrementally (Queue first, Processor later)
- Flexible composition model

**Cons:**
- More files/classes than Option B
- Requires coordination between components

---

## Detailed Design (Option C - Recommended)

### 1. DlMessageQueue

#### Purpose
Handle queuing, arbitration, and UART blocking for outbound DL messages.

#### Data Structures

```cpp
namespace ualink::dl {

// Discriminated union for all message types
using DlMessage = std::variant<
  NoOpMessage,
  TlRateNotification,
  DeviceIdMessage,
  PortIdMessage,
  UartStreamResetRequest,
  UartStreamResetResponse,
  UartStreamTransportMessage,
  UartStreamCreditUpdate,
  ChannelNegotiation
>;

enum class MessageGroup {
  kBasic,
  kControl,
  kUart,
  kNone  // No messages available
};

struct UartTransportState {
  std::vector<uint32_t> remaining_dwords;  // Payload DWords not yet sent
  bool in_progress{false};
};

class DlMessageQueue {
public:
  // Enqueue a message (determines group automatically)
  void enqueue(DlMessage msg);

  // Pop next DWord using round-robin arbitration
  // Returns nullopt if no messages available
  std::optional<std::array<std::byte, 4>> pop_next_dword();

  // Check if any messages are pending
  [[nodiscard]] bool has_pending_messages() const;

  // Statistics
  struct Stats {
    size_t basic_enqueued{0};
    size_t control_enqueued{0};
    size_t uart_enqueued{0};
    size_t basic_sent{0};
    size_t control_sent{0};
    size_t uart_sent{0};
    size_t uart_multi_flit_count{0};
  };
  [[nodiscard]] Stats get_stats() const;

private:
  std::queue<DlMessage> basic_queue_;
  std::queue<DlMessage> control_queue_;
  std::queue<DlMessage> uart_queue_;

  // Round-robin state: which group was last serviced
  MessageGroup last_served_group_{MessageGroup::kNone};

  // UART Stream Transport multi-DWord state
  UartTransportState uart_transport_state_;

  Stats stats_;

  // Helper: get next group to service (round-robin)
  MessageGroup select_next_group();

  // Helper: pop message from specific group queue
  std::optional<DlMessage> pop_from_group(MessageGroup group);

  // Helper: serialize a message to DWord(s)
  std::vector<std::array<std::byte, 4>> serialize_message(const DlMessage& msg);
};

} // namespace ualink::dl
```

#### Arbitration Algorithm

```
select_next_group():
  1. If UART transport in progress:
       return kUart (blocking mode)

  2. Start from group after last_served_group
  3. Check each group in round-robin order:
       - kBasic -> kControl -> kUart -> kBasic ...
  4. Return first group with non-empty queue
  5. If all empty, return kNone

pop_next_dword():
  1. If uart_transport_state.in_progress:
       - Pop next DWord from remaining_dwords
       - If remaining_dwords empty: clear in_progress
       - Return DWord

  2. group = select_next_group()
  3. If group == kNone: return nullopt

  4. msg = pop_from_group(group)
  5. dwords = serialize_message(msg)

  6. If msg is UartStreamTransportMessage with >1 DWord:
       - Store dwords[1..N] in uart_transport_state.remaining_dwords
       - Set uart_transport_state.in_progress = true

  7. Update last_served_group = group
  8. Return dwords[0]
```

---

### 2. DlSerializer Integration

#### Modified Signature

```cpp
class DlSerializer {
public:
  // New: accepts optional message queue
  [[nodiscard]] static DlFlit serialize(
    std::span<const TlFlit> tl_flits,
    const ExplicitFlitHeaderFields &header,
    DlMessageQueue* message_queue = nullptr,  // NEW
    std::size_t *flits_serialized = nullptr
  );

  // Existing methods remain...
};
```

#### Packing Algorithm (DL Messages Have Priority)

Per spec section 6.3.4, DL Alternative sectors are placed **FIRST** in the segment, before TL flits.

```
For each segment (0..4):
  1. payload_offset = 0

  2. If message_queue != nullptr:
       a. dword = message_queue->pop_next_dword()
       b. If dword.has_value():
            - Copy dword to segment payload at offset 0 (FIRST 4 bytes)
            - Set segment_header.dl_alt_sector = true
            - payload_offset = 4
       c. Else:
            - segment_header.dl_alt_sector = false
            - payload_offset = 0

  3. available_tl_space = segment_size - payload_offset
     // e.g., 128B segment - 4B DL msg = 124B for TL flits

  4. Pack TL flits into remaining space starting at payload_offset
     - Use current TL flit packing logic
     - May pack full flits or partial carry-over

  5. Zero-fill any remaining bytes after TL flits
```

**Example**: 128-byte segment with DL message pending:
- Bytes [0:3]: DL message DWord (4 bytes)
- Bytes [4:127]: TL flit data (124 bytes = 1 full TL flit + 60B partial/carry-over)
- `dl_alt_sector` bit = 1

**Example**: 128-byte segment with no DL message:
- Bytes [0:127]: TL flit data (128 bytes = 2 full TL flits)
- `dl_alt_sector` bit = 0

**Note**: Segment sizes are (128, 128, 128, 124, 120 bytes). One DL message per segment reduces TL capacity by exactly 4 bytes (1 sector) when present.

---

### 3. DlDeserializer Integration

#### Modified Signature

```cpp
struct DlDeserializedResult {
  std::vector<TlFlit> tl_flits;
  std::vector<std::array<std::byte, 4>> dl_message_dwords;  // NEW
};

class DlDeserializer {
public:
  // New: returns both TL flits and DL message DWords
  [[nodiscard]] static DlDeserializedResult deserialize_ex(const DlFlit &flit);

  // Legacy compatibility: returns only TL flits (existing behavior)
  [[nodiscard]] static std::vector<TlFlit> deserialize(const DlFlit &flit);

  // Existing methods with CRC/pacing remain...
};
```

#### Extraction Algorithm

```
For each segment (0..4):
  1. Deserialize segment header

  2. payload_offset = 0

  3. If segment_header.dl_alt_sector == true:
       a. Extract 4-byte DWord from segment payload at offset 0 (FIRST 4 bytes)
       b. Append to dl_message_dwords vector
       c. payload_offset = 4

  4. Extract TL flits from remaining segment starting at payload_offset
     - Use current TL flit extraction logic
     - Handles full flits and partial carry-over

  5. Continue to next segment

Return DlDeserializedResult { tl_flits, dl_message_dwords }
```

**Note**: The DL message is always at the **start** of the segment payload (bytes [0:3]), not after TL flits. This matches the transmit-side priority packing.

---

### 4. DlMessageProcessor (Optional - Phase 2)

#### Purpose
Higher-level protocol handling: responses, timeouts, callbacks.

```cpp
class DlMessageProcessor {
public:
  using MessageCallback = std::function<void(const DlMessage&)>;

  // Register callbacks for received messages
  void set_basic_message_callback(MessageCallback cb);
  void set_control_message_callback(MessageCallback cb);
  void set_uart_message_callback(MessageCallback cb);

  // Process received DWords (deserialize and invoke callbacks)
  void process_received_dwords(std::span<const std::array<std::byte, 4>> dwords);

  // Timeout tracking (call periodically)
  void tick(uint64_t delta_us);

  // Track pending requests expecting responses
  void track_pending_request(const DlMessage& request);

  struct Stats {
    size_t basic_received{0};
    size_t control_received{0};
    size_t uart_received{0};
    size_t timeouts{0};
  };
  [[nodiscard]] Stats get_stats() const;

private:
  MessageCallback basic_callback_;
  MessageCallback control_callback_;
  MessageCallback uart_callback_;

  // Track requests expecting responses (Device ID, Port ID, etc.)
  struct PendingRequest {
    DlMessage request;
    uint64_t timestamp_us;
  };
  std::vector<PendingRequest> pending_requests_;

  // UART Stream Transport reassembly state
  struct UartReassemblyState {
    std::vector<uint32_t> accumulated_dwords;
    uint8_t expected_length;
    bool in_progress{false};
  };
  UartReassemblyState uart_reassembly_;

  Stats stats_;

  // Helper: deserialize DWord to message
  std::optional<DlMessage> deserialize_dword(std::array<std::byte, 4> dword);
};
```

---

## Implementation Phases

### Phase 1: Core Message Queue + Serializer Integration (Minimal Viable)
- [ ] Implement `DlMessageQueue` with arbitration and UART blocking
- [ ] Add DLAltSector packing to `DlSerializer::serialize()`
- [ ] Add DLAltSector extraction to `DlDeserializer::deserialize_ex()`
- [ ] Unit tests for queue arbitration
- [ ] Integration test: pack single-DWord messages
- [ ] Integration test: pack UART Stream Transport (multi-DWord)

**Files to create/modify:**
- `include/ualink/dl_message_queue.h` (new)
- `src/dl_message_queue.cpp` (new)
- `tests/dl_message_queue_test.cpp` (new)
- `include/ualink/dl_flit.h` (modify: add DlDeserializedResult)
- `src/dl_flit.cpp` (modify: integrate queue in serialize, extract in deserialize)
- `tests/dl_flit_test.cpp` (modify: add DLAltSector packing tests)

**Estimated complexity**: Medium (2-3 days)

---

### Phase 2: Message Processor + Protocol Logic (Future)
- [ ] Implement `DlMessageProcessor` with callbacks
- [ ] Add timeout tracking for requests expecting responses
- [ ] UART Stream Transport reassembly on receive side
- [ ] End-to-end test: send Device ID request, receive response within 1us

**Files to create:**
- `include/ualink/dl_message_processor.h` (new)
- `src/dl_message_processor.cpp` (new)
- `tests/dl_message_processor_test.cpp` (new)

**Estimated complexity**: Medium (2-3 days)

---

### Phase 3: Endpoint Integration (Future)
- [ ] Integrate `DlMessageQueue` + `DlMessageProcessor` into `UaLinkEndpoint`
- [ ] Add public API for sending/receiving DL messages
- [ ] System-level tests with multiple endpoints

**Files to modify:**
- `include/ualink/ualink_endpoint.h`
- `src/ualink_endpoint.cpp`
- `tests/ualink_endpoint_test.cpp`

**Estimated complexity**: Low (1 day)

---

## Design Decisions (Resolved per UALink 200 Spec)

### Q1: DLAltSector Space Management ✅
**Question**: When a segment is full (2 TL flits = 128 bytes), and DL messages are pending, should we:
- A) Skip this segment, try next segment
- B) Reduce TL flit count to make room (prioritize DL messages)
- C) Buffer DL messages until space is naturally available

**Decision**: **Option B** - DL messages have priority and are placed FIRST.

**Spec Reference**: Section 6.3.4 (UALink 200 Rev 1.0, lines ~11181-11228):
> "DL Alternative sectors have priority, if an DL alternative sector is indicated then it is placed in the first sector of the segment, before the current TL Flit, or carry over from a previous TL Flit."

**Implementation**: When a DL message is pending:
1. Reserve first 4 bytes (1 sector) of segment for DL message
2. Pack TL flits in remaining space (124 bytes for 128B segments)
3. Set `dl_alt_sector` bit in segment header

---

### Q2: Multiple DWords per Segment ✅
**Question**: If a segment has 64 bytes free (space for 16 DWords), should we:
- A) Pack only 1 DWord per segment (conservative, matches "dl_alt_sector" singular naming)
- B) Pack multiple DWords to maximize throughput

**Decision**: **Option A** - Exactly ONE DWord (4 bytes) per segment.

**Spec Reference**: Lines ~11184-11185:
> "A DL message (Alternative sectors) takes up one sector (4-bytes)."

Segment header has single bit `DLAltSector` (not a count):
> "DLAltSector [0]: DL Alternative sector — 0b: No alternative sector in segment, 1b: DL alternative sector in segment"

**Multi-DWord Messages**: For UART Stream Transport (1-33 DWords), line ~12299:
> "UART Stream Transport Message shall be packed sequentially into each Segment, which may span multiple Flits."

**Implementation**: Multi-DWord messages span multiple segments/flits (e.g., 33 DWords = 33 segments ≈ 7 DL flits).

---

### Q3: UART Stream Transport Priority ✅
**Question**: During UART Stream Transport (blocking other messages), should Control messages bypass the block?
- A) No, UART blocks everything (matches spec: "other DL messages are blocked")
- B) Yes, Control has higher priority (better for critical channel negotiation)

**Decision**: **Option A** - UART blocks ALL other DL messages, no exceptions.

**Spec Reference**: Lines ~12301-12302:
> "UART Stream Transport Message shall be packed sequentially into each Segment, which may span multiple Flits. Other DL messages shall be blocked while the UART Stream Transport Message is transmitted."

**Implementation**: Once UART Stream Transport begins transmission, Basic and Control queues are blocked until the entire multi-DWord message completes. No bypass mechanism.

---

### Q4: Message Callback API ✅
**Question**: Should `DlMessageProcessor` use:
- A) Variant-based callbacks: `callback(const DlMessage&)` (type-erased)
- B) Type-specific callbacks: separate callback per message type
- C) No callbacks, return messages via `get_received_messages()` (pull model)

**Decision**: **Option B** - Type-specific callbacks for each message class.

**Rationale**: The spec defines three distinct message classes with different semantics:
- **Basic Messages**: Request/response pairs with 1µs response timeout requirements
- **Control Messages**: State machine for channel online/offline negotiation
- **UART Messages**: Credit-based flow control with stream reassembly

Each class requires different processing state (timeout tracking, state machines, reassembly), making type-specific callbacks more natural and type-safe.

**Implementation**:
```cpp
void set_basic_callback(std::function<void(const BasicMessage&)>);
void set_control_callback(std::function<void(const ControlMessage&)>);
void set_uart_callback(std::function<void(const UartMessage&)>);
```

---

### Q5: Integration with Existing `DlCommandProcessor` ✅
**Question**: The codebase has `dl_command.h` for ACK/Replay commands. Should:
- A) Keep separate (DL commands vs DL messages are different concepts)
- B) Unify into single message handler

**Decision**: **Option A** - Keep ACK/Replay and DL messages as separate domains.

**Rationale**: These are orthogonal mechanisms at different protocol layers:

| Feature | ACK/Replay Commands | DL Messages |
|---------|---------------------|-------------|
| **Location** | Flit header (3 bytes) | Segment payload (DLAltSector) |
| **Purpose** | Link-level reliable delivery | DL-to-DL management/configuration |
| **Spec Section** | 6.6 "Link Level Replay" | 6.4 "DL Messages" |
| **Operations** | Sequence numbering, ACK, Replay | Device ID, Port ID, Channel negotiation |
| **Encoding** | `op` field: 0b010=Ack, 0b011=Replay | `mclass`/`mtype` fields |

Every flit has a header (potential ACK/Replay), and any segment may independently have a DL message. Combining them would conflate distinct protocol layers.

**Implementation**: Maintain separate `DlCommandProcessor` (existing) and new `DlMessageQueue`/`DlMessageProcessor`.

---

## Testing Strategy

### Unit Tests (Per Component)
1. **DlMessageQueue**:
   - Test round-robin arbitration (Basic -> Control -> UART -> Basic)
   - Test UART blocking (while UART transport active, no Basic/Control served)
   - Test multi-DWord UART transport state machine
   - Test empty queue behavior

2. **DlSerializer with Messages**:
   - Test packing single DWord in segment with space
   - Test skipping segments without space
   - Test segment header `dl_alt_sector` bit set correctly
   - Test UART transport spanning multiple flits

3. **DlDeserializer with Messages**:
   - Test extracting DWords from segments with `dl_alt_sector=1`
   - Test ignoring segments with `dl_alt_sector=0`
   - Test correct DWord extraction offset calculation

### Integration Tests
1. **Round-trip**: Enqueue messages -> serialize -> deserialize -> verify messages match
2. **Interleaving**: TL flits + DL messages in same flit
3. **Multi-flit UART**: UART Stream Transport spanning 3+ flits
4. **Priority scenarios**: Queue mix of Basic/Control/UART, verify arbitration order

### System Tests (Future - Phase 3)
1. **Two-endpoint communication**: Send Device ID request, receive response
2. **Timeout handling**: Send request, verify timeout fires if no response in 1us (simulated time)

---

## Summary

**Recommended Approach**: **Option C (Hybrid)**
- Implement `DlMessageQueue` for arbitration logic
- Integrate queue into `DlSerializer`/`DlDeserializer` for packing/extraction
- Defer `DlMessageProcessor` (callbacks, timeouts) to Phase 2

**Phase 1 Deliverable**:
- DL messages can be enqueued, packed into segments, and extracted on the receive side
- Round-robin arbitration with UART blocking works correctly
- Multi-flit UART Stream Transport supported

**Next Steps**:
1. Review this design document
2. Decide on open questions (Q1-Q5)
3. Implement Phase 1
4. Test + iterate
5. Plan Phase 2 if needed

---

## Appendix: Message Size Reference

| Message Type | Size (DWords) | Group |
|--------------|---------------|-------|
| NoOp | 1 | Basic |
| TL Rate Notification | 1 | Basic |
| Device ID Request | 1 | Basic |
| Device ID Response | 1 | Basic |
| Port Number Request | 1 | Basic |
| Port Number Response | 1 | Basic |
| Channel Negotiation | 1 | Control |
| UART Stream Reset Request | 1 | UART |
| UART Stream Reset Response | 1 | UART |
| UART Stream Credit Update | 1 | UART |
| **UART Stream Transport** | **1-33** | **UART** |

Only UART Stream Transport is multi-DWord; all others fit in a single DLAltSector slot.
