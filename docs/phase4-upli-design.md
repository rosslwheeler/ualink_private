# Phase 4: UPLI Interface Design

## Overview

This document details the design for implementing the UPLI (UALink Protocol Level Interface) as Phase 4 of the UALink behavioral model. UPLI sits at the top of the UALink stack, providing the logical signaling interface for exchanging data and control information between accelerators.

## UPLI Architecture

### Position in Stack
```
┌──────────────────────────────────────┐
│  UPLI (Phase 4) - This Design       │  ← Protocol Level Interface
├──────────────────────────────────────┤
│  TL (Phase 3) - Complete             │  ← Transaction Layer
├──────────────────────────────────────┤
│  DL (Phase 1-2) - Complete           │  ← Data Link Layer
├──────────────────────────────────────┤
│  PL - Assumed Ideal                  │  ← Physical Layer (stubbed)
└──────────────────────────────────────┘
```

### Key UPLI Characteristics

**From Specification:**
- Split Request/Response architecture
- 4 primary channels: Request, OrigData, RdRsp, WrRsp
- Per-port transaction tags (11 bits) for multiple outstanding requests
- Up to 1024 Accelerators (10-bit physical IDs)
- Data transfers up to 256 bytes (four 64-byte beats)
- Time Division Multiplexing (TDM) for up to 4 ports
- Credit-based flow control (per-port, per-channel)
- Virtual channels (2-bit VC field)
- Security features (64-bit authorization tags)

## UPLI Signal Groups and Bit Field Formats

Following the existing pattern in DL/TL layers, we use the `bit_fields` library to define packet formats with `PacketFormat`, and encode/decode with `NetworkBitWriter`/`NetworkBitReader`.

### 1. Request Channel Format

The UPLI Request channel carries all control information for reads/writes/atomics. Total size calculated from bit widths.

```cpp
namespace ualink::upli {

// UPLI Request channel format (control fields only, not including data)
// Note: Data payload (if any) travels on OrigData channel
inline constexpr bit_fields::PacketFormat<13> kUpliRequestFormat{{{
    {"req_vld", 1},              // Valid request beat
    {"req_port_id", 2},          // Port ID for TDM routing
    {"req_src_phys_acc_id", 10}, // Source accelerator physical ID
    {"req_dst_phys_acc_id", 10}, // Destination accelerator physical ID
    {"req_tag", 11},             // Transaction tag
    {"req_addr", 57},            // Request address
    {"req_cmd", 6},              // Command type encoding
    {"req_len", 6},              // Number of doublewords - 1
    {"req_num_beats", 2},        // Number of data beats on OrigData
    {"req_attr", 8},             // Extended attributes
    {"req_meta_data", 8},        // Control info or UPLI message type
    {"req_vc", 2},               // Virtual channel identifier
    {"req_auth_tag", 64},        // Authorization tag for security
}}};

// Request channel fields structure
struct UpliRequestFields {
  bool req_vld{false};
  std::uint8_t req_port_id{0};
  std::uint16_t req_src_phys_acc_id{0};
  std::uint16_t req_dst_phys_acc_id{0};
  std::uint16_t req_tag{0};
  std::uint64_t req_addr{0};
  std::uint8_t req_cmd{0};
  std::uint8_t req_len{0};
  std::uint8_t req_num_beats{0};
  std::uint8_t req_attr{0};
  std::uint8_t req_meta_data{0};
  std::uint8_t req_vc{0};
  std::uint64_t req_auth_tag{0};
};

// Encode/decode functions
[[nodiscard]] std::vector<std::byte> encode_upli_request(const UpliRequestFields& fields);
[[nodiscard]] UpliRequestFields decode_upli_request(std::span<const std::byte> bytes);

} // namespace ualink::upli
```

### 2. Originator Data Channel Format

Carries 64-byte data beats for write data, atomic operands, and UPLI message data.

```cpp
namespace ualink::upli {

// UPLI Originator Data channel format (per beat)
constexpr std::size_t kUpliDataBeatBytes = 64;

inline constexpr bit_fields::PacketFormat<4> kUpliOrigDataControlFormat{{{
    {"orig_data_vld", 1},        // Valid data beat
    {"orig_data_port_id", 2},    // Port ID
    {"orig_data_error", 1},      // Data error "poison" bit
    {"_reserved", 4},            // Reserved bits
}}};

struct UpliOrigDataFields {
  bool orig_data_vld{false};
  std::uint8_t orig_data_port_id{0};
  bool orig_data_error{false};
  std::array<std::byte, kUpliDataBeatBytes> data{};  // 64-byte payload
};

[[nodiscard]] std::vector<std::byte> encode_upli_orig_data(const UpliOrigDataFields& fields);
[[nodiscard]] UpliOrigDataFields decode_upli_orig_data(std::span<const std::byte> bytes);

} // namespace ualink::upli
```

