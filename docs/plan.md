# UALink Behavioral Model Plan

## Goals
- Build a behavioral UALink stack model, starting at the Data Link (DL) and moving up.
- Use the `bit_fields` library for bit-level encoding/decoding.
- Follow `/home/ross/OSS/ai/nic/docs/coding-standards.md`.
- After the behavioral model is stable, build a driver to exercise the virtual chip.

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

### Phase 2 concrete tasks
- Define CRC polynomial/width in a header and add serialize/deserialize helpers.
- Add CRC to `DlSerializer::serialize` and validate in `DlDeserializer::deserialize` with error reporting.
- Add command flit handling for ack/replay sequence tracking and retry window.
- Add pacing hooks (interfaces or callbacks) in serialize/deserialize to model rate limits.
- Add error injection toggles for CRC failure and replay-triggered drops.

## Phase 3: Transaction Layer (TL) baseline
- Implement TL flit and half-flit serializing.
- Add basic request/response flows and flow control integration with DL.

## Phase 4: UPLI interface behavior
- Model UPLI reset, handshake, and credit flow.
- Add routing/VC rules and ordering constraints.

## Phase 5: Driver + tooling
- Build a driver that exercises the virtual UALink chip.
- Provide scenario tests and diagnostics.
