# ENP Routing Architecture Specification v0.2

**Status:** Architecture agreed — specification baseline for implementation review  
**Routing model:** Hybrid  
**Initial route metric:** Hop Count  
**Primary transport:** ESP-NOW  
**Transport abstraction:** Mandatory

## 1. Purpose

This document defines the routing architecture built on the frozen ENP v0.2
one-hop foundation. Routing must extend Discovery, Neighbor Management,
Dispatcher, Packet, Transport, and Duplicate Suppression without changing
their established semantics.

## 2. Routing Model

ENP uses **hybrid routing**:

```text
                    ENP Routing
                         │
             ┌───────────┴───────────┐
             │                       │
        PROACTIVE                 REACTIVE
             │                       │
     Direct-neighbor           Multi-hop route
        awareness                discovery
             │                       │
             └───────────┬───────────┘
                         │
                    Route Cache
                         │
                         ▼
                    Forwarding
                         │
                         ▼
                Transport Abstraction
```

Proactive behavior maintains direct-neighbor knowledge. Reactive behavior
establishes multi-hop routes when required. Valid routes are cached and
local repair is preferred when a next hop fails.

## 3. Responsibility Boundaries

**Discovery / Neighbor Management** answers: “Which nodes can I reach
directly?”

**Routing** answers: “Which next hop should I use to reach a destination?”

**Forwarding** performs destination checking, TTL handling, route lookup,
next-hop selection, and transmission.

**Transport** performs the actual link transmission. Routing must not call
ESP-NOW or other transport-specific APIs directly.

## 4. Originator vs Forwarder

The ENP source address identifies the originating node and must never be
rewritten during forwarding.

For:

```text
Node 1 → Node 2 → Node 3 → Node 4
```

the packet remains:

```text
Source      = Node 1
Destination = Node 4
```

The forwarding node and transport peer are distinct concepts.

This preserves the frozen duplicate identity:

```text
Source Network ID + Source Node ID + Packet Sequence
```

## 5. Route Discovery

ENP uses an **AODV-inspired reactive model**, adapted to the ENP protocol.

Routing control packets use:

```text
ENP_PACKET_ROUTE
```

with these initial subtypes:

```text
RREQ  Route Request
RREP  Route Reply
RERR  Route Error
```

This is not a requirement to implement full RFC AODV semantics.

## 6. Proactive Neighbor Awareness

Route discovery uses the existing neighbor table as the set of direct
forwarding candidates. ENP therefore does not need a separate one-hop
connectivity-discovery mechanism.

```text
Neighbor Table
      │
      ├── B
      ├── C
      └── D
           │
           ▼
     Route discovery
```

## 7. Expanding-Ring Discovery

RREQ propagation uses **TTL-limited expanding-ring discovery**.

```text
small TTL → search
      │
      ├── found → route established
      │
      └── not found → larger TTL → search again
```

Exact initial TTL, increment, maximum TTL, retry count, and timing values
will be defined in the Routing Protocol Specification.

## 8. Routing Control Identity

RREQ identity is separate from the ordinary data-packet sequence number.

The semantic identity of an RREQ is:

```text
Originating Node + Route Request ID
```

The existing duplicate-cache implementation may be reused to suppress
repeated control packets, but the routing protocol must define control
message identity explicitly.

## 9. RREP

A destination receiving a valid RREQ may return an RREP along the reverse
discovery relationship.

The originator installs a route such as:

```text
Destination = D
Next Hop    = B
Metric      = N
```

Intermediate route installation rules will be defined in the protocol
specification.

## 10. RERR

A node may generate RERR when an installed next hop becomes unusable,
including:

- next hop becomes stale;
- transport transmission failure;
- route expiration;
- failed local repair.

The route is invalidated locally before route-error propagation.

## 11. Local Repair

Local repair is preferred before unnecessary wider rediscovery.

```text
A → B → C → D

B fails
  ↓
local repair
  ↓
A → E → C → D
```

If repair fails within the defined policy, the route becomes invalid and
new discovery may be initiated.

## 12. Route States

The conceptual route states are:

```text
DISCOVERING
VALID
STALE
INVALID
```

`DISCOVERING` means establishment is in progress. `VALID` is usable.
`STALE` is retained information requiring validation/repair. `INVALID`
must not be used for forwarding.

## 13. Route Entry

The conceptual route entry is:

```text
RouteEntry
{
    destination
    next_hop

    metric

    state

    lifetime
    last_update

    route_sequence / freshness information

    flags / capabilities
}
```

The exact C structure is deferred until the route-table contract is
specified.

## 14. Extensible Route Metric

ENP uses an **extensible metric abstraction from the beginning**.

```text
Transport Link Information
          │
          ▼
      Link Metric
          │
          ▼
      Route Metric
          │
          ▼
      Route Selection
```

The initial v0.2 policy is:

```text
HOP_COUNT
```

Every forwarding hop contributes cost 1.

Examples:

```text
A → B             = 1
A → B → C         = 2
A → B → C → D     = 3
```

The architecture must permit future metrics such as:

- RSSI / link quality;
- SNR;
- latency;
- packet loss;
- reliability;
- ETX-like cost;
- energy cost;
- composite metrics.

The route-table and forwarding architecture must not require redesign when
a new metric policy is introduced.

## 15. Transport-Independent Metrics

Routing must not assume that a metric is available.

For example:

```text
ESP-NOW: RSSI may be available
Ethernet: RSSI is not meaningful
LoRa: RSSI/SNR may be available
```

The transport abstraction may expose optional link metrics and capabilities.
The routing metric policy decides whether and how to use them.

## 16. Route Selection

When multiple routes are valid:

