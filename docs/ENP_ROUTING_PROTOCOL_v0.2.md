# ENP Routing Protocol Specification v0.2

**Status:** Proposed protocol contract — implementation gate

**Architecture:** ENP Hybrid Routing

**Wire packet type:** `ENP_PACKET_ROUTE` (`5`)

**Initial metric:** Hop Count

**Transport:** Transport-independent; ESP-NOW is the current implementation

---

## 1. Purpose

This document defines the wire-level routing control protocol for ENP v0.2.
It follows the approved Routing Architecture Specification and defines the
three routing control messages:

```text
RREQ  Route Request
RREP  Route Reply
RERR  Route Error
```

No routing implementation should be created until this contract is reviewed.

---

## 2. Existing ENP Frame

Routing messages use the existing ENP frame without changing the 26-byte
header.

```text
+---------------------------+
| ENP Header      26 bytes  |
+---------------------------+
| Routing Payload           |
+---------------------------+
| CRC16           2 bytes   |
+---------------------------+
```

The existing header fields remain authoritative:

```text
magic
version
type = ENP_PACKET_ROUTE
flags
ttl
source
destination
payload_length
sequence
```

The existing ENP wire protocol uses little-endian byte order for multi-byte
integer fields.

---

## 3. Two Different Sequence Domains

ENP deliberately separates **packet sequence** from **routing freshness**.

### 3.1 Packet sequence

The existing 32-bit `enp_sequence_t` in the ENP header identifies an ENP
packet for duplicate suppression.

It is used as:

```text
Source Network + Source Node + Packet Sequence
```

It must not be used as the route-freshness sequence.

A forwarded packet keeps the same packet sequence.

### 3.2 Routing sequence

Routing uses a separate 32-bit **destination sequence number** inside RREQ,
RREP, and RERR payloads.

This number represents the freshness of routing information for a particular
ENP destination.

The two sequence domains must never be conflated.

---

## 4. Routing Sequence Semantics

Every ENP node owns one routing sequence number:

```text
route_sequence[node]
```

It is a 32-bit unsigned value.

### 4.1 Initialization

A node initializes its local routing sequence to a non-zero value. The
implementation may derive the initial value from persistent state or a
startup entropy source; the protocol does not require a particular
initialization mechanism.

### 4.2 Increment

A node increments its own routing sequence whenever it originates fresh
routing information that must supersede previously advertised information.

The sequence is never copied from another node.

### 4.3 Ownership

Only the node identified by a destination address may originate a new
sequence number for that destination.

Intermediate nodes may store, compare, and forward a destination sequence
number, but must not invent a newer value for another node.

### 4.4 Comparison

Routing sequence numbers use serial-number arithmetic over 32 bits.

For values `a` and `b`, `a` is newer than `b` when:

```c
(int32_t)(a - b) > 0
```

Equality means the same routing generation.

The protocol assumes that two valid sequence values being compared are never
separated by 2^31 or more increments.

This rule explicitly defines wrap-around behavior.

---

## 5. Routing Payload Common Header

Every routing payload starts with:

```text
+--------+--------+--------+--------+
| subtype| flags  | version| reserved|
+--------+--------+--------+--------+
```

All fields are one byte.

### 5.1 Subtypes

```text
1 = RREQ
2 = RREP
3 = RERR
```

All other values are reserved and must be rejected as unsupported routing
messages.

### 5.2 Payload version

The initial routing payload version is:

```text
ENP_ROUTING_PAYLOAD_VERSION = 1
```

This is independent of the ENP software milestone and permits future routing
payload evolution without changing the complete ENP frame version.

### 5.3 Flags

v0.2 defines:

```text
0x00 = no flags
```

All currently undefined flag bits must be transmitted as zero and ignored or
rejected according to the protocol compatibility policy. No v0.2 feature
may depend on an undefined flag.

---

## 6. RREQ — Route Request

RREQ discovers a route from the requesting node to a destination.

### 6.1 RREQ wire format

```text
+----------------------+-------+
| Common routing hdr   | 4     |
+----------------------+-------+
| Request ID           | 4     |
+----------------------+-------+
| Destination address  | 6     |
+----------------------+-------+
| Destination sequence | 4     |
+----------------------+-------+
| Hop count            | 1     |
+----------------------+-------+
| Reserved             | 1     |
+----------------------+-------+
```

