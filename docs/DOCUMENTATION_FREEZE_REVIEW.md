# ENP v0.2 Documentation Freeze Review

**Source baseline:** ENP-0.2-m3  
**Review status:** Documentation synchronized with the hardware-validated v0.2 baseline

## Frozen and validated

- Core protocol types and addressing
- Packet format and CRC16
- ESP-NOW transport
- Static receive path
- Dispatcher
- Discovery
- Periodic Discovery
- Neighbor table
- Neighbor aging and recovery
- Duplicate cache
- Dispatcher duplicate suppression
- End-to-end duplicate expiry behavior

## Duplicate suppression validation

The same sealed Discovery frame was transmitted three times:

```text
0x7E000001

Frame #1 → NEW → Discovery
Frame #2 → DUPLICATE → DROP
wait 11000 ms
Frame #3 → NEW → Discovery
```

The Sensor hardware log confirmed the duplicate was consumed by the
dispatcher and did not reach the Discovery service.

## Explicit v0.2 boundaries

The following remain outside the frozen implementation:

- route discovery
- route selection
- multi-hop forwarding
- active TTL enforcement
- reliable delivery
- retransmission
- fragmentation
- security
- OTA

## Sequence-number boundary

The sequence number is frozen as a 32-bit packet identity component for
duplicate suppression.

A sequence ordering/serial-number comparison rule for routing is **not**
yet frozen. That must be designed as part of the routing specification.

## Documentation files synchronized

- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/API_GUIDELINES.md`
- `docs/CORE_FREEZE.md`
- `docs/DUPLICATE_INTEGRATION.md`
- `docs/ENP_PROTOCOL_v0.2.md`
- `docs/ROADMAP.md`

This review is documentation-only and does not alter the production
protocol implementation.
