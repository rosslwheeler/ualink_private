# UALink Behavioral Model Plan

## Goals
- Build a behavioral UALink stack model, starting at the Data Link (DL) and moving up.
- Use the `bit_fields` library for bit-level encoding/decoding.
- Follow `/home/ross/OSS/ai/nic/docs/coding-standards.md`.
- After the behavioral model is stable, build a driver to exercise the virtual chip.

## Outstanding Work Items

### UPLI Write Response Channel - Missing Fields (High Priority)

The Write Response Channel implementation in `include/ualink/upli_channel.h` is missing 4 required fields from Table 2-11 of the UALink 200 Rev 1.0 specification.

**Critical for Multi-Node Operation:**
- [ ] Add `wr_rsp_dst_phys_acc_id` (10 bits) - **HIGH PRIORITY**
  - Routes response back to originator (contains `ReqSrcPhysAccID` from original request)
  - Required for multi-accelerator systems with switches
  - Used for response routing through topology

- [ ] Add `wr_rsp_vc` (2 bits) - **HIGH PRIORITY**
  - Virtual channel of the Write Response
  - Must match `ReqVC` from original request
  - Required for credit return mechanism
  - Used by credit manager to track VC-specific credits

- [ ] Add `wr_rsp_pool` (1 bit) - **HIGH PRIORITY**
  - Indicates if Pool Credit (1) or VC Credit (0) was used to issue the request
  - Required for credit return mechanism
  - Tells receiver which credit pool to replenish

**Optional/Debug:**
- [ ] Add `wr_rsp_src_phys_acc_id` (10 bits) - **MEDIUM PRIORITY**
  - Contains `ReqDstPhysAccID` from original request
  - Debug-only field per spec (may be compressed out by TL/DL layers)
  - Should be zero if not accurate
  - May NOT be used for functional purposes

**Investigation Needed:**
- [ ] Clarify purpose of existing `wr_rsp_attr` (8 bits)
  - Not specified in Table 2-11
  - May be implementation extension or should be removed
  - Current total: 96 bits; spec requires minimum 105 bits

**Impact:**
- Current implementation (5/9 signals) works for single-node systems
- Missing fields break multi-node routing and credit-based flow control
- Credit manager cannot properly track VC vs Pool credits without `wr_rsp_vc` and `wr_rsp_pool`

**Files to Update:**
- `include/ualink/upli_channel.h` - Add fields to `UpliWrRspFields` struct and `kUpliWrRspFormat`
- `src/upli_channel.cpp` - Update serialization/deserialization functions
- `tests/upli_channel_test.cpp` - Add test coverage for new fields

---

## Phase 0: Project scaffolding
- Add `include/`, `src/`, and `tests/` structure.
- Define minimal build wiring (CMake/Make) when the first module compiles.
- Add a local trace shim that can bind to `nic/trace.h` if available.

## Phase 1: Data Link (DL) baseline (current)
- Implement a logical 640B DL flit model with:
  - Flit header serialize/deserialize (explicit + command formats).
  - Segment header serialize/deserialize.
  - Payload serializing/deserializing of full 64B TL flits.
- Assumptions/staging for this phase:
  - Ideal PHY (no bit errors).
  - No CRC/replay/Tx pacing yet.
  - No alternative sectors (DL messages/UART stubbed).
  - Only full TL flits are serialized (no cross-flit partials yet).
- Tests:
  - Segment header serializing/deserializing.
  - TL flit serializing into segments with expected header bits.

### Phase 1 concrete tasks
- Add tests for `serialize_explicit_flit_header`/`deserialize_explicit_flit_header` round-trip.
- Add tests for `serialize_command_flit_header`/`deserialize_command_flit_header` round-trip.
- Add tests for `serialize_segment_header`/`deserialize_segment_header` for all bit combinations.
- Add tests that serialize/deserialize multiple TL flits across segments, verifying segment headers.
- Add tests that serializing stops at `kDlPayloadBytes` capacity and reports `flits_serialized`.
- Add tests for invalid header fields throwing (range checks).

## Phase 2: Data Link robustness
- Add CRC generation/check.
- Add link-level replay and ack/replay command handling.
- Add Tx pacing and Rx rate adaptation hooks.
- Add error injection hooks for negative testing.

### DL Message Codes (Table 6-3)
- Message classes (`mclass`) and types (`mtype`) are defined in code in [include/ualink/dl_messages.h](include/ualink/dl_messages.h).
- `mclass`:
  - Basic Messages: `DlMessageClass::kBasic` (`0b0000`)
  - Control Messages: `DlMessageClass::kControl` (`0b1000`)
  - UART Messages: `DlMessageClass::kUart` (`0b0001`)
- `mtype` (Basic / `DlBasicMessageType`):
  - No-Op: `kNoOp` (`0b000`)
  - TL Rate Notification: `kTlRateNotification` (`0b100`)
  - Device ID Request: `kDeviceIdRequest` (`0b101`)
  - Port Number Request/Response: `kPortNumberRequestResponse` (`0b110`)
- `mtype` (Control / `DlControlMessageType`):
  - DL Channel On/Offline negotiation: `kChannelOnlineOfflineNegotiation` (`0b100`)
- `mtype` (UART / `DlUartMessageType`):
  - UART Stream Transport Message: `kStreamTransportMessage` (`0b000`)
  - UART Stream Credit Update: `kStreamCreditUpdate` (`0b001`)
  - UART Stream Reset Request: `kStreamResetRequest` (`0b110`)
  - UART Stream Reset Response: `kStreamResetResponse` (`0b111`)
- Helper: `make_common(...)` constructs `DlMsgCommon` with the right `mclass/mtype` pairing.

### DL Messages (6.4) Behavioral Requirements (not implemented yet)
- Reserved bits behavior:
  - All reserved fields in DL messages shall be set to `0x0` and ignored by the receiver.
  - Note: Section text says “bit 0 is reserved”, but the message tables define bit 0 as `Compressed` (and require it to be `0`). The implementation follows the tables and treats `compressed != 0` as invalid.
- DLAltSector usage:
  - Any Segment may contain a DL alternative sector (`DLAltSector`) used to carry DL messages.
  - DL messages originate/terminate at the DL (not TL).
  - Serializer/deserializer needs an explicit path to place/extract message DWords in segments and set the Segment Header `dl_alt_sector` bit.
- Message packing and blocking:
  - All DL messages are a single DWord, except UART Stream Transport (up to 33 DWords).
  - UART Stream Transport must be packed sequentially into segments and may span multiple flits.
  - While a UART Stream Transport is being transmitted, other DL messages are blocked.
- Message arbitration:
  - Round-robin arbitration within each group (Basic/Control/UART) to pick a group-winner.
  - Final round-robin across groups (Basic vs Control vs UART) to select what to send.