### 3. Read Response Channel Format

Returns read data and status for read requests and AtomicR operations.

```cpp
namespace ualink::upli {

// UPLI Read Response channel format
inline constexpr bit_fields::PacketFormat<8> kUpliRdRspFormat{{{
    {"rd_rsp_vld", 1},           // Valid response beat
    {"rd_rsp_port_id", 2},       // Port ID
    {"rd_rsp_tag", 11},          // Transaction tag (matches ReqTag)
    {"rd_rsp_status", 4},        // Response status code
    {"rd_rsp_attr", 8},          // Response attributes
    {"rd_rsp_data_error", 1},    // Data error bit
    {"rd_rsp_auth_tag", 64},     // Authorization tag
    {"_reserved", 5},            // Reserved bits
}}};

struct UpliRdRspFields {
  bool rd_rsp_vld{false};
  std::uint8_t rd_rsp_port_id{0};
  std::uint16_t rd_rsp_tag{0};
  std::uint8_t rd_rsp_status{0};
  std::uint8_t rd_rsp_attr{0};
  bool rd_rsp_data_error{false};
  std::uint64_t rd_rsp_auth_tag{0};
  std::array<std::byte, kUpliDataBeatBytes> data{};  // 64-byte read data
};

[[nodiscard]] std::vector<std::byte> encode_upli_rd_rsp(const UpliRdRspFields& fields);
[[nodiscard]] UpliRdRspFields decode_upli_rd_rsp(std::span<const std::byte> bytes);

} // namespace ualink::upli
```

### 4. Write Response Channel Format

Returns completion status for writes and AtomicNR operations.

```cpp
namespace ualink::upli {

// UPLI Write Response channel format (no data payload)
inline constexpr bit_fields::PacketFormat<7> kUpliWrRspFormat{{{
    {"wr_rsp_vld", 1},           // Valid write response
    {"wr_rsp_port_id", 2},       // Port ID
    {"wr_rsp_tag", 11},          // Transaction tag (matches ReqTag)
    {"wr_rsp_status", 4},        // Response status code
    {"wr_rsp_attr", 8},          // Response attributes
    {"wr_rsp_auth_tag", 64},     // Authorization tag
    {"_reserved", 6},            // Reserved bits
}}};

struct UpliWrRspFields {
  bool wr_rsp_vld{false};
  std::uint8_t wr_rsp_port_id{0};
  std::uint16_t wr_rsp_tag{0};
  std::uint8_t wr_rsp_status{0};
  std::uint8_t wr_rsp_attr{0};
  std::uint64_t wr_rsp_auth_tag{0};
};

[[nodiscard]] std::vector<std::byte> encode_upli_wr_rsp(const UpliWrRspFields& fields);
[[nodiscard]] UpliWrRspFields decode_upli_wr_rsp(std::span<const std::byte> bytes);

} // namespace ualink::upli
```

### 5. Credit Return Format

Credit-based flow control signals (per-port, per-channel).

```cpp
namespace ualink::upli {

constexpr std::size_t kMaxPorts = 4;

// Per-port credit return format (replicated for each of 4 ports)
inline constexpr bit_fields::PacketFormat<4> kUpliCreditPortFormat{{{
    {"credit_vld", 1},           // Credit valid for this port
    {"credit_pool", 1},          // 0=VC-specific, 1=Pool credit
    {"credit_vc", 2},            // Virtual channel (0-3)
    {"credit_num", 2},           // Number of credits returned (encoding: 0=1 credit, 3=4 credits)
}}};

struct UpliCreditPortFields {
  bool credit_vld{false};
  bool credit_pool{false};      // false = VC-specific, true = pool
  std::uint8_t credit_vc{0};
  std::uint8_t credit_num{0};   // 0-3 encoding (actual credits = num + 1)
};

// Full credit return structure for all 4 ports
struct UpliCreditReturn {
  std::array<UpliCreditPortFields, kMaxPorts> ports{};
  std::array<bool, kMaxPorts> credit_init_done{};  // Initialization complete per port
};

[[nodiscard]] std::vector<std::byte> encode_upli_credit_return(const UpliCreditReturn& credits);
[[nodiscard]] UpliCreditReturn decode_upli_credit_return(std::span<const std::byte> bytes);

} // namespace ualink::upli
```

### 6. Parity and Error Detection

UPLI includes parity bits for error detection. These will be calculated automatically during encoding.

