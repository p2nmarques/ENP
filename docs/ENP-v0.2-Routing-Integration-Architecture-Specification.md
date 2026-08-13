# ENP v0.2 — Routing Integration Architecture Specification

**Status:** Proposed integration baseline  
**Target:** ESP-IDF 6.0.2  
**Transport:** Transport-independent; ESP-NOW is the current implementation

## 1. Purpose

This specification defines the integration boundary between the ENP v0.2
routing components already validated independently:

- R3-A — route metric abstraction
- R3-B — route table
- R4-A / R4-A.1 — route discovery state and RREP correlation
- R4-B — RREQ processor
- R4-C — RREP processor
- R4-D — RERR processor

The objective is to assemble these components without coupling routing logic
to ESP-NOW.

## 2. Architectural principle

Routing owns routing decisions.

Transport owns transmission and reception.

The integration layer connects the two.

```text
                  ENP APPLICATION / DATA PLANE
                            |
                            v
                    +---------------+
                    | Route Manager  |
                    | / Integration  |
                    +-------+-------+
                            |
          +-----------------+------------------+
          |                 |                  |
          v                 v                  v
       R4-B RREQ         R4-C RREP          R4-D RERR
          |                 |                  |
          +-----------------+------------------+
                            |
                            v
                       R3-B Route Table
                            ^
                            |
                       R3-A Metric
                            |
                            v
                    Transport Adapter
                            |
              +-------------+-------------+
              |                           |
           ESP-NOW                    Future transport
```

## 3. Component responsibilities

### R3-A — Metric

Provides metric creation, hop accumulation, comparison and validity.

It must not know about transports, packets or route discovery.

### R3-B — Route Table

Owns route storage and route state.

It provides:

- insert
- update
- lookup
- invalidate
- remove
- expiration

It must not transmit packets.

### R4-A / R4-A.1 — Discovery

Owns the local lifecycle of an outstanding route discovery:

```text
IDLE -> REQUESTING -> COMPLETE
                    |
                    +-> FAILED
```

It owns request ID correlation and destination-sequence freshness.

### R4-B — RREQ Processor

Processes an incoming RREQ.

Responsibilities:

1. validate RREQ;
2. perform duplicate suppression;
3. learn reverse route;
4. detect destination;
5. generate/request RREP at destination;
6. decrement TTL and increment hop count before forwarding.

It does not directly access ESP-NOW.

### R4-C — RREP Processor

Processes an incoming RREP.

Responsibilities:

1. validate RREP;
2. learn/update the route to the RREP destination;
3. determine whether this node is the discovery originator;
4. complete discovery at the originator;
5. otherwise obtain the next hop toward the originator;
6. increment hop count before forwarding.

### R4-D — RERR Processor

Processes an RERR and determines whether a locally installed route must
be invalidated.

It does not invent an originator/precursor forwarding mechanism because
the current v0.2 RERR wire format does not contain one.

## 4. Integration layer

The integration layer is the only component that knows how routing control
messages become transport transmissions.

It provides these logical operations:

```c
routing_tx_control(next_hop, payload);
routing_tx_data(next_hop, packet);
routing_lookup_next_hop(destination, route);
routing_start_discovery(destination);
routing_process_rx(packet, previous_hop);
```

The exact C API will be defined during implementation.

## 5. Receive path

All transports feed received ENP packets into one routing ingress:

```text
Transport RX
    |
    v
ENP packet validation
    |
    v
ENP dispatcher
    |
    +---- DATA --------> data forwarding / delivery
    |
    +---- RREQ --------> R4-B
    |
    +---- RREP --------> R4-C
    |
    +---- RERR --------> R4-D
```

The routing processors must receive a normalized transport-independent
`previous_hop` / peer identity.

They must never receive an ESP-NOW-specific structure.

## 6. Transmit path

Routing processors return an action to the integration layer rather than
calling a transport directly.

Conceptually:

```text
R4-B / R4-C / R4-D
        |
        v
   routing action
        |
        v
 Integration layer
        |
        v
 Transport abstraction
        |
        v
 ESP-NOW
```

This makes replacement of ESP-NOW possible without changing routing.

## 7. RREQ integration flow

```text
Application requests route
          |
          v
R4-A start discovery
          |
          v
Integration creates RREQ
          |
          v
Transport send
          |
          v
       network
          |
          v
R4-B at each intermediate node
          |
          +--> duplicate? DROP
          |
          +--> destination? generate RREP
          |
          +--> TTL exhausted? DROP
          |
          +--> otherwise FORWARD
```

Reverse routes are learned while the RREQ propagates.

## 8. RREP integration flow

```text
Destination
    |
    | RREP
    v
Transport
    |
    v
R4-C
    |
    +--> learn destination route
    |
    +--> local originator?
    |       |
    |       +--> R4-A.1 COMPLETE
    |
    +--> otherwise
            |
            v
       route lookup
            |
            v
       forward RREP
```

## 9. RERR integration flow

```text
Transport RX
     |
     v
R4-D
     |
     v
route lookup
     |
     +--> absent/inactive --> ignore
     |
     +--> stale sequence --> ignore
     |
     +--> current/newer --> invalidate
                              |
                              v
                         Integration
                              |
                              v
                  notification / recovery
```

