# ENP Protocol Specification v0.2

**Software version:** 0.2.0  
**Wire protocol version:** 1  
**Status:** v0.2 frozen core/discovery/duplicate-suppression baseline

---

# 1. Scope

This document defines the ENP v0.2 wire format and the protocol concepts implemented by the current reference implementation.

The current validated transport is ESP-NOW, but the ENP protocol layer is transport-independent.

---

# 2. Identity

Each node has a logical ENP address:

```text
Network ID: 2 bytes
Node ID:    4 bytes
```

Total:

```text
6 bytes
```

The transport address is separate.

For ESP-NOW, the transport address is a 6-byte MAC address.

---

# 3. Wire version

The ENP frame contains:

```text
version = 1
```

This is the wire-format version.

The current software/project version is:

```text
0.2.0
```

These must not be conflated.

---

# 4. Frame format

The frame is:

```text
+----------------------+----------------+
| Field                | Size           |
+----------------------+----------------+
| Magic                | 4 bytes        |
| Version              | 1 byte         |
| Type                 | 1 byte         |
| Flags                | 1 byte         |
| TTL                  | 1 byte         |
| Source address       | 6 bytes        |
| Destination address  | 6 bytes        |
| Payload length       | 2 bytes        |
| Sequence             | 4 bytes        |
+----------------------+----------------+
| Header               | 26 bytes       |
+----------------------+----------------+
| Payload              | 0..222 bytes   |
+----------------------+----------------+
| CRC16                | 2 bytes        |
+----------------------+----------------+
| Maximum frame        | 250 bytes      |
+----------------------+----------------+
```

Multi-byte values are serialized little-endian.

---

# 5. Magic

The current protocol magic is:

```text
0x45534E57
```

It identifies an ENP frame.

---

# 6. Packet types

Current packet types:

```text
0  INVALID
1  DISCOVERY
2  HEARTBEAT
3  SENSOR
4  ACK
5  ROUTE
6  APPLICATION
```

Only Discovery is currently implemented as a validated service in the v0.2 runtime.

The presence of a packet type in the enumeration does not imply that its service is implemented.

---

# 7. Flags

Current flags:

```text
0x00  NONE
0x01  ACK_REQUIRED
0x02  BROADCAST
0x04  ENCRYPTED
```

Flags may be combined.

Reliable delivery is not yet implemented merely because `ACK_REQUIRED` exists.

---

# 8. TTL

Current maximum/default TTL:

```text
16
```

TTL is reserved for forwarding/routing behavior.

A forwarding implementation must define decrement/discard behavior before TTL becomes an active routing mechanism.

---

# 9. Sequence number and duplicate suppression

Each packet contains a 32-bit sequence number.

For the v0.2 duplicate-suppression mechanism, packet identity is:

```text
source Network ID
+
source Node ID
+
sequence number
```

The transport source address is not part of duplicate identity.

The dispatcher owns a statically allocated duplicate cache:

```text
Capacity = 32 entries
Lifetime = 10000 ms
```

Duplicate checking occurs after complete packet validation and before
service dispatch.

A duplicate is consumed by the dispatcher and is not delivered to the
registered service.

The v0.2 runtime has been hardware-validated for:

```text
NEW → DUPLICATE → DROP → EXPIRED → NEW
```

The 32-bit sequence field is **not yet assigned an ordering/comparison
semantics** for routing. In particular, wrap-around ordering between
sequence values is not part of the frozen v0.2 routing contract.

---

# 10. CRC

ENP uses CRC-16/CCITT-FALSE:

```text
Polynomial = 0x1021
Initial    = 0xFFFF
RefIn      = false
RefOut     = false
XorOut     = 0x0000
```

The resulting CRC16 is serialized little-endian.

---

# 11. Discovery

Discovery is the first implemented ENP service.

A Discovery frame contains the normal ENP header followed by a 4-byte Discovery payload:

```text
Offset  Size  Field
0       1     role
1       2     capabilities
3       1     reserved
```

The reserved byte must be zero.

The logical source address is taken from the ENP header.

The transport source address is supplied separately by the transport callback.

The receiver updates the neighbor table with:

```text
logical address
transport address
role
capabilities
sequence
RSSI
last_seen_ms
state
```

Current RSSI is unavailable through the generic transport callback and is therefore recorded as zero.

---

# 12. Discovery broadcast

Discovery is transmitted as a logical broadcast.

At the ENP transport abstraction:

```text
transport destination length = 0
```

means broadcast.

The ESP-NOW implementation maps this to:

```text
FF:FF:FF:FF:FF:FF
```

and registers the broadcast peer before transmission.

---

# 13. Neighbor table

The neighbor table represents directly observed nodes.

Entry:

```text
address
transport_address
role
capabilities
last_sequence
rssi
last_seen_ms
state
```

States:

```text
EMPTY
ACTIVE
STALE
```

The table supports expiration through its API, but an automatic periodic aging mechanism is not yet part of the validated runtime.

---

# 14. Reliability

The v0.2 packet format contains an ACK flag and ACK packet type.

However, the complete reliable-delivery mechanism is not yet implemented.

The following are future work:

```text
ACK scheduling
timeout handling
retransmission
retry accounting
delivery failure reporting
```

These must be specified before being described as a v0.2 feature.

---

# 15. Routing

The packet format contains:

```text
destination
TTL
route packet type
```

but routing and forwarding are not implemented in the current v0.2 runtime.

Routing is planned for a later development stage.

---

# 16. Security

Encryption is not defined by the current v0.2 implementation.

The encrypted flag exists as a protocol extension point.

Security mechanisms must be specified before the flag is treated as operational.

---

# 17. Compatibility rule

The implementation and this document must remain synchronized.

A feature is considered implemented only when:

1. its API/protocol contract exists;
2. its implementation exists;
3. the project builds cleanly;
4. it has been tested at the appropriate level;
5. hardware-dependent behavior has been validated where applicable.

