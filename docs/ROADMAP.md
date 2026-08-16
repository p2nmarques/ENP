# ENP Roadmap

## v0.2 — Current milestone

### Core

- [x] Fundamental types
- [x] Logical addressing
- [x] Node/network model
- [x] Runtime context
- [x] Transport abstraction
- [x] Packet format
- [x] CRC16
- [x] Dispatcher
- [x] Service contract

### ESP-NOW transport

- [x] ESP-IDF 6.0.2
- [x] ESP-NOW 2.0 initialization
- [x] Broadcast peer
- [x] Unicast peer handling
- [x] Static RX queue
- [x] Static RX task
- [x] Transport callback

### Discovery

- [x] Discovery payload
- [x] Discovery TX
- [x] Discovery RX
- [x] Neighbor table update
- [x] Bidirectional two-node hardware validation

### Periodic Discovery

- [x] Define Discovery announcement interval: 2 seconds.
- [x] Add periodic Discovery scheduler using a static FreeRTOS task.
- [x] Keep startup Discovery separate from periodic Discovery.
- [x] Keep periodic work outside transport callbacks.
- [x] Hardware-validate periodic announcements.

### Neighbor aging

- [x] Define stale threshold: 6 seconds.
- [x] Add periodic neighbor maintenance.
- [x] Mark inactive neighbors `STALE`.
- [x] Define current policy: retain stale entries for reuse.
- [X] Validate with node power-off/recovery hardware tests.

### Sequence and duplicate handling

- [x] Define duplicate identity: source Network ID + source Node ID + sequence.
- [x] Define duplicate cache.
- [x] Define duplicate lifetime: 10 seconds.
- [x] Validate cache timeout and `uint32_t` time wrap-around.
- [x] Validate cache capacity and oldest-entry replacement.
- [x] Integrate duplicate detection into the dispatcher.
- [x] Validate repeated frames on hardware.
- [x] Validate `NEW → DUPLICATE → EXPIRED → NEW` end-to-end.
- [x] Freeze duplicate suppression.

### Current validated extensions

The original v0.2 foundation remains frozen. Higher-level work has since
been implemented and validated through the E3 test series:

- [x] Routing components and route table
- [x] Route discovery behavior used by the E3 integration tests
- [x] Multi-hop forwarding
- [x] Tested TTL decrement/forwarding behavior
- [x] E3.3.1 DATA wire/self-test
- [x] E3.3.2 DATA multi-hop forwarding
- [x] E3.3.3 DATA + ACK multi-hop path
- [x] E3.3.4 DATA duplicate suppression
- [x] E3.3.5 ACK duplicate suppression
- [x] E3.3.6 DATA retransmission / ACK recovery test behavior

### Current boundary

The following are **not yet implemented as the general ENP reliability subsystem**:

- [ ] Reliability transaction manager
- [ ] General ACK scheduling
- [ ] General timeout handling
- [ ] General retransmission management
- [ ] Retry accounting API
- [ ] Delivery failure reporting
- [ ] Fragmentation
- [ ] Security
- [ ] OTA

### E3.3.7 — Reliability architecture

Current status: **Phase 1/2 validated; Phase 3 routing integration in progress**

Design objectives:

- [ ] Define `ACK_REQUIRED` semantics.
- [ ] Define DATA/ACK transaction correlation.
- [ ] Define reliability transaction state machine.
- [ ] Define timeout and retry policy.
- [ ] Define retry accounting.
- [ ] Define delivery success/failure reporting.
- [ ] Define static transaction storage.
- [ ] Define concurrency/event handling.
- [x] Implement reliability core.
- [x] Self-test reliability core.
- [x] Hardware-validate reliability core.
- [x] Integrate reliability with dispatcher ACK service.
- [x] Hardware-validate dispatcher integration.
- [x] Implement routing DATA-path boundary.
- [x] Hardware-validate routing DATA-path boundary.
- [x] Integrate routing DATA path with real ENP context and neighbor table.
- [x] Hardware-validate E2-A context/neighbor integration.
- [x] Hardware-validate E2-B real ESP-NOW DATA path.
- [ ] Integrate reliability submit callback with routing DATA path.
- [ ] Hardware-validate reliability-to-routing integration.
- [ ] Validate three-node reliability path.
- [ ] Freeze E3.3.7.

### Documentation checkpoint

Before E3.3.7 implementation is frozen:

- [X] Synchronize README with current project state.
- [X] Synchronize roadmap with validated E3 results.
- [X] Clarify historical versus current freeze documents.
- [X] Review routing documentation against implementation.
- [X] Review E3.3.7 reliability specification against the synchronized documentation.

### v1.x

- Security
- OTA
- Diagnostics
- CLI
- Power management
- Additional transports
- Mesh optimization

---

## Development rule

A roadmap item is not considered complete because headers, enums, or placeholders exist.

A feature becomes complete only after:

```text
Design
  ↓
Specification
  ↓
API
  ↓
Implementation
  ↓
Clean build
  ↓
Test
  ↓
Hardware validation where applicable
  ↓
Freeze
```