Total payload size:

```text
20 bytes
```

### 6.2 RREQ frame header

For an RREQ:

```text
source      = route-discovery originator
 destination = broadcast address for network flooding
sequence    = ordinary ENP packet sequence
TTL         = discovery ring TTL
```

The requested destination is carried in the RREQ payload.

### 6.3 Request ID

`Request ID` is allocated by the RREQ originator and is monotonically
incremented with serial-number semantics.

The RREQ identity is:

```text
Originator address + Request ID
```

The ordinary packet sequence is not used as the semantic RREQ identity.

### 6.4 Destination sequence

The originator places its most recently known destination sequence in the
RREQ.

If no valid destination sequence is known:

```text
Destination sequence = 0
```

A value of zero means "unknown", not necessarily older than a non-zero
sequence.

### 6.5 Hop count

The originator creates the RREQ with:

```text
hop_count = 0
```

Each forwarding node increments it by one before forwarding.

### 6.6 RREQ processing

A node receiving an RREQ must first reject it if:

- the ENP frame is invalid;
- CRC is invalid;
- routing payload version is unsupported;
- subtype is invalid;
- RREQ identity has already been processed;
- TTL prevents further propagation.

If the node is the requested destination, it may generate an RREP.

Otherwise it forwards the RREQ if the forwarding policy permits it.

---

## 7. Expanding-Ring RREQ

RREQ discovery is performed using expanding rings.

The initial discovery uses a small TTL. If no route is established, the
originator retries with a larger TTL until the configured discovery limit is
reached.

The routing layer must never forward a packet whose TTL has expired.

Recommended initial v0.2 constants are:

```text
Initial discovery TTL = 2
Discovery TTL step     = 2
Maximum discovery TTL  = ENP_MAX_TTL (16)
```

These are protocol configuration values, not wire-format fields.

A future implementation may make the values configurable without changing
the wire format.

---

## 8. RREP — Route Reply

RREP confirms a discovered route.

### 8.1 RREP wire format

```text
+----------------------+-------+
| Common routing hdr   | 4     |
+----------------------+-------+
| Destination address  | 6     |
+----------------------+-------+
| Destination sequence | 4     |
+----------------------+-------+
| Hop count            | 1     |
+----------------------+-------+
| Route lifetime       | 4     |
+----------------------+-------+
| Reserved             | 1     |
+----------------------+-------+
```

Total payload size:

```text
20 bytes
```

### 8.2 RREP frame header

The RREP is sent toward the RREQ originator.

```text
source      = node generating the RREP
 destination = RREQ originator
sequence    = ordinary ENP packet sequence
TTL         = normal routing control TTL
```

The destination address in the RREP payload identifies the destination for
which the route is being confirmed.

### 8.3 RREP route metric

`Hop count` is the accumulated path length from the RREP source/destination
to the route originator.

Each forwarding node increments the hop count before forwarding according to
the selected path representation.

The final originator installs the route with the corresponding next hop.

### 8.4 Route lifetime

`Route lifetime` is expressed in milliseconds as a 32-bit unsigned value.

The v0.2 protocol recommends:

```text
Route lifetime = 30000 ms
```

The implementation must refresh or invalidate routes according to this
lifetime and the neighbor state.

---

## 9. RERR — Route Error

RERR informs nodes that a destination route is no longer usable.

### 9.1 RERR wire format

```text
+----------------------+-------+
| Common routing hdr   | 4     |
+----------------------+-------+
| Unreachable address  | 6     |
+----------------------+-------+
| Destination sequence | 4     |
+----------------------+-------+
| Reason               | 1     |
+----------------------+-------+
| Reserved             | 1     |
+----------------------+-------+
```

Total payload size:

```text
16 bytes
```

### 9.2 Reason codes

```text
1 = next hop unavailable
2 = transport transmission failure
3 = route expired
4 = local repair failed
5 = route invalidated by protocol policy
```

Other values are reserved.

### 9.3 RERR processing

A node receiving an RERR invalidates a route only when the RERR applies to
its installed route and is at least as fresh as the stored routing
information.

An RERR must not invalidate a newer route.