```cpp
namespace ualink::upli {

// Parity calculation helpers (even parity)
[[nodiscard]] bool calculate_even_parity(std::span<const std::byte> data);
[[nodiscard]] std::uint8_t calculate_data_parity_byte(std::span<const std::byte, 64> data);

} // namespace ualink::upli
```

## UPLI Command Encodings

```cpp
namespace upli {

enum class ReqCmd : std::uint8_t {
  kRead = 0x03,              // I/O coherent read
  kWrite = 0x28,             // I/O coherent write with byte enables
  kWriteFull = 0x29,         // Full cache-line write
  kUpliWriteMessage = 0x2A,  // Protocol message
  kAtomicR = 0x30,           // Atomic with data return
  kAtomicNR = 0x32,          // Atomic without data return
  // 0x00-0x02, 0x04-0x27, 0x2B-0x2F, 0x31, 0x33-0x3F: Vendor defined/reserved
};

enum class RspStatus : std::uint8_t {
  kOkay = 0b0000,                  // Normal completion
  kTargetAbort = 0b0010,           // End-target error
  kDecodeError = 0b0011,           // Address decode error
  kProtectionViolation = 0b0110,  // Security/protection check failure
  kCmpto = 0b1000,                 // Completion timeout
  kIsolate = 0b1111,               // Place originator in isolation (WrRsp only)
};

} // namespace upli
```

## UPLI Component Design

### Component 1: UPLI Channel Interfaces

**File:** `include/ualink/upli_channel.h`

This component provides the basic encode/decode functionality for UPLI channel signals, following the same pattern as DL/TL layers.

```cpp
namespace ualink::upli {

// Forward declaration for integration
class UpliCreditManager;

// Request channel interface (Originator → Completer)
class UpliRequestChannel {
 public:
  using RequestCallback = std::function<void(const UpliRequestFields&)>;

  void set_request_callback(RequestCallback callback);
  void send_request(const UpliRequestFields& request);
  [[nodiscard]] bool can_send(std::uint8_t port_id, std::uint8_t vc) const;

  // Connect to credit manager for flow control
  void connect_credit_manager(UpliCreditManager* credit_mgr);

 private:
  RequestCallback request_callback_;
  UpliCreditManager* credit_manager_{nullptr};
};

// Originator Data channel (Originator → Completer)
class UpliOrigDataChannel {
 public:
  using DataCallback = std::function<void(const UpliOrigDataFields&)>;

  void set_data_callback(DataCallback callback);
  void send_data(const UpliOrigDataFields& data);
  [[nodiscard]] bool can_send(std::uint8_t port_id) const;

  void connect_credit_manager(UpliCreditManager* credit_mgr);

 private:
  DataCallback data_callback_;
  UpliCreditManager* credit_manager_{nullptr};
};

// Read Response channel (Completer → Originator)
class UpliRdRspChannel {
 public:
  using RdRspCallback = std::function<void(const UpliRdRspFields&)>;

  void set_rd_rsp_callback(RdRspCallback callback);
  void send_rd_rsp(const UpliRdRspFields& response);
  [[nodiscard]] bool can_send(std::uint8_t port_id, std::uint8_t vc) const;

  void connect_credit_manager(UpliCreditManager* credit_mgr);

 private:
  RdRspCallback rd_rsp_callback_;
  UpliCreditManager* credit_manager_{nullptr};
};

// Write Response channel (Completer → Originator)
class UpliWrRspChannel {
 public:
  using WrRspCallback = std::function<void(const UpliWrRspFields&)>;

  void set_wr_rsp_callback(WrRspCallback callback);
  void send_wr_rsp(const UpliWrRspFields& response);
  [[nodiscard]] bool can_send(std::uint8_t port_id, std::uint8_t vc) const;

  void connect_credit_manager(UpliCreditManager* credit_mgr);

 private:
  WrRspCallback wr_rsp_callback_;
  UpliCreditManager* credit_manager_{nullptr};
};

} // namespace ualink::upli
```

**Implementation Notes for `upli_channel.cpp`:**

The encode/decode functions will use `NetworkBitWriter` and `NetworkBitReader` from the bit_fields library:

```cpp
// Example implementation pattern (following dl_flit.cpp):
std::vector<std::byte> ualink::upli::encode_upli_request(const UpliRequestFields& fields) {
  UALINK_TRACE_SCOPED(__func__);

  // Validate field ranges
  if (fields.req_src_phys_acc_id > 0x3FF) {  // 10 bits max
    throw std::invalid_argument("encode_upli_request: src_phys_acc_id out of range");
  }
  // ... more validations ...

  // Calculate buffer size from format
  const std::size_t total_bits = kUpliRequestFormat.total_bits();
  const std::size_t total_bytes = (total_bits + 7) / 8;
  std::vector<std::byte> buffer(total_bytes);

  // Encode using NetworkBitWriter
  bit_fields::NetworkBitWriter writer(buffer);
  writer.serialize(kUpliRequestFormat,
                   fields.req_vld ? 1U : 0U,
                   fields.req_port_id,
                   fields.req_src_phys_acc_id,
                   fields.req_dst_phys_acc_id,
                   fields.req_tag,
                   fields.req_addr,
                   fields.req_cmd,
                   fields.req_len,
                   fields.req_num_beats,
                   fields.req_attr,
                   fields.req_meta_data,
                   fields.req_vc,
                   fields.req_auth_tag);

  return buffer;
}

UpliRequestFields ualink::upli::decode_upli_request(std::span<const std::byte> bytes) {
  UALINK_TRACE_SCOPED(__func__);

  bit_fields::NetworkBitReader reader(bytes);
  UpliRequestFields fields{};

  std::uint8_t req_vld_bit = 0;
  reader.deserialize_into(kUpliRequestFormat,
                          req_vld_bit,
                          fields.req_port_id,
                          fields.req_src_phys_acc_id,
                          fields.req_dst_phys_acc_id,
                          fields.req_tag,
                          fields.req_addr,
                          fields.req_cmd,
                          fields.req_len,
                          fields.req_num_beats,
                          fields.req_attr,
                          fields.req_meta_data,
                          fields.req_vc,
                          fields.req_auth_tag);

  fields.req_vld = (req_vld_bit != 0);
  return fields;
}
```

### Component 2: UPLI Credit Manager

**File:** `include/ualink/upli_credit.h`

```cpp
namespace ualink::upli {

constexpr std::size_t kMaxPorts = 4;
constexpr std::size_t kMaxVCs = 4;

enum class CreditType {
  kVirtualChannel,  // VC-specific credit
  kPool,            // Pool credit (shared across VCs)
};

struct CreditPool {
  std::array<std::size_t, kMaxVCs> vc_credits{};  // Per-VC credits
  std::size_t pool_credits{0};                     // Shared pool
};

class UpliCreditManager {
 public:
  // Configuration
  void set_initial_credits(std::uint8_t port_id, std::uint8_t vc,
                          std::size_t credits);
  void set_pool_credits(std::uint8_t port_id, std::size_t credits);

  // Credit consumption (returns false if insufficient credits)
  [[nodiscard]] bool consume_credit(std::uint8_t port_id, std::uint8_t vc);

  // Credit return processing
  void return_credits(const UpliCreditReturn& credit_return);

  // Query
  [[nodiscard]] std::size_t available_credits(std::uint8_t port_id,
                                               std::uint8_t vc) const noexcept;
  [[nodiscard]] bool has_credits(std::uint8_t port_id,
                                 std::uint8_t vc) const noexcept;
  [[nodiscard]] bool is_init_done(std::uint8_t port_id) const noexcept;

  // Credit release (for initialization)
  UpliCreditReturn release_initial_credits(std::uint8_t port_id);

 private:
  std::array<CreditPool, kMaxPorts> port_credits_{};
  std::array<bool, kMaxPorts> init_done_{};
};

} // namespace ualink::upli
```

### Component 3: UPLI TDM Scheduler

**File:** `include/ualink/upli_tdm.h`

```cpp
namespace ualink::upli {

enum class BifurcationMode {
  kX4,  // One ×4 link (1 port)
  kX2,  // Two ×2 links (2 ports)
  kX1,  // Four ×1 links (4 ports)
};

class UpliTdmScheduler {
 public:
  explicit UpliTdmScheduler(BifurcationMode mode);

  // TDM cycle management
  void advance_cycle();
  [[nodiscard]] std::uint8_t current_port_id() const noexcept;
  [[nodiscard]] std::size_t num_active_ports() const noexcept;

  // Port availability
  [[nodiscard]] bool is_port_active(std::uint8_t port_id) const noexcept;
  [[nodiscard]] bool can_transmit_on_port(std::uint8_t port_id) const noexcept;

 private:
  BifurcationMode mode_;
  std::size_t cycle_counter_{0};
  std::size_t num_ports_{1};
};

} // namespace ualink::upli
```

### Component 4: UPLI Ordering Manager

**File:** `include/ualink/upli_ordering.h`

