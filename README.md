# ENP — ESP Network Protocol

![Status](https://img.shields.io/badge/status-v0.2.0--Frozen-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.2-red)
![License](https://img.shields.io/badge/license-GPLv3.0-green)

ENP (ESP Network Protocol) is a lightweight, modular, transport-independent network protocol being developed for ESP32 nodes using **ESP-IDF 6.0.2** and the **ESP-NOW 2.0** transport.

ENP is intended to provide a protocol and networking layer above transports such as ESP-NOW without exposing transport-specific details to services and applications.

The v0.2 milestone established the frozen one-hop protocol foundation. The project has since extended that foundation with routing, multi-hop forwarding, DATA/ACK data-plane behavior, duplicate suppression across multi-hop traffic, and a hardware-validated retransmission/ACK-recovery test path.

---

**Project status: v0.2 foundation frozen; routing and E3.3 data-plane extensions hardware-validated; E3.3.7 reliability layer is an approved draft and is not yet implemented.**

Status terminology used in this repository:

- 🔒 **FROZEN** — approved contract/baseline that should not be changed casually.
- ✅ **VALIDATED** — implemented and validated, including hardware validation where applicable.
- 🟡 **PROPOSED** — specified/approved design that is not yet frozen as an implementation.
- 🚧 **IMPLEMENTING** — implementation work is active and not yet validated.
- 📜 **HISTORICAL** — records a previous project state and should not be interpreted as the current state.

---

## 1. v0.2 Architecture

```text
                         ENP v0.2 / E3
                              │
             ┌────────────────┴────────────────┐
             │                                 │
          ENP Core                        ESP-NOW Transport
             │                                 │
    ┌────────┼─────────────┐                   │
    │        │             │                   │
  Packet  Dispatcher   Discovery/Neighbor      │
    │        │             │                   │
    │     Duplicate Cache │                   │
    │                      │                   │
    └──────────── Routing / Forwarding ────────┘
                         │
                         ▼
                  E3 DATA / ACK
                  data-plane tests
                         │
                         ▼
                 E3.3.7 Reliability
                    (PROPOSED)

### Receive path

```text
ESP-NOW RX
    │
    ▼
Transport receive path
    │
    ▼
ENP packet validation
    │
    ▼
Duplicate detection
    │
    ├── DUPLICATE ──► DROP
    │
    └── NEW
         │
         ▼
      Dispatcher
         │
         ▼
       Service
```

The duplicate check occurs before service dispatch.

---

## 2. Frozen and Hardware-Validated Features

| Component | Status |
|---|---|
| ENP core types | 🔒 Frozen |
| Logical addressing | 🔒 Frozen |
| Packet format | 🔒 Frozen |
| CRC16 | 🔒 Frozen |
| Transport abstraction | 🔒 Frozen |
| ESP-NOW 2.0 transport | ✅ Hardware validated |
| Static receive path | ✅ Hardware validated |
| Dispatcher | ✅ Hardware validated |
| Service contract | 🔒 Frozen |
| Discovery | ✅ Hardware validated |
| Neighbor table | ✅ Hardware validated |
| Periodic Discovery | ✅ Hardware validated |
| Neighbor aging | ✅ Hardware validated |
| Neighbor stale/recovery | ✅ Hardware validated |
| Sequence number | 🔒 Frozen |
| Duplicate cache | ✅ Hardware validated |
| Duplicate suppression | 🔒 Frozen / hardware validated |

---

## 3. ESP-NOW Transport

The current implementation targets:

```text
ESP-IDF: 6.0.2
ESP-NOW: 2.0
```

The transport provides the ENP link between nodes while keeping the
higher-level protocol independent of the ESP-NOW API.

The validated hardware configuration uses Wi-Fi station mode and keeps
the ESP-NOW channel aligned with the connected Wi-Fi network.

Broadcast Discovery is supported through the configured ESP-NOW broadcast
peer.

---

## 4. Logical Addressing

ENP uses logical addresses rather than exposing transport MAC addresses
to the higher protocol layers.

A node is identified by:

```text
Network ID
Node ID
```

Node roles are represented independently of the transport address.

Example validated configuration:

```text
Gateway:
    Network = 1
    Node    = 1
    Role    = Gateway

Sensor:
    Network = 1
    Node    = 2
    Role    = Sensor
```

---

## 5. Packet Processing

A received packet follows this sequence:

```text
Receive
   ↓
Frame validation
   ↓
CRC validation
   ↓
Duplicate detection
   ↓
Dispatcher
   ↓
Service
```

Invalid packets are rejected before they can reach a service or poison
the duplicate cache.

---

## 6. Discovery

Discovery is the first ENP network service.

A Discovery announcement communicates:

```text
Network ID
Node ID
Node role
Capabilities
```

Discovery is transported using ESP-NOW broadcast.

The receiving node updates its neighbor table.

Hardware validation confirmed bidirectional Discovery between:

```text
Gateway ↔ Sensor
```

---

## 7. Periodic Discovery

The ENP maintenance subsystem periodically sends Discovery announcements.

Current validated interval:

```text
Discovery interval = 2000 ms
```

Periodic work is performed outside the ESP-NOW receive callback using
statically allocated FreeRTOS resources.

Hardware validation confirmed that periodic Discovery continues to
refresh neighbors during normal operation.

---

## 8. Neighbor Management

Neighbors are maintained using logical ENP addresses.

Current validated stale policy:

```text
Neighbor stale threshold = 6000 ms
```

The neighbor lifecycle is:

```text
Discovery received
       │
       ▼
    ACTIVE
       │
       │ no Discovery
       ▼
     STALE
       │
       │ Discovery resumes
       ▼
    ACTIVE
```

Hardware testing confirmed:

- neighbor discovery;
- periodic refresh;
- transition to `STALE`;
- recovery after Discovery resumes.

---

## 9. Duplicate Suppression

Duplicate suppression is part of the frozen v0.2 runtime.

### Duplicate identity

A packet is considered a duplicate using:

```text
Source Network ID
+
Source Node ID
+
Sequence Number
```

The ESP-NOW MAC address is **not** part of the duplicate identity.

This is intentional because future multi-hop forwarding must preserve
the identity of the originating ENP packet independently of the
transport peer through which it arrives.

### Cache policy

```text
Capacity = 32 entries
Lifetime = 10000 ms
Allocation = static
Owner = Dispatcher
```

The dispatcher checks the duplicate cache after packet validation and
before service dispatch.

A duplicate is consumed and dropped:

```text
VALID PACKET
    │
    ▼
Duplicate check
    │
    ├── duplicate ──► DROP
    │
    └── new ────────► SERVICE
```

### Hardware validation

The same serialized Discovery frame was transmitted three times using:

```text
Sequence = 0x7E000001
```

Observed behavior:

```text
Frame #1
    NEW
    ↓
Discovery service

Frame #2
    DUPLICATE
    ↓
Dispatcher DROP

wait 11000 ms

Frame #3
    NEW
    ↓
Discovery service
```

The Sensor hardware log confirmed:

```text
enp_discovery: Neighbor discovered: network=1 node=1 ...
enp_dispatcher: Dropped duplicate packet: network=1 node=1 seq=2113929217
enp_discovery: Neighbor discovered: network=1 node=1 ...
```

`2113929217` is `0x7E000001`.

The normal periodic Discovery traffic continued during the test.

### Isolated duplicate-cache validation

The duplicate-cache module was also independently tested for:

- first packet / duplicate behavior;
- different sequence numbers;
- different sources;
- expiration;
- 32-bit millisecond clock wrap-around;
- 32-entry capacity;
- oldest-entry replacement;
- cache clear.

All tests passed.

---

## 10. Sequence Number Boundary

The ENP packet contains a 32-bit sequence number.

For v0.2 it is frozen as a component of duplicate identity.

However, **sequence-number ordering is not yet defined**.

In particular, routing has not yet established a serial-number comparison
rule for values crossing:

```text
0xFFFFFFFF → 0x00000000
```

That decision belongs to the routing architecture specification and is
therefore deliberately outside the frozen v0.2 duplicate-suppression
contract.

---

## 11. Static Resource Policy

The current ENP implementation follows a static-resource policy for
core runtime paths.

The project uses:

```text
Static FreeRTOS queues
Static FreeRTOS tasks
Fixed-size protocol buffers/tables
Bounded neighbor table
Bounded duplicate cache
```

This is intended to make memory ownership and runtime behavior
predictable on embedded nodes.

---

## 12. Hardware Validation Summary

The v0.2 Gateway/Sensor hardware validation has confirmed:

```text
Gateway ↔ Sensor communication               ✅
ESP-NOW transport                            ✅
Packet reception                             ✅
Discovery TX/RX                              ✅
Bidirectional Discovery                      ✅
Periodic Discovery                           ✅
Neighbor table updates                       ✅
Neighbor aging                               ✅
STALE transition                             ✅
STALE → ACTIVE recovery                      ✅
Duplicate cache                              ✅
Dispatcher duplicate suppression             ✅
NEW → DUPLICATE → EXPIRED → NEW              ✅
Normal Discovery after duplicate testing     ✅
```

The normal Gateway and Sensor firmware were subsequently flashed again
and confirmed operational after removal of the temporary replay
diagnostic.

---

## 13. Current Project Scope

The original v0.2 one-hop foundation remains frozen.

The following higher-level capabilities have subsequently been implemented and hardware-validated through the E3 test series:

```text
Route discovery
Route selection
Multi-hop forwarding
TTL handling in tested forwarding paths
DATA multi-hop delivery
ACK multi-hop delivery
DATA duplicate suppression
ACK duplicate suppression
DATA retransmission / ACK recovery test behavior
```

The following remain outside the frozen production reliability implementation:

```text
General-purpose reliability subsystem
Configurable ACK scheduling
General-purpose timeout/retry management
Retry accounting API
Delivery failure API
Fragmentation
Security
OTA
```

E3.3.6 is a **hardware-validated reliability behavior test**. It does not by itself mean that the general ENP reliability subsystem has been implemented.

---

## 14. Current Milestones

```text
E3.3.1  DATA wire/self-test                     VALIDATED
E3.3.2  DATA multi-hop forwarding               VALIDATED
E3.3.3  DATA + ACK multi-hop path               VALIDATED
E3.3.4  DATA duplicate suppression              VALIDATED
E3.3.5  ACK duplicate suppression               VALIDATED
E3.3.6  DATA retransmission / ACK recovery      VALIDATED
E3.3.7  Reliability layer architecture          PROPOSED
```

The E3.3.7 specification must be reviewed against the current project
documentation and implementation before its API and implementation are frozen.

---

## 15. Development / Freeze Rule

ENP development follows:

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
Hardware validation
  ↓
Documentation
  ↓
Freeze
```

A feature is not considered complete merely because its headers, enums,
or placeholders exist.

---

## 16. Project Status

**ENP v0.2 foundation — FROZEN**

**E3.3.1–E3.3.6 — VALIDATED**

**E3.3.7 Reliability Layer — APPROVED DRAFT / NOT IMPLEMENTED**

The project is currently at the documentation and architecture checkpoint
between E3.3.6 validation and E3.3.7 implementation.