- Timing / response behavior (modeling target):
  - For basic requests that require a response (e.g. TL Rate Notification, Device ID, Port ID), model the “respond within 1us” requirement at a coarse level (simulation time / tick budget).
  - For Control Messages, model the request/ack/nack/pending resolution flows and enforce “no new request of same `mclass/mtype` until completion”.

### Bitfield Coverage Audit (Spec Tables 5-29..5-38, 6-2..6-15, 9-3)

This section is a quick audit against the bitfields list captured in January 2026.

- Implemented + unit-tested packet formats:
  - DL message DWord formats (Tables 6-4..6-13) are implemented in [include/ualink/dl_messages.h](include/ualink/dl_messages.h) and exercised by [tests/dl_messages_test.cpp](tests/dl_messages_test.cpp).
  - DL flit header formats (Tables 6-14/6-15) and Segment Header (Table 6-2) are implemented in [include/ualink/dl_flit.h](include/ualink/dl_flit.h) and exercised by [tests/dl_flit_test.cpp](tests/dl_flit_test.cpp).
  - TL field formats (Tables 5-29/5-30/5-31/5-34/5-36/5-38) are implemented in [include/ualink/tl_fields.h](include/ualink/tl_fields.h) and exercised by [tests/tl_fields_test.cpp](tests/tl_fields_test.cpp).
  - Security IV format (Table 9-3) is implemented in [include/ualink/security_iv.h](include/ualink/security_iv.h) and exercised by [tests/security_iv_test.cpp](tests/security_iv_test.cpp).

- Implemented in format/codec but not yet enforced semantically (changes needed):
  - [ ] UART `stream_id`: spec text says only `000` (stream 0) is supported; current codec accepts any 3-bit value.
    - Apply to Tables 6-10/6-11/6-12/6-13 (reset req/rsp, stream transport, credit update).
  - [ ] Channel negotiation `channel_command`: spec enumerates a small set of legal values (Request/Ack/NAck/Pending); current codec accepts any 4-bit value.
    - Apply to Table 6-8.
  - [ ] Port ID Request/Response: spec prose describes a “10-bit port number” but the bit range is 27:16 (12 bits). Decide whether to:
    - enforce 10-bit in the model (treat upper 2 bits as reserved/0), or
    - accept full 12-bit value (follow the table bit range).

- Implemented in format but not yet used in the DL flit packing path (changes needed):
  - [ ] Segment Header `dl_alt_sector` (Table 6-2) is encoded/decoded, but DLAltSector message insertion/extraction is not implemented.
    - Add an explicit path in DL serializer/deserializer to place/extract DL message DWords and set/clear `dl_alt_sector` accordingly.

- “Implemented and used” vs “implemented and available” clarification:
  - DL flit packing currently transports opaque 64B TL flits; TL field encodings (Tables 5-29..5-38) are available as helpers/tests but are not required by the DL serializer itself until the model starts generating/parsing TL traffic at field level.

### Phase 2 concrete tasks
- Define CRC polynomial/width in a header and add serialize/deserialize helpers.
- Add CRC to `DlSerializer::serialize` and validate in `DlDeserializer::deserialize` with error reporting.
- Add command flit handling for ack/replay sequence tracking and retry window.
- Add pacing hooks (interfaces or callbacks) in serialize/deserialize to model rate limits.
- Add error injection toggles for CRC failure and replay-triggered drops.

#### Figure 6-21 Tx Flow Chart (extra)
- 6.6.6 General Rules (Tx-side details):
  - Flit sequence numbers: valid values are 1 to 511 (0 is reserved). 511 wraps to 1.
    - Any `(sequence number expression) % 511` implicitly wraps 511 to 1.
  - NOP flits do not consume a flit sequence number.
  - A NOP flit uses `Tx_last_seq` for its sequence number.
  - A payload flit uses `Tx_last_seq + 1` for its sequence number when it is added to the `TxReplay` buffer.
- Tx Source Flit Rules:
  - If `Tx_replay == 1`: flit from `TxReplay`.
  - Else: flit from DL stream.
  - If `Flit == payload`: flit added to `TxReplay`.
  - If all `TxReplay` flits sent: set `TxReplay` to `0`.
- Tx Scheduling:
  - Update `Tx_explicit_count -= 1`.
  - If `Tx_first_replay == 1`:
    - Clear `Tx_first_replay` to `0`.
    - Set `Tx_explicit_count` to `0x1F`.
    - Set `op` to Replay (`0b001`).
  - If `Tx_explicit_count <= 0`: set `Tx_explicit_count` to `0x1F`.
  - If `Tx_replay_req_count > 0` and next CW group:
    - Set `op` to Ack (`0b010`).
    - Set `ackReqSeq` to `Rx_last_seq_calc`.
  - If `Tx_replay_req_count == 3`: set `Tx_replay_req_seq_no` to `Rx_last_seq_calc + 1`.
  - Update `Tx_replay_req_count += 1`.
  - Set `Tx_replay_req_count = 1`.
  - Set `op` to Replay Request (`0b011`).
  - Set `ackReqSeq` to `Tx_replay_req_seq_no`.

#### Figure 6-20 Rx Flow Chart (extra)
- Rx Ingress Rules:
  - `Rx_replay_ignore_count` is decremented by 1 (saturates at `0x0`).
  - If the CRC check passes: proceed with both:
    - Rx Ack and Replay Request Processing Rules, and
    - Rx Enqueuing Rules.
  - Else (CRC fails):
    - `Rx_bad_crc_count += 1`.
    - If `Rx_bad_crc_count >= 7` then set `Rx_ambiguous` to `1`.
    - If `Rx_replay == 1` then `Rx_unexpected_count += 1`.
    - Discard flit.
    - Increment CRC error counter.
- Rx Ack and Replay Request Processing Rules:
  - A command flit with `ackReqSeq == 0` is dropped and error is logged.
  - If both of the following are true:
    - The DL flit is a Replay Request flit.
    - `Rx_replay_ignore_count == 0x0`.
    - Then:
      - If both are true:
        - `(ackReqSeq - Rx_last_ack - 1) % 511 <= 256`.
        - `(Tx_last_seq - ackReqSeq) % 511 <= 256`.
        - Then:
          - Set `TxReplay` to `1`.
          - Set `Tx_first_replay` to `1`.
          - Set `Rx_replay_ignore_count` to `12`.
          - Schedule replay with the `ackReqSeq` from the received flit as the next flit to transmit.
        - Else:
          - Ignore the DL Replay Request command in the ingress DL flit.
          - Optionally log unexpected Replay Request.
  - Else if the DL flit contains an Ack command, then:
    - If both are true:
      - `(ackReqSeq - Rx_last_ack) % 511 <= 256`.
      - `(Tx_last_seq - ackReqSeq) % 511 <= 256`.
      - Then:
        - `Rx_last_ack = ackReqSeq`.
        - Remove all DL payload flits with sequence number lower than or equal to `Rx_last_ack` from the `TxReplay` buffer.
      - Else:
        - Ignore the DL Ack command in the ingress DL flit.
        - Optionally log unexpected Ack.