```cpp
namespace ualink::upli {

constexpr std::size_t kRegionAlignmentBytes = 256;

enum class OrderingMode {
  kRelaxed,  // VC-based ordering only
  kStrict,   // Global strict ordering (auth/encryption enabled)
};

class UpliOrderingManager {
 public:
  explicit UpliOrderingManager(OrderingMode mode);

  // Port assignment for requests
  [[nodiscard]] std::uint8_t get_required_port(std::uint64_t address) const;

  // Ordering validation
  [[nodiscard]] bool can_issue_request(std::uint64_t address,
                                       std::uint8_t port_id,
                                       std::uint8_t vc) const;

  // Track in-flight requests
  void track_request(std::uint64_t address, std::uint8_t port_id,
                    std::uint8_t vc, std::uint16_t tag);
  void complete_request(std::uint16_t tag);

 private:
  OrderingMode mode_;

  struct InFlightRequest {
    std::uint64_t address{0};
    std::uint8_t port_id{0};
    std::uint8_t vc{0};
    std::uint16_t tag{0};
  };
  std::vector<InFlightRequest> in_flight_{};

  [[nodiscard]] std::uint64_t get_region_base(std::uint64_t address) const;
  [[nodiscard]] bool addresses_in_same_region(std::uint64_t addr1,
                                               std::uint64_t addr2) const;
};

} // namespace ualink::upli
```

### Component 5: UPLI Originator

**File:** `include/ualink/upli_originator.h`

```cpp
namespace ualink::upli {

struct UpliOriginatorConfig {
  std::uint16_t physical_acc_id{0};        // 10 bits: This accelerator's ID
  BifurcationMode bifurcation{BifurcationMode::kX4};
  OrderingMode ordering{OrderingMode::kRelaxed};
  bool enable_auth{false};                 // Enable authorization tags
  std::size_t max_outstanding_requests{64};
};

class UpliOriginator {
 public:
  explicit UpliOriginator(const UpliOriginatorConfig& config);

  // Reset and initialization
  void reset();
  [[nodiscard]] bool is_reset_complete() const noexcept;
  void process_credit_init(const UpliCreditReturn& credits);

  // Request API
  std::optional<std::uint16_t> send_read(std::uint16_t dst_acc_id,
                                         std::uint64_t address,
                                         std::uint8_t len,
                                         std::uint8_t vc = 0);

  std::optional<std::uint16_t> send_write(std::uint16_t dst_acc_id,
                                          std::uint64_t address,
                                          std::span<const std::byte> data,
                                          std::uint8_t vc = 0);

  std::optional<std::uint16_t> send_atomic(std::uint16_t dst_acc_id,
                                           std::uint64_t address,
                                           ReqCmd atomic_type,
                                           std::span<const std::byte> operand,
                                           std::uint8_t vc = 0);

  // Response processing
  void process_rd_rsp(const UpliRdRspBeat& beat);
  void process_wr_rsp(const UpliWrRspBeat& beat);

  // Callbacks for completions
  using ReadCompletionCallback =
      std::function<void(std::uint16_t tag, RspStatus status,
                        std::span<const std::byte> data)>;
  using WriteCompletionCallback =
      std::function<void(std::uint16_t tag, RspStatus status)>;

  void set_read_completion_callback(ReadCompletionCallback callback);
  void set_write_completion_callback(WriteCompletionCallback callback);

  // Channel connections
  void connect_request_channel(UpliRequestChannel* channel);
  void connect_orig_data_channel(UpliOrigDataChannel* channel);
  void connect_credit_return(UpliCreditManager* credit_mgr);

  // Statistics
  struct Stats {
    std::size_t requests_sent{0};
    std::size_t read_completions{0};
    std::size_t write_completions{0};
    std::size_t credit_stalls{0};
    std::size_t ordering_stalls{0};
  };
  [[nodiscard]] Stats get_stats() const noexcept;

 private:
  UpliOriginatorConfig config_;

  // Sub-components
  UpliTdmScheduler tdm_scheduler_;
  UpliOrderingManager ordering_manager_;
  UpliCreditManager* credit_manager_{nullptr};

  // Channel connections
  UpliRequestChannel* request_channel_{nullptr};
  UpliOrigDataChannel* orig_data_channel_{nullptr};

  // Tag management
  std::uint16_t next_tag_{0};
  std::map<std::uint16_t, InFlightTransaction> outstanding_transactions_;

  // Callbacks
  ReadCompletionCallback read_completion_callback_;
  WriteCompletionCallback write_completion_callback_;

  // Statistics
  Stats stats_{};

  // Helper methods
  [[nodiscard]] std::optional<std::uint16_t> allocate_tag();
  void free_tag(std::uint16_t tag);
  [[nodiscard]] std::uint8_t calculate_num_beats(std::size_t data_size) const;
};

} // namespace ualink::upli
```

### Component 6: UPLI Completer

**File:** `include/ualink/upli_completer.h`

