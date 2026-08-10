# ENP — ESP Network Protocol

**Version:** 0.2.0  
**Status:** Core Discovery path hardware-validated  
**Target:** ESP-IDF 6.0.2  
**Primary transport:** ESP-NOW

ENP (ESP Network Protocol) is a lightweight, modular, transport-independent networking layer for ESP32 systems.

ENP is intended to provide a protocol and networking layer above transports such as ESP-NOW without exposing transport-specific details to services and applications.

![Status](https://img.shields.io/badge/status-v0.2.0-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.2-red)
![License](https://img.shields.io/badge/license-GPLv3.0-green)

---

## Current v0.2 milestone

ENP v0.2 currently has a working, hardware-validated Discovery path between two ESP32 nodes.

Validated:

- Wi-Fi STA initialization and connection.
- ESP-NOW 2.0 initialization on ESP-IDF 6.0.2.
- ESP-NOW broadcast peer handling.
- Transport abstraction.
- Static RX queue and static RX task.
- ENP packet creation and CRC validation.
- ENP dispatcher.
- Discovery service.
- Logical node addressing.
- Transport-source addressing.
- Neighbor table updates.
- Bidirectional Discovery between two physical ESP32 nodes.

Example validation:

```text
Gateway
  network = 1
  node    = 1

Sensor
  network = 1
  node    = 2

Wi-Fi channel = 10

Gateway:
  Neighbor discovered: network=1 node=2

Sensor:
  Neighbor discovered: network=1 node=1
```

The current Discovery payload is 4 bytes:

```text
role            1 byte
capabilities    2 bytes
reserved        1 byte
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
| ENP Core                                              |
| Context | Dispatcher | Network | Protocol | Stats     |
+------------------------------------------------------+
| Transport abstraction                                 |
| enp_transport                                         |
+------------------------------------------------------+
| Link implementations                                  |
| ESP-NOW | Wi-Fi* | ...                                |
+------------------------------------------------------+
| ESP-IDF                                               |
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

| Component | Responsibility | v0.2 status |
|---|---|---|
| `enp_address` | Logical ENP addresses | Frozen |
| `enp_types` | Fundamental protocol types | Frozen |
| `enp_node` | Local node model | Frozen |
| `enp_network` | Local network state | Frozen |
| `enp_context` | Runtime ownership/state | Frozen |
| `enp_transport` | Transport abstraction | Frozen |
| `enp_packet` | Packet storage/access | Frozen |
| `enp_crc16` | CRC-16/CCITT-FALSE | Frozen |
| `enp_protocol` | Wire constants/types | Frozen |
| `enp_dispatcher` | Packet validation/routing to services | Implemented |
| `enp_service` | Service contract | Frozen |
| `enp_neighbor` | Direct-neighbor table | Implemented |
| Discovery | Node announcement/neighbor learning | Hardware validated |
| ESP-NOW transport | ESP-NOW link implementation | Hardware validated |

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

Neighbor expiration support exists in the neighbor API, but automatic periodic aging is not yet part of the validated runtime loop.

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

## Next

- Periodic Discovery
- Neighbor aging/expiration task
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

