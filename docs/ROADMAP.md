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

### Neighbor aging

- [x] Define stale threshold: 6 seconds.
- [x] Add periodic neighbor maintenance.
- [x] Mark inactive neighbors `STALE`.
- [x] Define current policy: retain stale entries for reuse.
- [X] Validate with node power-off/recovery hardware tests.

---

## Immediate next milestone

### Sequence and duplicate handling

- [ ] Define sequence comparison rules.
- [ ] Define wrap-around behavior.
- [ ] Define duplicate cache.
- [ ] Define duplicate lifetime.
- [ ] Validate repeated frames.

This should be completed before routing/forwarding.

---

## Later

### v0.3

- Heartbeat
- Node information
- Link-quality metadata
- RSSI integration
- Neighbor maintenance

### v0.4

- Routing table
- Route selection
- Multi-hop forwarding
- TTL enforcement
- Duplicate suppression integration

### v0.5

- Reliable delivery
- ACK handling
- Retransmission
- Retry policy
- Fragmentation
- Reassembly

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