```cpp
namespace ualink::upli {

struct UpliCompleterConfig {
  std::uint16_t physical_acc_id{0};        // 10 bits: This accelerator's ID
  BifurcationMode bifurcation{BifurcationMode::kX4};
  bool enable_auth{false};
  std::uint64_t address_base{0};           // Base address for this completer
  std::uint64_t address_size{0};           // Address space size
};

class UpliCompleter {
 public:
  explicit UpliCompleter(const UpliCompleterConfig& config);

  // Reset and initialization
  void reset();
  void send_initial_credits();

  // Request processing callbacks
  using ReadRequestCallback =
      std::function<void(std::uint16_t src_acc_id, std::uint16_t tag,
                        std::uint64_t address, std::uint8_t len,
                        std::uint8_t vc)>;
  using WriteRequestCallback =
      std::function<void(std::uint16_t src_acc_id, std::uint16_t tag,
                        std::uint64_t address, std::span<const std::byte> data,
                        std::uint8_t vc)>;
  using AtomicRequestCallback =
      std::function<void(std::uint16_t src_acc_id, std::uint16_t tag,
                        std::uint64_t address, ReqCmd atomic_type,
                        std::span<const std::byte> operand, std::uint8_t vc)>;

  void set_read_request_callback(ReadRequestCallback callback);
  void set_write_request_callback(WriteRequestCallback callback);
  void set_atomic_request_callback(AtomicRequestCallback callback);

  // Request reception
  void process_request(const UpliRequestBeat& beat);
  void process_orig_data(const UpliOrigDataBeat& beat);

  // Response API
  void send_read_response(std::uint16_t tag, RspStatus status,
                         std::span<const std::byte> data, std::uint8_t vc = 0);
  void send_write_response(std::uint16_t tag, RspStatus status,
                          std::uint8_t vc = 0);

  // Channel connections
  void connect_rd_rsp_channel(UpliRdRspChannel* channel);
  void connect_wr_rsp_channel(UpliWrRspChannel* channel);
  void connect_credit_manager(UpliCreditManager* credit_mgr);

  // Statistics
  struct Stats {
    std::size_t requests_received{0};
    std::size_t read_responses_sent{0};
    std::size_t write_responses_sent{0};
    std::size_t decode_errors{0};
    std::size_t protection_violations{0};
  };
  [[nodiscard]] Stats get_stats() const noexcept;

 private:
  UpliCompleterConfig config_;

  // Sub-components
  UpliTdmScheduler tdm_scheduler_;
  UpliCreditManager* credit_manager_{nullptr};

  // Channel connections
  UpliRdRspChannel* rd_rsp_channel_{nullptr};
  UpliWrRspChannel* wr_rsp_channel_{nullptr};

  // Multi-beat data assembly
  struct PendingWrite {
    std::uint16_t tag{0};
    std::uint8_t expected_beats{0};
    std::uint8_t received_beats{0};
    std::vector<std::byte> data{};
  };
  std::map<std::uint16_t, PendingWrite> pending_writes_;

  // Callbacks
  ReadRequestCallback read_request_callback_;
  WriteRequestCallback write_request_callback_;
  AtomicRequestCallback atomic_request_callback_;

  // Statistics
  Stats stats_{};

  // Helper methods
  [[nodiscard]] bool is_address_valid(std::uint64_t address) const;
  void assemble_write_data(std::uint16_t tag, const UpliOrigDataBeat& beat);
};

} // namespace ualink::upli
```

## Integration with Existing Layers

### Mapping UPLI to TL

The UPLI layer will interface with the existing Transaction Layer (TL) through the following mapping:

```cpp
// UPLI Request → TL Request conversion
TlReadRequest upli_to_tl_read(const UpliRequestBeat& upli_req) {
  TlReadRequest tl_req;
  tl_req.header.opcode = TlOpcode::kReadRequest;
  tl_req.header.address = upli_req.req_addr;
  tl_req.header.tag = upli_req.req_tag;
  tl_req.header.size = upli_req.req_len;
  return tl_req;
}

// TL Response → UPLI Response conversion
UpliRdRspBeat tl_to_upli_read_rsp(const TlReadResponse& tl_rsp,
                                  std::uint8_t port_id) {
  UpliRdRspBeat upli_rsp;
  upli_rsp.rd_rsp_vld = true;
  upli_rsp.rd_rsp_port_id = port_id;
  upli_rsp.rd_rsp_tag = tl_rsp.header.tag;
  upli_rsp.rd_rsp_status = static_cast<std::uint8_t>(tl_rsp.header.status);
  std::copy(tl_rsp.data.begin(), tl_rsp.data.end(), upli_rsp.rd_rsp_data.begin());
  return upli_rsp;
}
```