- Rx Enqueuing Rules:
  - If `op == "explicit"` and `flitSeqNo == 0`: drop flit, log error.
  - If `op == "explicit"` or (`Rx_ambiguous == 0` and `Rx_replay == 0`):
    - If NOP flit or payload flit with expected sequence (per Figure 6-20):
      - If payload flit: add flit to receive queue.
      - Clear `Rx_unexpected_count` to `0`.
      - Set `Rx_replay` to `0`.
      - Update `Rx_last_seq_calc` from `Rx_seq_calc`.
      - Clear `Rx_ambiguous` to `0`.
      - Clear `Rx_bad_crc_count` to `0`.
      - Clear `Tx_replay_req_count` to `0`.
    - Else:
      - Set `Tx_replay_req_count` to `3`.
      - Set `Rx_replay` to `1`.
      - Clear `Rx_unexpected_count` to `0`.
  - Else if `Rx_replay == 1`:
    - Update `Rx_unexpected_count += 1`.
    - If `Rx_unexpected_count >= Rx_replay_limit`:
      - Set `Tx_replay_req_count` to `3`.
      - Clear `Rx_bad_crc_count` to `0`.
    - Drop flit (optionally increment error count).

## Phase 3: Transaction Layer (TL) baseline
- Implement TL flit and half-flit serializing.
- Add basic request/response flows and flow control integration with DL.

## Appendix: Transaction Layer (TL) Flits, Half-Flits, and Sequencing (Spec Ch. 5)

### TL ↔ UPLI role summary (Figure 5-1)
- TL converts inbound UPLI beats (from two attached UPLI interfaces) into outbound Tx TL flits.
- TL converts inbound Rx TL flits into outbound UPLI beats (to those two UPLI interfaces).
- Tx/Rx TL flit formats are symmetric.

### TL flit structure (Table 5-1)
- TL flit is 64 bytes.
- Split into two 32-byte half-flits:
  - Lower 32B half-flit (sectors 0..7)
  - Upper 32B half-flit (sectors 8..15)
- 16 sectors per flit, each 4 bytes, numbered from least-significant sector 0 to sector 15.

### Half-flit kinds
- A TL half-flit may be:
  - Control half-flit
  - Data half-flit
  - Message half-flit
  - Authentication Tags (AuthTags) half-flit

### Control half-flit: contents + field footprints (Tables 5-2 and related text)
- Control half-flit encodes:
  - Requests (Read/Write/AtomicR/AtomicNR)
  - Read Responses (control only, not data)
  - Write Responses
  - Flow Control (FC) / NOP information
- Control field type encoding (5.9 / Table 5-27):
  - Each field begins with `FTYPE` in the high-order 4 bits of the Control Field.
  - `FTYPE` implicitly determines the field size (1 sector / 2 sectors / 4 sectors).
  - Legal `FTYPE` encodings:
    - `0x0`: Flow Control Pool/NOP indication (1 sector)
    - `0x1`: Uncompressed Request (4 sectors)
    - `0x2`: Uncompressed Response (2 sectors)
    - `0x3`: Compressed Request (2 sectors)
    - `0x4`: Compressed Response for Single-Beat Read Response (1 sector)
    - `0x5`: Compressed Response for Write or Multi-Beat Read Response (1 sector)
- Sector footprints:
  - 4-sector Request: sectors 7-6-5-4 or 3-2-1-0
  - 2-sector Compressed Request: 7-6 or 5-4 or 3-2 or 1-0
  - 2-sector Response: 7-6 or 5-4 or 3-2 or 1-0
  - 1-sector Compressed Response: any single sector 7..0
  - 1-sector Flow Control / NOP: any single sector 7..0
- Mixing rule:
  - Field types may be intermingled subject to footprints.
  - Any unused sector is a NOP field.
- Flow Control field semantics (Table 5-38 defines bit layout; TL text adds aggregation rule):
  - FC contains four signals: `ReqCmd`, `RspCmd`, `ReqData`, `RspData`.
  - Each signal independently indicates (a) pool vs VC and (b) credit count.
  - A Control half-flit may contain multiple FC sectors, but for each of the four signals, only one FC sector may carry a non-zero credit value for any given pool/VC.
    - Intended behavior: credit-update logic may bitwise-OR (not add) FC sectors to form the update for each signal.

### TL flow control and credits (5.8)
- Credits govern:
  - Control-half fields: Request credits and Response credits.
  - Data-half-flits: credits for pairs of Data half-flits (i.e., per 64B of data).
- Credit classes (Table 5-26):
  - Request CMD: credits for Uncompressed/Compressed Request fields in Control half-flits.
  - Response CMD: credits for Uncompressed/Compressed Read Response fields (control only, not data) and Write Response fields in Control half-flits.
  - Request Data: credits for 64B data buffers for Write/WriteFull data, atomic operand data, vendor-defined-with-data, and UPLI Write Message requests.
    - Byte Enables (even though delivered in a Data half-flit) do not consume separate Data credits; they are assumed to be held in dedicated side buffers.
  - Response Data: credits for 64B data buffers for Read Response data (no associated byte-enables).
- Data-credit granularity:
  - One Data credit reserves one 64B data buffer.
  - Therefore, a Data credit corresponds to a pair of 32B TL Data half-flits.
- Pool vs VC credits:
  - Pool credit may be used for any Virtual Channel (subject to TL-controlled allocation).
  - VC credit is tied to a specific virtual channel and may only be used for that VC.

#### Credit initialization and “initial release complete” (5.8)
- At initialization, both connected TLs are programmed (implementation-specific) with the number and types of credits (Pool and/or VC) their Rx side can buffer for each class.
- After reset/initialization:
  - TLs start with no credits available to issue any Control fields or Data half-flits.
  - Each TL transmits one or more Flow Control fields on its Tx TL flit channel to advertise its available Rx buffering (for each class: ReqCmd, RspCmd, ReqData, RspData).
  - The receiving TL records credit counts, class, credit type (pool vs VC), and VC if applicable.
  - When the initial credit issuance is complete, the TL sends the `0x01` “Initial Credit Release Complete” TL Message half-flit.

#### Returning credits
- Requests/Responses carry `POOL` (and `VCHAN`) in the Control field:
  - `POOL==1` indicates the sender consumed a Pool credit.
  - `POOL==0` indicates the sender consumed a VC credit for `VCHAN`.