RERR generation and propagation policy remains an integration-layer
responsibility until a precursor/notification mechanism is explicitly
specified.

## 10. Route discovery versus existing routes

The route manager follows this policy:

```text
Need destination
      |
      v
Route lookup
      |
  +---+---+
  |       |
found    miss
  |       |
  v       v
use     start R4-A
route     |
          v
        RREQ
          |
          v
        RREP
          |
          v
      route table
```

This is the core of the agreed hybrid model:

- proactive information is maintained in the route table;
- discovery is reactive when a route is missing or unusable;
- route metrics are abstracted from the beginning.

## 11. Transport abstraction

The routing layer must depend on a minimal transport contract:

```text
send(control/data, destination_peer)
```

and normalized receive metadata:

```text
source peer
RSSI / link metadata if available
receive timestamp if available
payload
payload length
```

Transport-specific capabilities must not leak into routing APIs.

ESP-NOW may provide richer metadata than another transport. Such metadata
must be optional and capability-based.

## 12. Transport-independent routing information

Routing may depend on:

- ENP node identity
- previous hop
- destination identity
- route sequence
- route request ID
- hop count
- TTL
- metric
- route lifetime
- normalized link-quality information where supported

Routing must not depend on:

- `esp_now_send()`
- `esp_now_recv_info_t`
- Wi-Fi MAC-specific structures
- ESP-NOW peer configuration
- Wi-Fi channel management

## 13. Concurrency model

The integration layer should serialize routing-table mutations.

Recommended model:

```text
Transport RX callback
        |
        v
small normalized RX event
        |
        v
Routing task
        |
        +--> RREQ
        +--> RREP
        +--> RERR
        +--> route maintenance
```

ESP-NOW callbacks should not execute complex routing logic.

This is particularly important for the eventual swarm workload.

## 14. Error handling

Processors distinguish:

- invalid protocol input;
- valid message with no applicable route;
- stale routing information;
- callback/storage failure;
- successful processing.

The integration layer converts those outcomes into:

- drop;
- forward;
- route invalidation;
- discovery completion/failure;
- diagnostic event.

## 15. Loop and duplicate protection

The existing duplicate cache remains independent from the route table.

```text
ENP packet sequence
        |
        v
Duplicate suppression
        |
        v
Routing control processing
```

Route request IDs are separate from normal ENP packet sequence numbers.

This separation is already part of the routing wire model.

## 16. Future transport support

The architecture must permit:

- ESP-NOW
- another 802.11-based transport
- wired transport
- long-range radio
- simulator/test transport

without modifying R3/R4 routing logic.

A transport can expose additional capabilities through a capability
descriptor, but routing must have a functional baseline when those
capabilities are absent.

## 17. Integration test strategy

The next implementation phase is staged:

### E1 — Hardware-independent integration

Connect:

```text
R4-A.1 + R4-B + R4-C + R4-D + R3-B + R3-A
```

using mock callbacks.

Test:

- complete RREQ -> RREP route establishment;
- route-table updates;
- discovery completion;
- duplicate RREQ suppression;
- TTL termination;
- RERR invalidation;
- stale RERR protection.

### E2 — Transport adapter integration

Connect the same integration layer to a mock transport.

No ESP-NOW dependency in routing tests.

### E3 — ESP-NOW integration

Connect the real ESP-NOW adapter.

Test:

- gateway -> sensor discovery;
- multi-hop discovery;
- route establishment;
- route reuse;
- route expiration;
- RERR propagation/recovery;
- duplicate suppression;
- packet forwarding.

### E4 — Multi-node swarm tests

Test:

- multiple simultaneous discoveries;
- changing topology;
- route competition;
- metric comparison;
- node disappearance;
- recovery;
- route convergence.

## 18. Important wire-format consistency gate

Before implementing E1, the routing wire definitions must be treated as the
single source of truth.

The current `enp_routing.h` defines:

- RREQ = 20 bytes;
- RREP = 20 bytes;
- RERR = 16 bytes;

and defines RERR as:

```text
payload_version
subtype
unreachable_network_id
unreachable_node_id
destination_sequence
reason
reserved
reserved_1
```

The standalone R4-D test package created during development used a temporary
equivalent RERR representation with additional header fields. That temporary
representation must NOT be merged into the main ENP tree.

E1 must use the canonical `enp_routing.h` / `enp_routing.c` wire definitions.

## 19. Integration API design rule

Do not create one giant routing function.

Prefer narrow interfaces:

```text
routing manager
    |
    +-- discovery
    +-- route table
    +-- RREQ processor
    +-- RREP processor
    +-- RERR processor
    +-- transport adapter
```

This preserves unit-testability and makes future transport replacement
practical.

## 20. Acceptance criteria for E1

E1 is complete only when a hardware-independent test can demonstrate:

1. source starts discovery;
2. RREQ is generated;
3. intermediate node learns reverse route;
4. destination produces RREP;
5. intermediate node learns forward route;
6. RREP reaches originator;
7. discovery enters COMPLETE;
8. subsequent data lookup uses the installed route;
9. duplicate RREQ is suppressed;
10. expired route is not selected;
11. RERR invalidates the affected route;
12. stale RERR does not invalidate a newer route;
13. no test depends on ESP-NOW APIs.