### UaLinkEndpoint Extension

Extend the existing `UaLinkEndpoint` to include UPLI interfaces:

```cpp
class UaLinkEndpointWithUpli : public UaLinkEndpoint {
 public:
  // UPLI Originator interface
  UpliOriginator& get_upli_originator();

  // UPLI Completer interface
  UpliCompleter& get_upli_completer();

  // Initialize UPLI layer
  void initialize_upli(const UpliOriginatorConfig& orig_config,
                      const UpliCompleterConfig& compl_config);

 private:
  std::unique_ptr<UpliOriginator> upli_originator_;
  std::unique_ptr<UpliCompleter> upli_completer_;
  std::unique_ptr<UpliCreditManager> upli_credit_manager_;

  // Internal channels for UPLI-to-TL bridging
  UpliRequestChannel request_channel_;
  UpliOrigDataChannel orig_data_channel_;
  UpliRdRspChannel rd_rsp_channel_;
  UpliWrRspChannel wr_rsp_channel_;
};
```

## Implementation Phases

### Phase 4.1: Basic Channel Infrastructure
**Files:** `upli_channel.h`, `upli_channel.cpp`, `upli_channel_test.cpp`

- Define all signal structures
- Implement basic channel send/receive
- Add parity calculation helpers
- Unit tests for signal packing/unpacking

### Phase 4.2: Credit Management
**Files:** `upli_credit.h`, `upli_credit.cpp`, `upli_credit_test.cpp`

- Implement credit pools (VC-specific and shared)
- Credit consumption and return logic
- Initialization sequence
- Unit tests for credit scenarios

### Phase 4.3: TDM Scheduling
**Files:** `upli_tdm.h`, `upli_tdm.cpp`, `upli_tdm_test.cpp`

- Bifurcation mode support
- Port scheduling logic
- Cycle advancement
- Unit tests for all bifurcation modes

### Phase 4.4: Ordering Rules
**Files:** `upli_ordering.h`, `upli_ordering.cpp`, `upli_ordering_test.cpp`

- 256-byte region alignment
- Port assignment based on address
- In-flight tracking per VC
- Strict vs relaxed ordering modes
- Unit tests for ordering constraints

### Phase 4.5: UPLI Originator
**Files:** `upli_originator.h`, `upli_originator.cpp`, `upli_originator_test.cpp`

- High-level request API (read/write/atomic)
- Tag management
- Multi-beat data transmission
- Response processing
- Integration tests

### Phase 4.6: UPLI Completer
**Files:** `upli_completer.h`, `upli_completer.cpp`, `upli_completer_test.cpp`

- Request reception and dispatch
- Multi-beat data assembly
- Response transmission
- Address validation
- Integration tests

### Phase 4.7: End-to-End Integration
**Files:** `upli_integration_test.cpp`

- Originator ↔ Completer scenarios
- Credit flow validation
- Ordering enforcement
- Error injection and recovery
- Multi-port TDM validation

## Testing Strategy

### Unit Tests (Per Component)

Each component gets comprehensive unit tests:

```cpp
// Example: upli_credit_test.cpp
TEST(UpliCreditManager, ConsumeAndReturnCredits) {
  UpliCreditManager mgr;
  mgr.set_initial_credits(/*port=*/0, /*vc=*/0, /*credits=*/10);

  // Consume credits
  for (std::size_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(mgr.consume_credit(0, 0));
  }
  EXPECT_FALSE(mgr.consume_credit(0, 0));  // Exhausted

  // Return credits
  UpliCreditReturn credit_return;
  credit_return.credit_vld[0] = true;
  credit_return.port_credits[0].vc = 0;
  credit_return.port_credits[0].num = 3;  // Return 4 credits (num+1)
  mgr.return_credits(credit_return);

  EXPECT_EQ(mgr.available_credits(0, 0), 4);
}
```

### Integration Tests

```cpp
// Example: upli_integration_test.cpp
TEST(UpliIntegration, ReadRequestResponseFlow) {
  // Setup originator and completer
  UpliOriginatorConfig orig_config;
  orig_config.physical_acc_id = 1;
  UpliOriginator originator(orig_config);

  UpliCompleterConfig compl_config;
  compl_config.physical_acc_id = 2;
  compl_config.address_base = 0x1000;
  compl_config.address_size = 0x1000;
  UpliCompleter completer(compl_config);

  // Connect channels and credits
  UpliCreditManager credit_mgr;
  // ... setup ...

  // Issue read request
  std::vector<std::byte> received_data;
  originator.set_read_completion_callback(
      [&](std::uint16_t tag, RspStatus status, std::span<const std::byte> data) {
        EXPECT_EQ(status, RspStatus::kOkay);
        received_data.assign(data.begin(), data.end());
      });

  auto tag = originator.send_read(/*dst=*/2, /*addr=*/0x1000, /*len=*/8);
  ASSERT_TRUE(tag.has_value());

  // Completer processes and responds
  // ... validation ...

  EXPECT_EQ(received_data.size(), 64);
}
```