- The receiving TL must record the observed `POOL`/`VCHAN` used for each Request/Response and return credits using the same pool/vc + VC association.
- Data half-flits do not explicitly carry pool/vc or VC.
  - Data credits “inherit” the pool/vc + VC association from their corresponding Request (for request data / atomic operands) or from their corresponding Read Response (for response data).

#### Shared Data Buffer mode (optional)
- TL may support “Shared Data Buffer mode” where Pool credits for Request Data and Response Data buffers may be shared across those two data classes.
  - In this mode, initial Data credits may be issued entirely in one class (ReqData or RspData) or split across both.
  - If shared mode is enabled, the transmitter must tolerate being issued zero credits for one of the data classes.
  - If shared mode is not enabled, the TL issuing credits must issue at least some credits for both data classes.
- Shared-mode capability is indicated in the `0x01` Initial Credit Release Complete TL message:
  - low-order bit of the 31-byte message payload is `1` if shared mode supported, else `0`.

#### NOP encoding
- A Flow Control field returning zero pool credits for all four classes has value `0x0000_0000` and is the NOP field.

### Control field formats: key signals + restrictions (5.9.1–5.9.6)

#### Uncompressed Request field (`FTYPE=0x1`, 4 sectors; Table 5-29)
- Key signals carried:
  - `CMD` (UPLI `ReqCmd`)
  - `VCHAN` (UPLI `ReqVC`)
  - `ASI` (UPLI `ReqASI`)
  - `TAG` (UPLI `ReqTag`)
  - `POOL` (TL credit domain select: Pool vs VC; independent of UPLI credit controls)
  - `ATTR` (UPLI `ReqAttr`)
  - `LEN` (UPLI `ReqLen`)
  - `METADATA` (UPLI `ReqMetadata`)
  - `ADDR` (UPLI `ReqAddr[56:2]`, doubleword aligned)
  - `SRCACCID` / `DSTACCID` (UPLI physical accelerator IDs)
  - `CLOAD` / `CWAY` (Rx Address Cache load controls)
  - `NUMBEATS` (number of data beats transferred for requests with data: writes, atomics operands, vendor-defined with data)

#### Uncompressed Response field (`FTYPE=0x2`, 2 sectors; Table 5-30)
- Key signals carried:
  - `VCHAN` (UPLI `RdRspVC` or `WrRspVC`)
  - `TAG` (UPLI `RdRspTag` or `WrRspTag`)
  - `POOL` (TL credit domain select: Pool vs VC; independent of UPLI credit controls)
  - `LEN` (UPLI `RdRspNumBeats`, only meaningful for multi-beat reads; else `00`)
  - `OFFSET` (UPLI `RdRspOffset`, only meaningful for single-beat reads; else `00`)
  - `STATUS` (UPLI `RdRspStatus` or `WrRspStatus`)
  - `RD/WR` (read vs write response; includes AtomicR vs AtomicNR mapping)
  - `LAST` (UPLI `RdRspLast`, valid only for reads)
  - `SRCACCID` / `DSTACCID` (primarily routing/debug; may be zeroed if not carried)

#### Compressed Request field (`FTYPE=0x3`, 2 sectors; Tables 5-31..5-33)
- Key signals carried (subset / re-encoded):
  - `CMD` (compressed encoding of UPLI `ReqCmd`)
  - `VCHAN`, `ASI`, `TAG`, `POOL`
  - `LEN` (2b encoding: 64/128/192/256 bytes)
  - `METADATA` (UPLI `ReqMetadata[2:0]` only; upper bits assumed 0)
  - `ADDR` (UPLI `ReqAddr[19:6]`, 64B-aligned block within cached 1MB region)
  - `SRCACCID` / `DSTACCID`
  - `CWAY` (selects cache way used to reconstitute `ReqAddr[56:20]`)
- Usage restrictions (Table 5-32):
  - Command must be compressible; atomics and vendor-defined commands are never compressed.
  - `ATTR` must be constrained:
    - Reads: `ReqAttr == 0xFF` (all bytes enabled)
    - Writes: `ReqAttr == 0x00`
  - Length must be 64/128/192/256 bytes.
  - Address must be 64B-aligned and must not cross a 256B boundary.
  - `ReqMetadata[7:2] == 0`.
  - Must hit in the Tx Address Cache; the matching entry must not be overwritten/invalidated before issuance.
- Compressed CMD encoding (Table 5-33):
  - `000` Read
  - `100` Write
  - `110` WriteFull
  - All other encodings reserved.

#### Compressed Response fields (`FTYPE=0x4` and `FTYPE=0x5`, 1 sector; Tables 5-34..5-37)
- `FTYPE=0x4`: single-beat read response compressed; carries `VCHAN`, `TAG`, `POOL`, `DSTACCID`, `OFFSET`, `LAST`.
- `FTYPE=0x5`: write response OR multi-beat read response compressed; carries `VCHAN`, `TAG`, `POOL`, `DSTACCID`, `LEN`, `RD/WR`.
- Usage restriction (Tables 5-35 and 5-37):
  - Only permitted if `STATUS == 0b0000` (“OKAY / Normal Completion”).
  - TL is not required to use compressed responses even when permitted.

### Data half-flit: contents + packing rules
- Data half-flit conveys:
  - Read response data (Read + AtomicR)
  - Write data (Write/WriteFull/UPLI Write Message + AtomicNR)
  - Byte enables (Write + AtomicR/AtomicNR)
  - Atomic operand data (AtomicR/AtomicNR)
- Data beat mapping:
  - Each 64B UPLI beat corresponds to two consecutive 32B TL Data half-flits, preserving order.
- Writes and vendor-defined commands with OrigData beats:
  - Append one additional 32B Data half-flit containing Byte Enables to the end of the request’s data half-flits.
  - WriteFull: no ByteEnables half-flit is appended.
  - Byte Enables overhead is fixed at 32B regardless of write size (no optimization).
- Atomics:
  - UPLI atomic operands + byte enables arrive as a single 64B OrigData beat.
  - Encode as three consecutive 32B data half-flits:
    - two for operand data, then one for atomic byte enables.
  - The byte-enable half-flit alignment is relative to the 256B memory block being modified.
  - AtomicR returned read data encodes as two ascending 32B data half-flits.

### Half-flit sequencing (5.1.1)
- Half-flit “type” is not explicitly indicated (except Message indicator bits); it is inferred from sequencing.
- When Authentication is disabled:
  - Lower half-flit is interpreted as Control.
  - If Control implies subsequent Data half-flits ("Data Request Fields"), subsequent half-flits are Data until satisfied.
  - Data half-flits follow the order of Data Request Fields in Control, from lowest-numbered sector field upward.
  - Swap rule:
    - If the final Data half-flit implied by a Control would fall into the lower half of a TL flit, it is swapped into the upper half and the lower half becomes the next Control.
  - Placement rule:
    - Each TL flit may contain only one non-NOP Control half-flit, and it must be in the lower half.
    - If a non-NOP Control implies no Data, the upper half must be a NOP Control half-flit (“MANDATORY NOP half-flit”).
