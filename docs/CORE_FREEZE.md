# ENP v0.2 Core Freeze

**Version:** 0.2.0  
**Status:** v0.2 baseline frozen; Discovery, neighbor maintenance, and duplicate suppression hardware-validated

This document records the v0.2 baseline that must not be changed casually while higher-level networking features are developed.

---

## Core

- [x] `enp.h`
- [x] `enp_types.h`
- [x] `enp_node.h`
- [x] `enp_network.h`
- [x] `enp_context.h`
- [x] `enp_context.c`
- [x] `enp_transport.h`
- [x] `enp_transport.c`
- [x] `enp_address.h`
- [x] `enp_address.c`

## Protocol

- [x] `enp_protocol.h`
- [x] `enp_packet.h`
- [x] `enp_packet.c`
- [x] `enp_crc16.h`
- [x] `enp_crc16.c`

## Runtime infrastructure

- [x] `enp_dispatcher.h`
- [x] `enp_dispatcher.c`
- [x] `enp_service.h`
- [x] `enp_neighbor.h`
- [x] `enp_neighbor.c`

## Link

- [x] `enp_transport_espnow.h`
- [x] `enp_transport_espnow.c`
- [x] ESP-NOW broadcast peer handling
- [x] Static RX queue
- [x] Static RX task

## Periodic maintenance status

The periodic maintenance path is **hardware-validated and frozen**.

Current behavior:

```text
Discovery interval = 2000 ms
Neighbor timeout   = 6000 ms
```

The implementation uses statically allocated FreeRTOS resources and keeps
periodic work outside the ESP-NOW receive callback.

Hardware validation confirmed:

- periodic Discovery announcements;
- neighbor transition to `STALE` after loss of Discovery;
- neighbor refresh/recovery after Discovery resumes.

## Duplicate suppression

Duplicate suppression is **hardware-validated and frozen**.

The dispatcher owns a statically allocated duplicate cache.

Duplicate identity:

```text
(source Network ID,
 source Node ID,
 sequence number)
```

Current cache policy:

```text
Capacity = 32 entries
Lifetime = 10000 ms
```

A valid packet is recorded before service dispatch. A duplicate is consumed
by the dispatcher and is not delivered to the service.

End-to-end hardware validation confirmed:

```text
NEW → service
DUPLICATE → DROP
wait > 10 seconds
same packet → NEW → service
```

The duplicate cache also passed isolated ESP32 tests covering timeout,
`uint32_t` time wrap-around, capacity/replacement, and clear behavior.


## Discovery

- [x] Discovery payload definition
- [x] Discovery service
- [x] Discovery transmit
- [x] Discovery receive
- [x] Neighbor update
- [x] Two-node hardware validation

---

## Frozen protocol properties

### Software version

```text
0.2.0
```

### Wire protocol version

```text
1
```

The software/library version and wire protocol version are distinct.

### Maximum frame

```text
250 bytes
```

### Header

```text
26 bytes
```

### CRC

```text
CRC-16/CCITT-FALSE
Polynomial: 0x1021
Initial:    0xFFFF
RefIn:      false
RefOut:     false
XorOut:     0x0000
```

CRC is serialized little-endian.

### Logical address

```text
Network ID: 16 bits
Node ID:    32 bits
Total:      6 bytes
```

### Default TTL

```text
16
```

---

## Discovery payload

The current Discovery payload is exactly 4 bytes:

```text
Offset  Size  Field
0       1     role
1       2     capabilities
3       1     reserved
```

`reserved` must be zero in v0.2.

The logical source address is in the ENP header.

The physical transport address is metadata supplied by the transport and is not part of the payload.

---

## Transport broadcast

At the generic transport API:

```text
destination.length == 0
```

means broadcast.

For ESP-NOW this is mapped to:

```text
FF:FF:FF:FF:FF:FF
```

The ESP-NOW transport installs the broadcast peer during initialization.

---

## What is NOT frozen as a completed feature

The following are intentionally not implemented merely because APIs,
enums, or placeholders exist:

- Routing
- Multi-hop forwarding
- Multi-hop forwarding
- Reliable delivery
- Retransmission
- Fragmentation
- Security
- OTA

---

## Freeze rule

Changes to frozen protocol structures or public APIs require:

1. explicit design review;
2. update of protocol documentation;
3. update of dependent code;
4. clean build;
5. hardware validation where applicable;
6. a new freeze revision if the wire/API contract changes.