### Scenario Tests

```cpp
TEST(UpliScenarios, MultiPortTdmWithCreditStalls) {
  // Test TDM scheduling across 4 ports with varying credit availability
}

TEST(UpliScenarios, OrderingEnforcementSameRegion) {
  // Verify requests to same 256B region maintain order on same port
}

TEST(UpliScenarios, MultiRequestOutstandingTags) {
  // Issue multiple reads with different tags, verify independent completion
}
```

## Code Quality Checklist

Per coding standards:

- ✅ C++20 features (`std::span`, `std::optional`, `[[nodiscard]]`)
- ✅ All functions have `UALINK_TRACE_SCOPED(__func__)`
- ✅ Functions < 100 lines
- ✅ Methods in headers < 10 lines
- ✅ `snake_case` for functions/variables, `PascalCase` for types
- ✅ Mandatory braces on all if statements
- ✅ No ternary operators
- ✅ Context-aware loop indices
- ✅ RAII, no raw `new`/`delete`
- ✅ `noexcept` on getters
- ✅ Comprehensive unit tests for all modules

## Open Questions / Future Work

1. **Security/Authorization Tags**: Initial implementation stubs the 64-bit `req_auth_tag` / `rd_rsp_auth_tag` / `wr_rsp_auth_tag` fields. Full crypto validation deferred.

2. **UPLI Messages (0x2A)**: Key roll messages (types 0x01-0x03) will be stubbed initially. Full message protocol in future phase.

3. **Vendor-Defined Commands**: Reserved command encodings available for vendor extensions.

4. **Strict Ordering Mode**: Initial focus on relaxed (VC-based) ordering. Strict ordering (global) as enhancement.

5. **Physical Layer Integration**: Currently assumes ideal PHY. Bit-level error injection could be added.

6. **Performance Modeling**: Cycle-accurate timing model vs behavioral model trade-offs.

## Summary

This design provides a complete, modular implementation of the UPLI interface that:

- **Follows the UALink 2.0 specification exactly** - All signal fields, bit widths, and semantics match the spec
- **Integrates cleanly with existing DL/TL layers (Phases 1-3)** - Reuses established patterns and interfaces
- **Uses the bit_fields library consistently** - All packet formats defined with `PacketFormat`, encoded/decoded with `NetworkBitWriter`/`NetworkBitReader`
- **Adheres to coding standards** - C++20, Tracy instrumentation, snake_case, function size limits, etc.
- **Provides comprehensive test coverage** - Unit tests per component, integration tests, scenario tests
- **Supports incremental implementation via sub-phases** - Each sub-phase (4.1-4.7) can be developed and tested independently

### Key Design Patterns from Existing Code

The Phase 4 design carefully follows established patterns from Phases 1-3:

1. **Bit Field Encoding/Decoding**
   - Define `constexpr PacketFormat<N>` for each protocol structure
   - Use `NetworkBitWriter::serialize()` for encoding
   - Use `NetworkBitReader::deserialize_into()` for decoding
   - Validate field ranges before encoding
   - Convert bool fields to/from uint8_t for serialization

2. **Structure Naming**
   - Packet formats: `kUpliRequestFormat`, `kUpliRdRspFormat` (kCamelCase constants)
   - Field structures: `UpliRequestFields`, `UpliRdRspFields` (PascalCase types)
   - Encode functions: `encode_upli_request()`, `encode_upli_rd_rsp()` (snake_case)
   - Decode functions: `decode_upli_request()`, `decode_upli_rd_rsp()` (snake_case)

3. **Namespace Organization**
   - All UPLI components in `namespace ualink::upli`
   - Matches existing `ualink::dl` and `ualink::tl` namespaces

4. **Error Handling**
   - Range validation with `std::invalid_argument` exceptions
   - `std::optional<T>` for operations that can fail
   - Clear error messages with function name prefix

5. **Tracing**
   - All functions include `UALINK_TRACE_SCOPED(__func__)` as first line
   - Integrates with Tracy profiler when enabled

6. **Testing Strategy**
   - Round-trip encode/decode tests for all formats
   - Boundary condition tests for field ranges
   - Integration tests for component interactions
   - Scenario tests for end-to-end flows

The modular design allows each component to be developed, tested, and validated independently before integration.