- When Authentication is enabled:
  - A Control containing Requests/Responses must be followed immediately by an AuthTags half-flit.
  - The Control and its AuthTags half-flit must occur within the same TL flit.
  - AuthTags half-flit carries up to four 8B tags (one per Request/Response); unused tags are 0.
  - Because of the 4-tag limit, a non-NOP Control half-flit is limited to at most 4 total Requests+Responses.
  - Swap interaction:
    - If swap occurs (final Data half-flit occupies upper half), then the lower Control in that flit may only contain FC/NOP (no Requests/Responses), because there is no place for AuthTags.
  - Message half-flits must not replace AuthTags half-flits.

### TL message half-flits (5.1.2)
- Each TL flit has per-half Message Indicator bits: one for lower half (M0) and one for upper half (M1).
- If Message Indicator bit is 1, the half-flit is interpreted as:
  - low-order byte of the 32B half-flit = Msg Type (0..255)
  - remaining 31 bytes = message-specific payload
- Legal TL Message half-flits (5.9 / Table 5-28):
  - `0x00`: NOP TL Message half-flit
  - `0x01`: Initial Credit Release Complete TL Message half-flit
  - `0x20`: Poisoned Data TL Message half-flit
- Sequencing rule:
  - TL message half-flits generally “delay” the normal half-flit sequencing, except:
    - A message in the lower half that would have been a Control half-flit cannot cause that Control to move into the upper half.
    - “Data Poisoned” message half-flits replace corrupted Data half-flits (see 5.3).

### Indicating data corruption (5.3)
- If a UPLI data beat is marked corrupted (`RdRspDataError` or `OrigDataError`), the corresponding TL Data half-flits are replaced with a TL message half-flit of Msg Type `0x20` (“Poisoned Data”).
  - Because a UPLI beat is 64B and TL data half-flits are 32B, corrupted beats replace two TL data half-flits.
- Msg Type `0x20` is only legal in half-flits that would otherwise carry Data or Atomic Operands.
  - It is not used for byte-enables half-flits (byte enables are treated as control information; control-signal errors drive UPLI drop-mode behavior instead).
- Errors on the TL Rx interface are fatal/uncorrectable and halt forwarding on that Rx TL interface; errors on TL Tx are handled by DL.

### Changes needed to model this TL behavior (implementation checklist)
- [ ] Add explicit modeling of TL half-flit kind inference and sequencing rules (including swap + mandatory NOP behavior).
- [ ] Add support for Control half-flit packing across sectors (4-sector and 2-sector requests; 2-sector and 1-sector responses; FC sectors; NOP sectors).
- [ ] Enforce Control Field `FTYPE` legality (0x0..0x5) and correct field-size interpretation from `FTYPE`.
- [ ] Add compressed request/response selection rules and restrictions:
  - compressed request: address cache hit + alignment/size/metadata/attr + compressible-CMD constraints.
  - compressed response: `STATUS==OKAY` constraint.
- [ ] Enforce the Flow Control “OR-not-add” rule when multiple FC sectors exist in a Control half-flit.
- [ ] Implement TL credit accounting per 5.8:
  - four credit classes (ReqCmd, RspCmd, ReqData, RspData) with pool vs VC variants.
  - data credit granularity = 64B buffer = pair of 32B data half-flits.
  - byte-enables half-flit does not consume data credits (assumed side-buffered).
- [ ] Implement initial credit release protocol:
  - start with zero available credits; process inbound FC fields to build initial credit state.
  - emit `0x01` Initial Credit Release Complete message when initial issuance is complete.
  - parse the shared-data-buffer capability flag from `0x01` payload.
- [ ] Implement credit return formation rules:
  - requests/responses: return credits based on observed `POOL`/`VCHAN` of the consumed credit.
  - data: inherit pool/vc + VC association from corresponding request or corresponding read response.
- [ ] (Optional) Implement Shared Data Buffer mode for pool data credits (ReqData/RspData sharing) and allow zero initial credits for one data class when enabled.
- [ ] Add Message Indicator bits per half-flit and Message half-flit decode/encode.
- [ ] Enforce legal TL Message half-flit types (`0x00`, `0x01`, `0x20`) and define behavior for `0x01` (Initial Credit Release Complete).
- [ ] Add Poisoned Data message handling (Msg Type `0x20`) and integrate with UPLI `*DataError` indications:
  - replace the two 32B data half-flits corresponding to a corrupted 64B beat.
  - ensure byte-enable half-flits are never replaced by Poisoned Data messages.
- [ ] Add AuthTags half-flit behavior for security modes:
  - enforce “Control + AuthTags in same flit” and “≤4 req/rsp” constraint.
  - ensure Message half-flits do not replace AuthTags.

## Phase 4: UPLI interface behavior
- Model UPLI reset, handshake, and credit flow.
- Add routing/VC rules and ordering constraints.

## Phase 5: Driver + tooling
- Build a driver that exercises the virtual UALink chip.
- Provide scenario tests and diagnostics.

## Appendix: Address Translation Model (Spec 1.4, illustrative)
- The spec’s cross-domain address translation model is explicitly “for illustration”; address translation is an implementation choice (switches route by identifier).
- Addressing concepts:
  - System Physical Address (SPA): local system-domain physical address.
  - Network Physical Address (NPA): address used to access memory across system domains.
  - Optional “global flat addressing model” may be used by an implementation to simplify translation.
- Translation model (illustrative):
  - Source Accelerator MMU translates Guest Virtual Address (GVA) to an NPA.
  - Destination node uses a “link MMU” to translate NPA to local SPA.

### Remote Memory Access (RMA) (Spec 1.4.1, illustrative)
- Distributed apps may import remote memory via a PGAS library (e.g. OpenSHMEM-style), exchanging pointers out-of-band (front-side Ethernet).
- Exchanged pointer is expected to be an address handle + a physical Accelerator identifier (within a Pod), not a raw address.
  - The address handle improves security compared to exchanging actual addresses.
- Illustrative page-table behavior:
  - Importing/source Accelerator MMU creates a PTE containing (address handle, destination Accelerator ID).
  - Exporting/destination Accelerator link MMU creates a PTE containing (address handle, source Accelerator ID).

### Modeling Note (future)
- Defer any concrete address-translation behavior until TL/UPLI message flows are stable; when added, keep it pluggable (flat addressing vs handle-based translation) since the spec leaves translation as an implementation choice.

