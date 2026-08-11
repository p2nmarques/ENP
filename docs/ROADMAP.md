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

### v0.2 boundary

The following are deliberately **not implemented** in the frozen v0.2
baseline:

- [ ] Routing
- [ ] Route discovery
- [ ] Route selection
- [ ] Multi-hop forwarding
- [ ] TTL enforcement
- [ ] Reliable delivery
- [ ] Retransmission
- [ ] Fragmentation
- [ ] Security
- [ ] OTA


---

## Next milestone — Routing architecture

Before implementation, define and review:

- [ ] Route entry structure.
- [ ] Destination versus next-hop semantics.
- [ ] Route state.
- [ ] Route lifetime and expiration.
- [ ] Route metric.
- [ ] TTL decrement/discard semantics.
- [ ] Sequence-number semantics for routing.
- [ ] Forwarding rules.
- [ ] Loop prevention.
- [ ] Route discovery.
- [ ] Route invalidation and repair.
- [ ] Gateway/root-node behavior.

The routing specification must not destabilize the frozen v0.2
Discovery/Neighbor/Duplicate foundation.

---

## Later

### Reliability

- [ ] ACK handling
- [ ] Retransmission
- [ ] Retry policy
- [ ] Fragmentation
- [ ] Reassembly

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
