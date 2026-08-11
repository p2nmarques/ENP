# ENP — ESP Network Protocol

![Status](https://img.shields.io/badge/status-v0.2.0--draft-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.2-red)
![License](https://img.shields.io/badge/license-GPLv3.0-green)

ENP (ESP Network Protocol) is a lightweight, modular, transport-independent networking layer for ESP32 systems.

ENP is intended to provide a protocol and networking layer above transports such as ESP-NOW without exposing transport-specific details to services and applications.

---

## Current v0.2 milestone

The following functionality has been validated on two physical ESP32 nodes:

- Wi-Fi STA connection
- ESP-NOW 2.0 on ESP-IDF 6.0.2
- ESP-NOW broadcast peer
- Transport abstraction
- Static RX queue/task
- ENP packet and CRC handling
- Dispatcher
- Discovery service
- Logical and transport addressing
- Neighbor table updates
- Bidirectional Discovery
- Periodic Discovery every 2000 ms
- Neighbor aging with a 6000 ms timeout
- ACTIVE → STALE
- STALE → ACTIVE recovery

Validated topology:

```text
Gateway                         Sensor
network=1                       network=1
node=1                          node=2
role=1                          role=2
       \                       /
        \--- Wi-Fi channel 10 /
         \---- ESP-NOW -------/
```

## Neighbor lifecycle

```text
           Discovery
              │
              ▼
           ACTIVE
              │
       no Discovery for
        6000 ms
              │
              ▼
            STALE
              │
       Discovery resumes
              │
              ▼
           ACTIVE
```

Validated timing:

```text
Discovery interval: 2000 ms
Neighbor timeout:   6000 ms
```

## Hardware validation record

Gateway:

```text
network=1 node=1
```

Sensor:

```text
network=1 node=2
```

Wi-Fi channel:

```text
10
```

Observed periodic Discovery at approximately:

```text
27715 ms
29715 ms

61055 ms
63055 ms
65055 ms
67055 ms
69055 ms
```

Observed aging:

```text
Neighbor aging: 1 neighbor(s) became STALE
```

Observed recovery:

```text
Neighbor discovered: network=1 node=2 role=2 capabilities=0x0000
```

Result:

**PASS — hardware validated.**

## Receive path

```text
ESP-NOW RX callback
       ↓
StaticQueue
       ↓
StaticTask
       ↓
ENP transport callback
       ↓
Dispatcher
       ↓
Packet validation
       ↓
Discovery service
       ↓
Neighbor table
```

---

# Architecture

```text
+------------------------------------------------------+
| Applications                                         |
| Gateway | Sensor | Relay | Monitor | ...             |
+------------------------------------------------------+
| ENP Services                                         |
| Discovery | Heartbeat* | Routing* | ...              |
+------------------------------------------------------+
| ENP Core                                             |
| Context | Dispatcher | Network | Protocol | Stats    |
+------------------------------------------------------+
| Transport abstraction                                |
| enp_transport                                        |
+------------------------------------------------------+
| Link implementations                                 |
| ESP-NOW | Wi-Fi* | ...                               |
+------------------------------------------------------+
| ESP-IDF                                              |
+------------------------------------------------------+

* not yet part of the validated v0.2 runtime path
```

The dependency direction is:

```text
Application
    ↓
ENP Services
    ↓
ENP Core
    ↓
Transport abstraction
    ↓
Link implementation
    ↓
ESP-IDF
```

ENP services do not directly call ESP-NOW APIs.

---

# Core components
## Current status

| Component | Status |
|---|---|
| `enp_address` | Frozen |
| `enp_types` | Frozen |
| `enp_node` | Frozen |
| `enp_network` | Frozen |
| `enp_context` | Frozen |
| `enp_transport` | Frozen |
| `enp_packet` | Frozen |
| `enp_crc16` | Frozen |
| `enp_protocol` | Frozen |
| `enp_dispatcher` | Hardware validated |
| `enp_service` | Frozen |
| `enp_neighbor` | Hardware validated |
| ESP-NOW transport | Hardware validated |
| Discovery | Hardware validated |
| Periodic Discovery | Hardware validated |
| Neighbor aging | Hardware validated |

---

# Addressing

ENP distinguishes logical and transport identities.

## Logical address

```text
Network ID : 16 bits
Node ID    : 32 bits
Total      : 6 bytes
```

Logical addresses are carried by the ENP packet header.

## Transport address

Transport addresses are owned by the active transport implementation.

For ESP-NOW:

```text
6 bytes = MAC address
```

At the ENP transport abstraction level:

```text
length == 0  → transport broadcast
length == 6  → ESP-NOW unicast MAC
```

The ESP-NOW implementation maps transport broadcast to:

```text
FF:FF:FF:FF:FF:FF
```

and maintains the broadcast peer during transport initialization.

---

# Packet format

The ENP v0.2 header is 26 bytes:

```text
Magic             4
Version           1
Type              1
Flags             1
TTL               1
Source            6
Destination       6
Payload length    2
Sequence          4
-------------------
Header           26 bytes
CRC16             2 bytes
```

Maximum frame size:

```text
250 bytes
```

Maximum payload:

```text
222 bytes
```

Multi-byte values are serialized little-endian.

The wire protocol version is currently:

```text
1
```

This is intentionally distinct from the ENP software/project version:

```text
0.2.0
```

---

# Discovery

Discovery is currently the first implemented ENP service.

A node broadcasts:

```text
ENP_PACKET_DISCOVERY
```

with:

```text
role
capabilities
reserved
```

The receiver combines:

```text
ENP logical source address
+
transport source address
+
Discovery payload
```

to create/update a neighbor entry.

Discovery does not place MAC addresses in the ENP payload.

---

# Neighbor table

A neighbor entry currently contains:

```text
Logical address
Transport address
Role
Capabilities
Last sequence
RSSI
Last seen time
State
```

States:

```text
EMPTY
ACTIVE
STALE
```

Periodic Discovery and neighbor aging are now implemented and hardware validated.

RSSI is currently recorded as unavailable (`0`) because the generic ENP transport receive callback does not yet expose radio metadata.

---

# Memory model

The ESP-NOW transport uses:

- `StaticQueue_t`
- statically allocated queue storage
- `StaticTask_t`
- statically allocated task stack

The ESP-NOW receive callback copies the frame into the static queue and performs no blocking application processing.

The worker task invokes the ENP transport receive callback.

---

# Current runtime flow

```text
Wi-Fi STA
   ↓
ESP-NOW initialization
   ↓
ENP context
   ↓
Dispatcher
   ↓
Discovery service registration
   ↓
Transport receive callback
   ↓
Discovery announcement
```

Receive path:

```text
ESP-NOW RX callback
   ↓
Static queue
   ↓
Static worker task
   ↓
ENP receive callback
   ↓
Dispatcher
   ↓
Packet validation
   ↓
Discovery service
   ↓
Neighbor table
```

---

# Build

Requirements:

- ESP-IDF 6.0.2
- ESP32 device
- Wi-Fi access point

Configure:

```bash
idf.py menuconfig
```

Build:

```bash
idf.py build
```

Flash and monitor:

```bash
idf.py flash monitor
```

For the current ESP-NOW + Wi-Fi STA implementation, nodes must operate on the same Wi-Fi channel.

---

# Roadmap

## v0.2 — current

- Core API
- Packet format
- CRC
- Transport abstraction
- ESP-NOW transport
- Dispatcher
- Service contract
- Discovery
- Neighbor table
- Two-node hardware validation
- Hardware validation of periodic Discovery and neighbor aging

## Next

- Duplicate suppression
- Sequence-number policy
- Better local capability configuration
- Optional transport metadata such as RSSI

## Future

- Heartbeat
- Routing table
- Multi-hop forwarding
- TTL enforcement during forwarding
- Reliable delivery/retransmission
- Fragmentation/reassembly
- Security
- OTA
- Diagnostics
- CLI

Features are not considered implemented merely because their headers or placeholders exist.

---

# Design rule

The protocol specification, public API, and implementation should be kept synchronized.

When a design decision changes:

1. Update the specification.
2. Update the API documentation.
3. Update the implementation.
4. Perform a build.
5. Perform hardware validation where applicable.
6. Freeze the result before building the next layer.


## Duplicate suppression integration

The isolated duplicate cache has passed all ESP32 tests and is now
integrated into the dispatcher.

The dispatcher checks every valid packet before service dispatch.
Duplicate packets are consumed and do not reach services.

Integration is implemented but requires a clean build and live
hardware validation before this feature is marked hardware-validated.