## Appendix: Coherency (Spec 1.5)
- No snoop transactions:
  - UALink does not carry snoop transactions for hardware coherency among Accelerators.
  - Host↔Accelerator hardware coherence (within a system node) is expected to be handled by host-side connections and is not specified by UALink.
- Software coherence expectation:
  - Accelerators caching peer memory (within or across system nodes) are expected to maintain coherence through software (e.g., cache flush/clear at appropriate kernel boundaries).
- I/O coherency semantics UALink shall support:
  - Reads from peer memory observe the most recent coherent copy from memory or a cache within the requester’s system node.
  - Writes to peer memory invalidate all cache copies within the requester’s system node.
    - Partial writes fetch any cached data and merge with write data; the most recent copy is written back to memory.
- Modeling note (future):
  - Treat snoop-less behavior as a baseline assumption; if/when modeling caches, represent the above semantics as local-node cache interactions rather than UALink message traffic.

## Appendix: UALink Stack Components + Routing (Spec Ch. 2)
- Stack component separation (Figure 2-4):
  - A UALink Stack Component contains an independent UPLI Originator and an independent UPLI Completer.
  - The UPLI Originator and UPLI Completer within a given stack shall not communicate directly; they are connected only via the UALink TL.
- TL ↔ UPLI channel mapping (high level):
  - UPLI Originator drives: `Req` and `OrigData`.
  - UPLI Completer drives: `RdRsp/Data` and `WrRsp`.
  - TL packages UPLI protocol channels into TL Half-Flits (Control + Data beats), combines two Half-Flits into a TL Flit, and hands TL Flits to DL.
  - DL aggregates TL Flits into a DL Flit by adding DL headers + CRC (and ultimately passes to PL/PHY in real HW).
  - The reverse path unpacks DL→TL→UPLI channels.
- Connected stack components (Figure 2-5):
  - Two stack components (Accelerator ↔ Switch) form a bidirectional, symmetric interface.
  - Accelerator’s UPLI Originator communicates with Switch’s UPLI Completer; Switch’s UPLI Originator communicates with Accelerator’s UPLI Completer.

### Request/Response paths (Figures 2-6 / 2-7)
- Requests originate at an Accelerator’s UPLI Originator and terminate at the destination Accelerator’s UPLI Completer.
- Responses originate at the destination Accelerator’s UPLI Completer and terminate at the source Accelerator’s UPLI Originator.
- A Switch participates as an intermediate termination/re-origination point (UPLI Completer on ingress side → Switch core routing → UPLI Originator on egress side).

### Routing signals + PortID semantics (Spec 2.4)
- Routing IDs carried end-to-end:
  - Request Channel includes `ReqSrcPhysAccID` (10b) and `ReqDstPhysAccID` (10b) which remain unchanged end-to-end.
  - Read/Write response channels similarly carry `*SrcPhysAccID` / `*DstPhysAccID`.
- PortID is link-local (not propagated over the wire):
  - `ReqPortID` (2b) selects which local port (bifurcated link) / TDM slice to use at a given UPLI.
  - `ReqPortID` is not propagated over the UALink link; it is dropped on egress and regenerated on ingress based on the physical port where traffic entered.
  - Switch assigns a new `ReqPortID` value based on the entry port and uses it locally for its internal UPLI/TDM.
- Switch routing model (illustrative):
  - Switch core performs a lookup keyed by `*DstPhysAccID` in a destination route table to select target Station/Port.
  - How the Switch routes internally is implementation-defined; model should treat it as a pluggable policy.

## Appendix: UPLI Flow Control (Credit-Based, Spec Ch. 2)
- Credit scope:
  - Credit-based flow control is per Port, per UPLI Channel (`Req`, `OrigData`, `RdRsp/Data`, `WrRsp`).
  - Credit returns are independent of UPLI time-division multiplexing (credits for any port may be returned in any cycle).
- Initialization/reset behavior:
  - After reset, the Sender side of a UPLI Channel starts with zero credits.
  - The Receiver side issues an initial set of credits based on its available buffering.
  - Receiver may issue: only Pool credits, only VC credits, or a combination.
- Credit return signaling (receiver → sender):
  - `*CreditVld[3:0]` indicates a credit return and which port(s) the return applies to (bit i corresponds to Port i).
  - `*CreditPool[3:0]` indicates credit type per port when the corresponding `*CreditVld` bit is asserted:
    - `0` = VC Credit
    - `1` = Pool Credit
  - `*CreditPort{0..3}VC[1:0]` indicates the Virtual Channel for the returned credit when `*CreditVld` is asserted for that port.
  - `*CreditPort{0..3}Num[1:0]` indicates the number of credits returned for that port as `Num + 1` when `*CreditVld` is asserted.
- Credit initialization completion:
  - Receiver asserts `*CreditInitDone[3:0]` per-port once it is done releasing initial credits.
  - Once asserted, `*CreditInitDone` remains asserted until reset/power-off.
  - Sender monitors `*CreditInitDone` until it has been asserted for an implementation-specific burst length (> 1 cycle), then stops monitoring it.
- Sender issuance rules:
  - Sender may only issue beats for a given port+channel after that port’s initial credit release is complete.
  - For writes, Sender must have sufficient credits in both `Req` and `OrigData` before issuing the Request (to ensure contiguous data beats starting with the Request beat).
  - When issuing a beat, Sender sets `*VC[1:0]` (beat’s VC) and `*Pool` to indicate whether a VC credit (`0`) or Pool credit (`1`) was consumed.
  - If Pool credits exist, Sender chooses how to allocate them across VCs over time.
- Credit accounting on return:
  - Receiver records the incoming beat’s `*VC` and `*Pool` and plays those back when returning the credit.
  - Sender returns the credit to the appropriate VC or Pool credit count accordingly.

## Appendix: UPLI Request Commands + Metadata (Spec 2.7.4.6 / 2.7.4.7)

### ReqCmd (Table 2-6)
- `ReqCmd[5] == 1` indicates the Request transfers data on the Originator Data channel.
  - For these commands, Sender must hold both:
    - a `Req` Channel credit (to issue the Request), and
    - one or more `OrigData` Channel credits (to issue the data beats).
- Command class encoding:
  - `ReqCmd[5:4] == 00`: Read-class (no data or byte masks with the Request).
  - `ReqCmd[5:4] == 10`: Write-class (data + possibly byte masks on `OrigData`).
  - `ReqCmd[5:4] == 11`: Atomic-class (operand data + byte masks on `OrigData`).