1. select the best active metric;
2. use freshness as a secondary criterion where required;
3. apply deterministic tie-breaking;
4. never select an unusable next hop.

For v0.2:

```text
Primary criterion = lowest hop count
```

Tie-breaking will be frozen with the route-table specification.

## 17. Forwarding Pipeline

```text
Receive
   ↓
Packet validation
   ↓
Duplicate detection
   ↓
Is destination local?
   │
   ├── YES → service delivery
   │
   └── NO
        ↓
      TTL check
        │
        ├── expired → DROP
        │
        └── valid
             ↓
        Route lookup
             │
             ├── no valid route → route discovery
             │
             └── route found
                    ↓
              decrement TTL
                    ↓
              next-hop TX
```

The source address and packet sequence remain unchanged during forwarding.

## 18. TTL

TTL limits forwarding.

Conceptually:

```text
if TTL <= 1:
    DROP / do not forward
else:
    TTL = TTL - 1
    forward
```

Exact initial values and constants will be defined in the Routing Protocol
Specification. TTL is also used to contain RREQ propagation.

## 19. Duplicate Suppression Interaction

The frozen data-packet duplicate identity remains:

```text
Source Network ID + Source Node ID + Packet Sequence
```

TTL changes during forwarding but source and sequence do not.

Routing control packets have separate semantic identities, for example:

```text
RREQ = originator + route request ID
```

Repeated control messages must not cause uncontrolled rebroadcast.

## 20. Transport Abstraction

Routing must use only the ENP transport abstraction.

Routing must not directly call:

```text
esp_now_send()
esp_now_add_peer()
Wi-Fi APIs
ESP-NOW MAC functions
```

The transport layer owns:

- logical-to-transport next-hop resolution;
- unicast;
- broadcast/multicast where supported;
- receive delivery;
- delivery status;
- optional link metrics;
- transport capabilities.

Routing operates on ENP logical addresses.

## 21. Additional Transport Compatibility

The architecture must remain compatible with future transports such as:

```text
ESP-NOW
Wi-Fi / UDP
802.15.4-class links
LoRa-class links
BLE-class links
Ethernet
```

Routing must not assume a particular MAC address, Wi-Fi channel, RSSI,
broadcast mechanism, MTU, or acknowledgement mechanism.

## 22. Gateway / Root Role

The Gateway is a normal routing node with a distinguished ENP role.

The routing layer must not require a central root for ordinary node-to-node
routing. This preserves decentralized swarm operation.

External-network gateway behavior remains above the core mesh routing layer.

## 23. Failure Handling

Routing must handle at least:

```text
No route
Next-hop stale
Transport send failure
Route expiration
RREQ timeout
RREQ duplicate
RREP timeout
Route repair failure
TTL expiration
```

Retries and flooding must be bounded and deterministic.

## 24. Resource Policy

The existing static-resource policy remains mandatory.

Routing should use:

```text
fixed-size route table
static queues/tasks
bounded control buffers
bounded route-discovery state
```

No unbounded route cache is permitted.

Exact capacities will be selected after the wire protocol and control
behavior are specified.

## 25. Security Boundary

Security is outside the current routing implementation.

Future authentication must be able to cover RREQ, RREP, and RERR without
requiring redesign of the route table or forwarding architecture.

## 26. High-Level Route State Machine

```text
                 ┌─────────────┐
                 │  NO ROUTE   │
                 └──────┬──────┘
                        │ route needed
                        ▼
                 ┌─────────────┐
                 │ DISCOVERING │
                 └──────┬──────┘
                    RREP│
                        ▼
                 ┌─────────────┐
            ┌───►│    VALID    │◄───┐
            │    └──────┬──────┘    │
            │           │           │
       repair│      next-hop        │refresh
            │       failure         │
            │           ▼           │
            │    ┌─────────────┐    │
            └────│    STALE    │────┘
                 └──────┬──────┘
                        │ repair fails
                        ▼
                 ┌─────────────┐
                 │   INVALID   │
                 └─────────────┘
```

Exact transitions belong in the route-table and timer specification.

## 27. Implementation Boundary

This document does not authorize routing implementation yet.

Before coding, complete:

### Routing packet specification

- RREQ format;
- RREP format;
- RERR format;
- subtypes;
- control packet identity;
- control TTL;
- route freshness/sequence semantics.

### Route-table specification

- exact route entry;
- capacity;
- states;
- timers;
- lookup API;
- insertion/update;
- invalidation;
- metric comparison;
- tie-breaking.

### Forwarding specification

- exact TTL behavior;
- local destination;
- route lookup failure;
- local repair;
- send failure;
- route-error generation;
- control/data interaction.

## 28. Agreed Architectural Decisions

```text
Routing architecture       = HYBRID
Neighbor awareness         = PROACTIVE
Multi-hop discovery        = REACTIVE
Discovery mechanism        = TTL-LIMITED EXPANDING RING
Route cache                = YES
Local repair               = YES
Control packets            = RREQ / RREP / RERR
Packet family              = ENP_PACKET_ROUTE
Metric abstraction         = EXTENSIBLE
Initial metric             = HOP COUNT
Source address             = ORIGINATOR, NEVER REWRITTEN
Duplicate foundation       = EXISTING ENP MECHANISM
TTL                        = REQUIRED
Transport coupling         = NONE
Initial transport          = ESP-NOW
Future transports          = ARCHITECTURALLY SUPPORTED
```

## 29. Next Step

The next document is:

**ENP Routing Protocol Specification v0.2**

It will define the exact wire-level RREQ/RREP/RERR contract, field sizes,
byte order, sequence/freshness semantics, TTL rules, route discovery
timers, and control-packet duplicate semantics.

Only after that specification is reviewed should routing code be created.
