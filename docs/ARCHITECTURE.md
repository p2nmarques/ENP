# ENP v0.2 Architecture

## 1. Purpose

This document describes the architecture of the ENP v0.2 implementation.

The architecture is intentionally layered so that protocol services do not depend directly on ESP-NOW.

---

## 2. Layers

```text
+------------------------------------------------------+
| Application                                          |
+------------------------------------------------------+
| ENP Services                                         |
| Discovery                                            |
+------------------------------------------------------+
| ENP Core                                             |
| Context | Dispatcher | Network | Neighbor | Protocol |
+------------------------------------------------------+
| Transport abstraction                                |
| enp_transport                                        |
+------------------------------------------------------+
| Link implementation                                  |
| enp_transport_espnow                                 |
+------------------------------------------------------+
| ESP-IDF                                              |
+------------------------------------------------------+
```

### Application

Owns the runtime entry point and supplies configuration.

The application should not parse ENP frames manually.

### Services

Services implement protocol/application functions associated with ENP packet types.

A service receives:

```text
enp_context_t
enp_packet_t
transport source
```

### Core

The core owns ENP runtime state and common protocol infrastructure.

Important modules:

```text
enp_context
enp_network
enp_node
enp_neighbor
enp_dispatcher
enp_packet
enp_crc16
enp_protocol
enp_transport
```

### Transport abstraction

`enp_transport` provides raw byte transmission and reception without exposing the link implementation.

### Link implementation

`enp_transport_espnow` maps the generic transport interface to ESP-NOW.

---

## 3. Context ownership

One ENP runtime instance is represented by:

```c
enp_context_t
```

It contains:

```text
network
neighbors
transport pointer
```

The context does not own the transport object.

The application supplies the transport instance.

---

## 4. Packet receive path

```text
ESP-NOW receive callback
        ↓
copy frame
        ↓
StaticQueue
        ↓
StaticTask
        ↓
ENP transport callback
        ↓
ENP dispatcher
        ↓
packet validation
        ↓
service lookup by packet type
        ↓
service process callback
```

The ESP-NOW callback must remain short and non-blocking.

---

## 5. Dispatcher

The dispatcher is responsible for:

1. receiving a complete ENP frame;
2. validating the packet;
3. determining its packet type;
4. locating the registered service;
5. invoking the service.

The dispatcher does not implement Discovery, Routing, or application logic.

---


## 5A. Periodic maintenance

The current implementation adds a statically allocated ENP maintenance task. Every 2 seconds it:

1. expires neighbors whose `last_seen_ms` is at least 6 seconds old;
2. sends a Discovery announcement.

The task is intentionally outside the ESP-NOW receive callback and outside the Discovery service processing callback.

The periodic mechanism is implemented but must still be hardware-validated with node power-off and recovery tests.

## 6. Service contract

A service descriptor contains:

```text
name
packet_type
optional init callback
process callback
```

The process callback receives:

```c
enp_context_t *
const enp_packet_t *
const enp_transport_address_t *
```

The service therefore has access to both:

- ENP logical source/destination information;
- physical transport source information.

---

## 7. Addressing model

ENP has two address domains.

### Logical address

```text
Network ID + Node ID
```

Used by the ENP protocol.

### Transport address

Defined by the active transport.

For ESP-NOW:

```text
6-byte MAC
```

Transport addresses are not placed into the ENP Discovery payload.

---

## 8. Broadcast model

The ENP transport abstraction represents broadcast with:

```text
transport_address.length == 0
```

The ESP-NOW implementation converts this to:

```text
FF:FF:FF:FF:FF:FF
```

and registers the broadcast peer during ESP-NOW initialization.

This keeps ESP-NOW-specific details below the transport abstraction.

---

## 9. Discovery

The Discovery service is the first validated ENP service.

Transmit:

```text
local logical source
destination = logical broadcast
type = DISCOVERY
role
capabilities
reserved = 0
```

Receive:

```text
packet source
transport source
role
capabilities
sequence
```

These are combined into a neighbor entry.

---

## 10. Neighbor model

The neighbor table represents nodes that have been directly observed.

It does not yet represent a routing table.

Current entry:

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

Routing and forwarding are deliberately outside the current Discovery implementation.

---

## 11. Time

The ENP context provides:

```c
uint32_t enp_context_time_ms(
    const enp_context_t *context);
```

The current implementation uses a monotonic FreeRTOS tick-derived millisecond value.

Services should use the ENP context time abstraction rather than directly depending on an ESP-IDF timer API.

---

## 12. Memory

The current ESP-NOW receive path uses static FreeRTOS objects:

```text
StaticQueue_t
StaticTask_t
static queue storage
static task stack
```

The transport receive callback copies incoming frames into the queue.

No dynamic allocation is required for the receive path.

---

## 13. Current validated topology

Two physical ESP32 nodes have been tested:

```text
Gateway node 1  ←──── ESP-NOW ────→  Sensor node 2
        \                           /
         \────── Wi-Fi channel 10 /
```

Both directions have been validated.

---

## 14. Future architecture

The planned evolution is:

```text
Discovery
   ↓
Neighbor table
   ↓
Link quality / heartbeat
   ↓
Routing table
   ↓
Forwarding
   ↓
Multi-hop mesh
```

Routing must not be introduced until sequence handling and duplicate suppression are defined.