- Key standard commands (non-vendor-defined):
  - Read (`0x03`): I/O coherent read, up to 256B (up to 4 beats). Response on `RdRsp/Data`.
  - Write (`0x28`): I/O coherent write, up to 256B (up to 4 `OrigData` beats). Response on `WrRsp`.
  - WriteFull (`0x29`): I/O coherent store of 64/128/192/256B with all byte enables asserted. Response on `WrRsp`.
  - UPLI Write Message (`0x2A`): request + 1..4 `OrigData` beats; response channel depends on `ReqMetaData` (Read or Write response channel).
  - AtomicR (`0x30`): atomic read-modify-write with returned read data. Uses `OrigData` operands; response on `RdRsp/Data`.
  - AtomicNR (`0x32`): atomic read-modify-write with no data returned. Uses `OrigData` operands; response on `WrRsp`.
- Vendor Defined Commands (VDC) ranges:
  - Read-class VDC: `0x08`–`0x0F` (response on `RdRsp/Data`).
  - Write-class VDC: `0x2C`–`0x2F` (response on `WrRsp`; `OrigData` beats controlled by `ReqNumBeats`).
  - Atomic-class VDC: `0x3C`–`0x3F` (may respond on Read or Write response channel; beat counts are vendor-defined / controlled by `RdRspNumBeats` for returned read data).

### ReqMetaData (Table 2-7)
- For Requests that are not UPLI Write Message, `ReqMetaData` conveys implementation-defined control information.
- For UPLI Write Message (`ReqCmd == 0x2A`): `ReqMetaData` selects the message type (up to 256 types) and determines response channel/beat behavior.
  - `0x00`: NOP (1 beat of all-zeros data), response on `WrRsp`.
  - `0x01`: KeyRollMsg.ReqChannel (1 beat all-zeros), response on `WrRsp`.
  - `0x02`: KeyRollMsg.RdRspChannel (1 beat all-zeros), response on `RdRsp/Data`.
  - `0x03`: KeyRollMsg.WrRspChannel (1 beat all-zeros), response on `WrRsp`.
  - `0xF0`–`0xFF`: Vendor Defined UPLI Write Message types.
- Compatibility guideline:
  - Prefer allocating `ReqMetaData` values from low-order bits first, leaving high-order bits unused where possible.

## Appendix: UPLI Originator Data Channel (Spec 2.7.7 / Table 2-14)

### Signal coverage vs current model
- Current code coverage:
  - [include/ualink/upli_channel.h](include/ualink/upli_channel.h): `UpliOrigDataFields` currently models:
    - `OrigDataVld` (as `orig_data_vld`)
    - `OrigDataPortID` (as `orig_data_port_id`)
    - `OrigData` (512b payload as 64B `data`)
    - `OrigDataError` (as `orig_data_error`)
  - [src/upli_channel.cpp](src/upli_channel.cpp) + [tests/upli_channel_test.cpp](tests/upli_channel_test.cpp) serialize/deserialize and round-trip test the above.

- Missing fields (changes needed):
  - [ ] Add `OrigDataByteEn` (64b, 1 bit per byte).
  - [ ] Add `OrigDataOffset` (2b) and `OrigDataLast` (1b) for multi-beat transfers.
  - [ ] Add `OrigDataVC` (2b) and `OrigDataPool` (1b).
    - Note: spec says `OrigDataVC` is informational to the Switch in UALink 1.0 except that it is returned in credit-return signals; model still needs to carry it end-to-end to support correct credit return behavior.
  - [ ] Add Originator Data Channel credit-return signals with per-port VC/Num semantics:
    - `OrigDataCreditInitDone[3:0]`, `OrigDataCreditVld[3:0]`, `OrigDataCreditPool[3:0]`,
    - `OrigDataCreditPort{0..3}VC[1:0]`, `OrigDataCreditPort{0..3}Num[1:0]`.
    - Current model uses a generic `UpliCreditReturn` used by [include/ualink/upli_credit.h](include/ualink/upli_credit.h); either split credits per-channel (Req/OrigData/RdRsp/WrRsp) or annotate `UpliCreditReturn` with channel identity.
  - [ ] Add parity signals (not currently modeled):
    - `OrigDataVldParity`, `OrigDataParity[7:0]`, `OrigDataByteEnParity`, `OrigDataFieldsParity`, `OrigDataCreditVldParity`, `OrigDataCreditParity`.
  - [ ] Implement `OrigDataError` behavior (Spec 2.7.7.1): set/propagate “poison” when parity error detected at any UPLI, or when a Switch detects a data error (data only, not control) per its soft error scheme.

### Behavioral rules tied to OrigData (Spec 2.7.8)
- [ ] Do not transfer any information on any channel (beats or credits) until the UPLI control handshake has completed.
- [ ] Do not issue a Request/RdRsp/WrRsp/OrigData beat without sufficient credits for that channel.
- [ ] Sender must be able to unconditionally receive returned credits for that channel.
- [ ] For all Write and Atomic Requests: first Originator Data beat is issued on the same cycle as the Request beat (`ReqVld` and `OrigDataVld` asserted together).
- [ ] For a given Write/WriteFull/Atomic Request: OrigData beats are contiguous, in ascending address order:
  - `OrigDataOffset` starts at 0 and increments each beat.
  - final beat asserts `OrigDataLast`.
- [ ] Consequence: Write and Atomic Requests are not pipelined on Req+OrigData (must complete all OrigData beats for one write/atomic before issuing the next write/atomic request).
- [ ] Reads may be pipelined in cycles where OrigData beats for a prior write/atomic are present.
- [ ] Responses (WrRsp and RdRsp/Data) must not be blocked from making progress in returning to the Originator.

## Appendix: UPLI Ordering + Strict Ordering Mode (Spec 2.7.9)
- Ordering scope:
  - Requests ordered independently per (Source Accelerator, Destination Accelerator) pair.
  - Responses ordered independently for Read responses vs Write responses, and independently per (Destination, Source) pair.
- Address-based ordering rule:
  - All Requests are ordered based on `ReqAddr` “as if it contained an address to memory” (including requests without an address).
  - Requests whose addresses fall within a given 256-byte aligned region shall egress the Accelerator on the same Port.
- Strict Ordering Mode definition:
  - “Strict Ordering Mode” means Authentication+Encryption enabled or Encryption alone enabled.
- Request ordering:
  - Not Strict Ordering Mode: Requests within each 256B aligned region for a given Virtual Channel remain ordered end-to-end (implementations may choose to keep all requests ordered).
  - Strict Ordering Mode: all Requests remain ordered end-to-end.
- Response ordering:
  - Not Strict Ordering Mode: Read responses may be freely reordered; Write responses may be freely reordered; no ordering between read vs write responses.
  - Strict Ordering Mode: Read responses remain ordered among themselves; Write responses remain ordered among themselves; no ordering between read vs write responses.