---

## 10. Route Freshness Rules

When comparing route information for the same destination:

1. A newer destination sequence wins.
2. If sequence numbers are equal, lower metric wins.
3. If sequence and metric are equal, deterministic tie-breaking is used.
4. Invalid information never replaces a valid newer route.

Unknown destination sequence (`0`) may be replaced by any known sequence.

A known sequence must not be replaced by an unknown sequence merely because
the unknown route has a lower metric.

---

## 11. Route Discovery Duplicate Suppression

The same RREQ may arrive through multiple neighbors.

Only the first accepted instance of:

```text
Originator + Request ID
```

is eligible for forwarding by a node within the active discovery window.

Subsequent copies are dropped as routing-control duplicates.

The ordinary ENP packet duplicate cache remains useful as an additional
packet-level safety mechanism, but the routing protocol identity above is
the authoritative semantic identity for RREQ suppression.

---

## 12. RREQ Reverse-Path State

An intermediate node receiving a new RREQ records the neighbor from which
the RREQ arrived as the reverse next hop toward the originator.

Conceptually:

```text
RREQ arrives:

Originator A
     ↓
     B  ← received from A
     ↓
     C  ← received from B
```

C stores:

```text
route to A = B
```

This reverse state is temporary and exists to return the RREP toward the
originator.

The reverse state must have a bounded lifetime and must not become an
unbounded route table.

---

## 13. RREP Forwarding

When an intermediate node receives an RREP:

1. Validate the message.
2. Identify the destination route represented by the RREP.
3. Update/install route information if the RREP is fresher or better.
4. Select the next hop toward the RREQ originator using the temporary reverse
   discovery state.
5. Decrement TTL.
6. Forward the RREP.

The RREP must not be flooded.

---

## 14. RERR Propagation

RERR is propagated only to nodes for which the failed route information is
relevant.

The first implementation may use bounded unicast RERR propagation based on
known route state rather than network-wide broadcast.

If no relevant upstream route exists, the RERR may be discarded locally.

---

## 15. Data Forwarding After Discovery

Once a route is valid:

```text
Application
    ↓
Destination lookup
    ↓
Valid route
    ↓
TTL check
    ↓
TTL--
    ↓
Transport next-hop send
```

The packet's:

```text
source
 destination
sequence
```

remain unchanged.

Only the forwarding TTL is modified.

---

## 16. Local Route Repair

When a next hop fails, the node first marks the affected route stale and
attempts local repair.

The repair operation must use a bounded discovery TTL based on the failed
route's previous metric.

If repair succeeds:

```text
STALE → VALID
```

If repair fails:

```text
STALE → INVALID
```

and an RERR may be generated where appropriate.

The exact repair timing and retry limits are implementation constants.

---

## 17. TTL Rules

TTL is an unsigned 8-bit field in the existing ENP header.

For any forwarding operation:

```text
if ttl <= 1:
    do not forward
else:
    ttl = ttl - 1
    forward
```

A packet received with TTL zero must be dropped.

RREQ and ordinary data packets use the same fundamental TTL decrement rule.

The discovery ring is formed by selecting different initial RREQ TTL values.

---

## 18. Transport Independence

The routing wire protocol contains only ENP logical addresses and routing
information.

It does not contain:

```text
ESP-NOW MAC addresses
Wi-Fi channels
transport peer handles
RSSI-specific fields
```

The transport implementation maps the selected logical next hop to its
transport-specific destination.

---

## 19. MTU / Frame Size

The current ENP frame limit is:

```text
250 bytes
```

The routing payloads defined here are deliberately small:

```text
RREQ = 20 bytes
RREP = 20 bytes
RERR = 16 bytes
```

No routing control message requires fragmentation.

This keeps the routing protocol suitable for ESP-NOW while remaining
transport-independent.

---

## 20. Broadcast Semantics

RREQ requires a broadcast-capable discovery operation in the current ESP-NOW
implementation.

However, the routing layer must not directly depend on ESP-NOW broadcast.

The transport abstraction must expose whether broadcast or multicast is
available.

For transports without native broadcast, a transport-specific adaptation
may emulate discovery or return an unsupported-capability result.

RREP is unicast.

