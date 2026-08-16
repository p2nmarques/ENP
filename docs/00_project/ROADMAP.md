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

**Current status:**

- Phase 3 / E3C three-node reliability hardware validated;
- E3.3.7 integration sequence frozen.

Completed:

- [X] Define `ACK_REQUIRED` semantics.
- [X] Define DATA/ACK transaction correlation.
- [X] Define reliability transaction state machine.
- [X] Define timeout and retry policy.
- [X] Define retry accounting.
- [X] Define delivery success/failure reporting.
- [X] Define static transaction storage.
- [X] Implement reliability core.
- [X] Self-test reliability core.
- [X] Validate reliability core on ESP hardware.
- [X] Validate dispatcher/ACK integration on ESP hardware.
- [X] Validate routing data path on ESP hardware.
- [X] Validate context + neighbor integration on ESP hardware.
- [X] Validate real ESP-NOW origin DATA path on two ESP32 nodes.

Current:

- [x] Complete Phase 3 / E3A reliability-to-routing self-test.
- [x] Hardware-validate Phase 3 / E3B reliability-to-routing integration on two ESP32 nodes.
- [ ] Validate Phase 3 / E3C end-to-end reliability on the three-node A -> B -> C topology.
- [ ] Freeze E3.3.7 after the validated integration sequence.

Deliberately later routing-resilience work:

- [ ] Stale/lost next-hop handling.
- [ ] Link/transport failure propagation into routing.
- [ ] RERR integration.
- [ ] Route rediscovery/repair during an active reliability transaction.
- [ ] Multi-path and larger-topology validation.

Architectural rule established for E3: reliability owns the transaction and packet identity; routing owns the current path. Every transmission attempt, including retransmissions, enters the same routing submission interface so a future route repair can change the next hop without changing the reliability transaction.

### Documentation checkpoint

- [X] Synchronize README with current validated project state.
- [X] Synchronize roadmap with validated E1/E2 results.
- [X] Record the E2-A stack-overflow correction and clean validation.
- [X] Record the E2-B two-node ESP-NOW validation.
- [X] Record the E3 reliability-to-routing API audit.
- [X] Record the corrected E3 self-test validation.
- [X] Record E3B two-node hardware validation.
- [X] Define E3C three-node reliability validation.

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


## Phase 4 — Core Consolidation

### P4.1 Reusable Data Plane
- [x] Introduce reusable ENP data-plane component.
- [x] Separate DATA and ACK duplicate caches.
- [x] Route non-local DATA/ACK through existing routing data path.
- [x] Preserve existing dispatcher compatibility.
- [ ] Validate P4.1 hardware-independent self-test.

### P4.2 Reliability Runtime Integration
- [ ] Integrate reliability periodic servicing into ENP runtime maintenance.

### P4.3 E3C Consolidation
- [ ] Remove test-specific DATA/ACK forwarding from E3C.
- [ ] Revalidate three-node reliability using reusable ENP infrastructure.