## Appendix: UPLI Single-Copy Atomicity (Spec 2.7.10)
- Define Single-Copy atomicity as “performed in its entirety with no visible fragmentation”.
- Non-Single-Copy-atomic operations may be decomposed into smaller disjoint Single-Copy atomic operations (decomposition is allowed, not required).
- Atomics that are not Single-Copy atomic may only be decomposed into operation sizes that are multiples of the atomic element size.
- UPLI/TL are required to deliver UPLI operations without decomposition; Single-Copy atomicity and any decomposition is an implementation-specific behavior of the Destination Accelerator.
- If two non-Single-Copy-atomic operations are ordered for any reason (e.g., due to 256B region ordering), all constituent decomposed operations of the first must complete before the Response for that first operation is returned.

## Appendix: Data + Atomic Operands Transfer (Spec 2.8)

### Reads
- Addressing:
  - Read data is a contiguous block of one or more aligned doublewords.
  - Initial byte address is `ReqAddr` (doubleword aligned, so `ReqAddr[1:0] == 2'b00`).
  - `ReqLen` is “# doublewords - 1” (0 → 1 DW, 63 → 64 DW). Max 256B; request may not cross a 256B boundary.
- Read Response/Data payload mapping:
  - Data returns on the Read Response/Data channel in a 64-byte `RdRspData` beat.
  - Byte lanes in a beat are numbered 0..63, low address to high address.
  - A byte at address $X$ is placed in byte lane $(X \bmod 64)$.
  - Transfers spanning 64B boundaries use multiple 64B beats; each beat is transferred exactly once.
- Beat labeling:
  - Beats are labeled by `RdRspOffset` in {0,1,2,3}; offset 0 contains the initial doubleword.
  - In multi-beat mode, beats are returned low→high address order; in single-beat mode, beats may come in any order (subject to switch forwarding rules).
- Byte enables via `ReqAttr[7:0]`:
  - `ReqAttr[3:0]` specifies byte enables for the first doubleword (bit 3 = high-order byte, bit 0 = low-order byte).
  - `ReqAttr[7:4]` specifies byte enables for the last doubleword when the transfer is 2+ doublewords.
  - For a 1-doubleword access, `ReqAttr[7:4]` is ignored.
  - For 3+ doublewords, intermediate doublewords are implicitly all-bytes-enabled.
  - Bytes masked by `ReqAttr` or outside the `ReqAddr/ReqLen` range are ignored by the Originator but still participate in parity computations.

### Writes
- Addressing and size rules match Reads:
  - `ReqAddr[1:0] == 2'b00`.
  - `ReqLen` is “# doublewords - 1”, max 256B, may not cross a 256B boundary.
- OrigData payload mapping:
  - Write data transfers on a 64-byte `OrigData` beat.
  - Byte lanes are numbered 0..63 low→high address.
  - A byte at address $X$ is placed in byte lane $(X \bmod 64)$.
- Multi-beat rules:
  - If the write spans 64B boundaries (but not 256B), it uses multiple 64B beats; each beat is transferred exactly once.
  - “Wrapping” data within a beat is not permitted.
  - First beat must occur on the same cycle as the Request (`OrigDataVld` asserted with `ReqVld`).
  - Subsequent beats (if any) occur in consecutive cycles in ascending address order.
  - Beats are labeled by `OrigDataOffset` in {0,1,2,3}; offset 0 contains the initial doubleword.
- `OrigDataByteEn` rules:
  - Allows sparse writes within each beat; byte enables may be non-contiguous and all-disabled is permitted (beats still transfer but no memory updates occur).
  - For Write (not WriteFull), byte enables outside the `ReqAddr/ReqLen` region must be 0.
  - For WriteFull, all byte enables must be 1; additionally:
    - transfer must start on a 64B boundary,
    - length must be a multiple of 64B,
    - must not cross a 256B boundary.
  - TL/PHY may compress byte enables for WriteFull internally, but byte enables must still be present on UPLI.

### Atomics
- Command behavior:
  - AtomicR returns read data on Read Response/Data; AtomicNR returns status on Write Response.
  - Atomic semantics are determined by `OpType` in `ReqAttr`; element size (4B vs 8B) by `OpSize` in `ReqAttr`.
- Single-operand atomics:
  - Operand data conveyed on `OrigData` in a single beat.
  - `ReqAddr` must be aligned to element size (4B or 8B).
  - `ReqLen` must be a multiple of the element size.
  - Request may not cross a 64B boundary.
  - Operands are placed in naturally aligned byte lanes; returned AtomicR data is placed in corresponding `RdRspData` byte lanes.
  - `OrigDataByteEn` is used to select which elements are updated; for each element, all bytes must be enabled or all disabled (no partial enables within an element). Sparse elements are permitted.
  - For AtomicR: returned data for elements with byte-enables not set must be ignored by the Originator; Completer should drive those bytes to all-0 or all-1 to prevent security leaks.
- Double-operand atomics:
  - `ReqLen` must specify a 64B transfer (`ReqLen == 15`), and `ReqAddr` must be 32B-aligned.
  - The atomic updates the 32B memory region at `ReqAddr`.
  - Operands are carried in one beat with fixed placement (independent of `ReqAddr`):
    - Op1 occupies byte lanes 0..31 of `OrigData`.
    - Op2 occupies byte lanes 32..63 of `OrigData`.
  - Operands are partitioned into aligned elements (4B or 8B). Byte enables apply per element and must be all-0 or all-1 within an element.
  - Op1 and Op2 enables must match per element (no partial enables; consequence: Op1 and Op2 byte-enables are identical per element).
- Unsupported atomics:
  - A Completer that does not support a given Atomic returns `SLVERR` on the appropriate response channel (Read Response/Data for AtomicR, Write Response for AtomicNR).

### Changes needed to model Spec 2.8 (implementation checklist)
- [ ] Extend Read Response/Data channel modeling with the fields required to represent multi-beat vs single-beat transfers:
  - `RdRspOffset`, `RdRspLast`, and (for multi-beat mode) a beat-count indicator (`RdRspNumBeats` per spec terminology).
- [ ] Extend Originator Data channel modeling (see Table 2-14 section above):
  - `OrigDataByteEn`, `OrigDataOffset`, `OrigDataLast`, plus `OrigDataVC`/`OrigDataPool` for credit attribution.
- [ ] Define/validate boundary rules for Requests:
  - enforce “may not cross a 256B boundary” for Reads/Writes.
  - enforce “single-operand atomics may not cross a 64B boundary”.
- [ ] Define `ReqAttr` subfield interpretation for Atomic operand sizing/type (OpType/OpSize) so operand placement checks can be implemented.
- [ ] Add a small reference mapping helper in the model for “byte lane = addr mod 64” so tests can validate beat/offset placement deterministically.