RERR is normally unicast or bounded to affected upstream nodes.

---

## 21. Control Packet Sequence Numbers

Every RREQ, RREP, and RERR is still an ordinary ENP packet and therefore has
an ordinary ENP packet sequence number in the frame header.

This number is used for packet-level duplicate protection.

Routing-specific freshness and request identity remain separate:

```text
ENP packet sequence
    ≠
RREQ Request ID
    ≠
Destination route sequence
```

This separation is mandatory.

---

## 22. Error Handling

A routing implementation must reject:

- malformed routing payloads;
- unsupported routing payload versions;
- unknown routing subtypes;
- impossible payload lengths;
- invalid destination addresses;
- expired TTL;
- duplicate RREQs;
- stale route updates;
- invalid RERR updates.

Malformed routing control packets must not modify the route table.

---

## 23. Static Resource Requirements

Routing state is bounded.

The existing project defines:

```text
ENP_MAX_ROUTES = 64
```

The implementation should use that as the initial maximum route-table size.

Temporary RREQ reverse-path/discovery state must also have a fixed upper
bound. The exact constant should be defined before implementation.

No unbounded allocation is permitted in the routing path.

---

## 24. Recommended Initial Timers

The following values are the proposed v0.2 implementation defaults:

```text
Initial RREQ TTL          = 2
RREQ TTL increment        = 2
Maximum RREQ TTL          = 16
Route lifetime            = 30000 ms
RREQ response timeout     = 1000 ms
Maximum RREQ retries      = 3
Local repair attempts     = 1
```

These values are configuration defaults, not wire-format requirements.
They should be placed in `enp_defaults.h` rather than encoded into packet
structures.

Hardware validation may tune them later without changing the routing wire
format.

---

## 25. Metric Contract

The v0.2 wire protocol carries hop count because hop count is the initial
route metric.

The route-table implementation must nevertheless use an abstract metric
interface so that future policies can use:

```text
RSSI / link quality
SNR
latency
packet loss
reliability
ETX-like cost
energy
composite cost
```

Future metric policies must not change the RREQ/RREP/RERR structural contract
unless the metric requires additional information that cannot be derived
locally.

---

## 26. Compatibility Rules

A node implementing this specification must:

- recognize `ENP_PACKET_ROUTE`;
- validate routing payload version;
- recognize RREQ/RREP/RERR;
- reject unsupported routing payloads safely;
- preserve the existing ENP packet header semantics;
- preserve source and packet sequence during forwarding;
- enforce TTL;
- prevent repeated RREQ forwarding;
- never install a route from malformed control information.

---

## 27. Protocol State Summary

```text
                NO ROUTE
                   │
                   │ data needs destination
                   ▼
              RREQ DISCOVERY
                   │
             ┌─────┴─────┐
             │           │
          RREP          timeout
             │           │
             ▼           ▼
          VALID       expand TTL
             │           │
             │           └───► RREQ again
             │
       next-hop failure
             │
             ▼
           STALE
          /     \
     repair     repair fails
       │             │
       ▼             ▼
     VALID         INVALID
```

---

## 28. Implementation Gate

Before implementation, the following must be reviewed against this
specification:

1. Exact packed C structures and serialization.
2. Route sequence initialization and persistence policy.
3. RREQ reverse-state capacity.
4. Route-table timer behavior.
5. RREQ response timeout and retry behavior.
6. RERR propagation rules.
7. Metric abstraction API.
8. Transport capability API for broadcast and link metrics.
9. Route-table tie-breaking.
10. Hardware test plan.

No routing code should be treated as frozen until these points are resolved
and the first Gateway/Sensor hardware tests pass.

---

## 29. Relationship to ENP v0.2 Foundation

This routing protocol builds on the validated ENP v0.2 foundation:

```text
Packet / CRC             FROZEN
ESP-NOW transport        VALIDATED
Dispatcher               VALIDATED
Discovery                VALIDATED
Neighbor management      VALIDATED
Periodic Discovery       VALIDATED
Neighbor aging           VALIDATED
Duplicate suppression    VALIDATED
```

Routing adds:

```text
RREQ
RREP
RERR
Route table
Route discovery
Route repair
Forwarding
```

The routing implementation must not regress the validated one-hop behavior.
