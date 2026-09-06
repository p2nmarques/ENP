# IG-G.3 — RERR Generation Service Interface

**Project:** ENP  
**Baseline:** `ENP-0.3.1(3).zip`  
**Status:** COMPLETE — Interface Defined / Ready for Implementation  
**Predecessor:** IG-G.2 — RERR Generation Boundary  
**Next gate:** IG-G.4 — RERR Packet Construction & Generation Implementation

---

## 1. Purpose

IG-G.3 defines the concrete C-level service boundary for production RERR
generation.

The purpose is to turn the architectural boundary established by IG-G.2 into
a small, explicit interface without changing existing E5C, IG-D, E5D, route
table, transport, discovery, or IG-F.7.8 behaviour.

**No production source code is modified by IG-G.3.**

---

# 2. Current Baseline Interfaces

The current baseline already exposes the following relevant interfaces.

## 2.1 E5C route-failure notification

`enp_routing_data_path` exposes:

```c
typedef void (*enp_routing_route_failure_fn)(
    void *context,
    enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop);
```

and:

```c
bool enp_routing_data_path_set_route_failure_callback(
    enp_routing_data_path_t *path,
    enp_routing_route_failure_fn callback,
    void *context);
```

The callback is a notification boundary associated with an ACTIVE route
transitioning to STALE after a transport failure.

**Important:** this callback executes in the transport send-result callback
context. It is therefore not an appropriate place to generate/transmit RERR
directly.

---

## 2.2 IG-D failure event

The current IG-D event contains:

```c
typedef struct {
    enp_route_destination_t destination;
    enp_route_destination_t failed_next_hop;
    enp_route_failure_event_state_t state;
} enp_route_failure_event_t;
```

The coalescing identity is exactly:

```text
(destination, failed_next_hop)
```

The public observation API is:

```c
bool enp_route_failure_coalescer_observe(
    enp_route_failure_coalescer_t *coalescer,
    enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop);
```

IG-D then hands the event to E5D through:

```c
enp_route_repair_request_ex(
    repair,
    event.destination,
    event.failed_next_hop);
```

The current IG-D event does **not** contain:

- an RERR reason;
- a destination sequence number;
- a dependent recipient;
- an authorization flag;
- an E5D terminal outcome.

This is a critical interface observation.

---

## 2.3 E5D repair request

The current E5D request is:

```c
typedef struct {
    enp_route_destination_t destination;
    enp_route_destination_t failed_next_hop;
} enp_route_repair_request_t;
```

The current consume callback is:

```c
typedef void (*enp_route_repair_consume_fn)(
    const enp_route_repair_request_t *request,
    void *context);
```

The current callback represents repair-request consumption, not a terminal
repair-success/failure result.

Therefore IG-G must **not** infer `LOCAL_REPAIR_FAILED` merely because an E5D
request was consumed.

---

# 3. Key IG-G.3 Finding

## Finding G3-01 — Existing failure event is insufficient for RERR generation

The current E5C → IG-D event contains only:

```text
destination
failed_next_hop
```

It does not contain the information required to authorize and construct a
production RERR.

Therefore the IG-G service must receive an explicit generation request at the
point where routing has sufficient evidence to authorize RERR generation.

### Decision

**Do not overload the existing IG-D event with speculative RERR semantics.**

The existing IG-D interface remains unchanged for the first IG-G
implementation.

---

# 4. Proposed IG-G Service Interface

The proposed service interface is intentionally small.

## 4.1 Generation request

```c
typedef struct {
    enp_route_destination_t unreachable;
    uint32_t destination_sequence;
    enp_route_error_reason_t reason;
} enp_rerr_generation_request_t;
```

The request identifies the information intrinsic to the RERR itself:

```text
unreachable destination
destination sequence number
authorized reason
```

The service does not accept an arbitrary transport error as a request.

---

## 4.2 Recipient submission callback

Recipient selection is deliberately separated from RERR packet construction.

```c
typedef bool (*enp_rerr_recipient_fn)(
    void *context,
    enp_route_destination_t recipient);
```

The generation service may invoke the recipient callback once for each
explicitly identified dependent recipient.

The callback is a routing-integration boundary, not a broadcast mechanism.

---

## 4.3 Transport submission callback

The RERR service should not call `esp_now_send()` directly.

The proposed integration boundary is:

```c
typedef esp_err_t (*enp_rerr_submit_fn)(
    void *context,
    enp_route_destination_t recipient,
    const enp_packet_t *packet);
```

This keeps physical-address resolution and transport ownership outside the
RERR generator.

The existing ENP transport path remains authoritative.

---

## 4.4 Service state

A bounded static service object is recommended:

```c
typedef struct {
    bool initialized;

    enp_rerr_submit_fn submit;
    void *submit_context;

    enp_rerr_recipient_fn recipient;
    void *recipient_context;

    uint32_t generated_count;
    uint32_t submitted_count;
    uint32_t rejected_count;
    uint32_t submit_failure_count;
} enp_rerr_generation_t;
```

No dynamic allocation is required.

The service is intended to be owned by the routing/runtime integration layer.

---

# 5. Proposed Public API

The production service should expose the following conceptual API:

```c
bool enp_rerr_generation_init(
    enp_rerr_generation_t *generation,
    enp_rerr_submit_fn submit,
    void *submit_context,
    enp_rerr_recipient_fn recipient,
    void *recipient_context);
```

and:

```c
esp_err_t enp_rerr_generation_submit(
    enp_rerr_generation_t *generation,
    const enp_rerr_generation_request_t *request);
```

The API name is intentionally a proposal for IG-G.4 implementation; it is not
being added to the source tree during IG-G.3.

---

# 6. Service Responsibilities

The IG-G generation service owns:

1. validation of the generation request;
2. validation of the frozen RERR reason;
3. construction of the canonical RERR payload;
4. construction of the ENP routing packet;
5. enumeration of explicitly identified dependent recipients;
6. submission through the supplied transport callback;
7. bounded counters/diagnostics.

The service does **not** own:

```text
route table
route invalidation
route repair
discovery
neighbour state
ESP-NOW
RERR reception
```

---

# 7. Authorized Generation Boundary

The service must distinguish:

```text
failure observation
        ≠
RERR authorization
```

A transport failure can produce:

```text
E5C route invalidation
        ↓
IG-D failure event
        ↓
E5D repair
```

but that does not automatically mean:

```text
failure → RERR
```

The generation API therefore represents an already-authorized routing decision.

This preserves the frozen GP-B rule.

---

# 8. Reason Mapping

The generation request uses the frozen production reason contract:

```text
1  NO_ROUTE
2  NEXT_HOP_UNREACHABLE
3  ROUTE_EXPIRED
4  LOCAL_REPAIR_FAILED
5  TTL_EXPIRED
```

`UNKNOWN = 0` must be rejected.

The service must not reinterpret transport-layer error codes as RERR reasons.

---

# 9. Destination Sequence Number

The service does not maintain its own RERR sequence-number state.

The caller supplies the destination sequence number obtained from the
authoritative existing routing/discovery semantics.

Conceptually:

```text
routing evidence
      ↓
destination sequence
      ↓
enp_rerr_generation_request_t
```

This preserves GP-E.

---

# 10. Dependent-Recipient Selection

Recipient selection is intentionally outside the RERR wire-format builder.

The routing integration layer determines:

```text
affected destination
        ↓
actual routing evidence
        ↓
dependent recipient(s)
```

and supplies those recipients through the recipient boundary.

No persistent AODV-style precursor table is introduced.

If the required dependency evidence is unavailable:

```text
do not invent recipient
do not broadcast RERR
do not blindly forward RERR
```

This preserves GP-C and GP-D.

---

# 11. One Destination / One RERR Semantics

The service request represents one unreachable destination.

Therefore:

```text
affected destination A
    + recipient X
        → RERR(A) → X

affected destination A
    + recipient Y
        → RERR(A) → Y
```

If multiple destinations are independently affected, each is represented by a
separate generation request.

The canonical RERR wire format is not expanded.

---

# 12. Transport Boundary

The service must submit through an abstract callback:

```text
IG-G
  ↓
enp_rerr_submit_fn
  ↓
existing ENP transport integration
  ↓
logical → transport address resolution
  ↓
ESP-NOW
```

There must be no:

```text
IG-G → esp_now_send()
```

direct path.

This preserves transport ownership and the validated IG-F.7.8 transport
baseline.

---

# 13. Task-Context Requirement

RERR generation executes in routing/task context.

It must not execute from:

```text
ESP-NOW RX callback
ESP-NOW send-result callback
```

The existing E5C route-failure callback is therefore only an observation/
handoff boundary.

If an implementation needs to bridge from callback context to the RERR
service, it must use an existing or newly defined bounded task/queue boundary,
not synchronous RERR generation from the callback.

---

# 14. Duplicate and Loop Control

The service does not introduce an independent routing duplicate cache.

Duplicate/loop control comes from:

1. authorized generation;
2. selective recipients;
3. no blind forwarding;
4. existing routing identity/sequence semantics;
5. R4-D freshness handling for received RERRs.

A failed RERR submission must not recursively generate another RERR.

---

# 15. Relationship to E5D

A significant interface constraint is established here:

```text
E5D request consumed
        ≠
LOCAL_REPAIR_FAILED
```

The current E5D API exposes request admission and consumption, but no terminal
repair-result callback in the inspected baseline.

Therefore a future `LOCAL_REPAIR_FAILED` trigger must come from an explicit
terminal E5D routing outcome or equivalent authorized routing-maintenance
evidence.

IG-G.3 does **not** modify E5D to manufacture such an outcome.

---

# 16. Recommended Implementation Location

The service should live under the routing integration layer, for example:

```text
core/routing/
    enp_rerr_generation.h
    enp_rerr_generation.c
```

The exact filename remains an implementation decision for IG-G.4.

It should be owned/initialized by the existing routing runtime rather than by
the transport component or `enp_rerr_processor`.

---

# 17. IG-G.3 Interface Contract

The following contract is established:

```text
                    authorized
                  routing decision
                         │
                         ▼
          enp_rerr_generation_request_t
                         │
                         ├── unreachable
                         ├── destination_sequence
                         └── reason
                         │
                         ▼
                IG-G generation service
                         │
                         ├── build canonical RERR
                         │
                         ▼
                 dependent recipients
                         │
                         ▼
                enp_rerr_submit_fn
                         │
                         ▼
                existing ENP transport
```

---

# 18. Explicit Non-Responsibilities

IG-G generation does not:

- invalidate routes;
- repair routes;
- discover routes;
- maintain neighbours;
- maintain precursor tables;
- forward received RERRs;
- interpret arbitrary ESP-NOW errors;
- modify R4-D;
- bypass IG-D;
- bypass E5D;
- call `esp_now_send()` directly;
- change the IG-F.7.8 application path.

---

# 19. IG-G.3 Findings

### G3-01 — Existing IG-D event is too small

The event has destination and failed next hop only.

**Disposition:** Preserve IG-D; introduce an explicit authorized-generation
request boundary.

### G3-02 — E5D consume is not a repair-result interface

Consumption of a repair request does not prove repair failure.

**Disposition:** Do not map E5D consumption to `LOCAL_REPAIR_FAILED`.

### G3-03 — RERR intrinsic data should be explicit

Destination, sequence, and reason should be carried by a generation request.

**Disposition:** Define `enp_rerr_generation_request_t`.

### G3-04 — Recipient selection must remain routing-owned

The generator should not invent dependency state.

**Disposition:** Use an explicit recipient boundary backed by existing routing
evidence.

### G3-05 — Transport must remain abstract

The RERR service should not know ESP-NOW internals.

**Disposition:** Submit through an abstract callback into existing transport.

### G3-06 — Static bounded implementation is sufficient

The current architecture does not require dynamic allocation or a persistent
RERR state database.

**Disposition:** Use bounded static state and counters.

### G3-07 — Generation remains task-context only

The E5C callback executes in transport send-result context.

**Disposition:** Treat it as notification/handoff only.

---

# 20. IG-G.3 Acceptance Criteria

- [x] Concrete RERR generation request defined.
- [x] Destination field defined.
- [x] Destination sequence field defined.
- [x] Frozen reason field defined.
- [x] Recipient-selection boundary defined.
- [x] Transport-submission boundary defined.
- [x] Generator ownership defined.
- [x] Task-context requirement defined.
- [x] E5C relationship preserved.
- [x] IG-D relationship preserved.
- [x] E5D relationship preserved.
- [x] R4-D receive semantics preserved.
- [x] No precursor table introduced.
- [x] No direct ESP-NOW dependency introduced.
- [x] IG-F.7.8 baseline preserved.
- [x] No production source changes made.

---

# 21. IG-G.3 Result

## **PASS — RERR Generation Service Interface Defined**

IG-G.3 establishes the C-level service boundary required for the first
production RERR-generation implementation.

The key implementation rule is:

> **The RERR generator consumes an already-authorized routing-generation
> request; it does not decide that an arbitrary transport failure is an RERR.**

The interface also preserves the existing separation of:

```text
E5C          route failure / invalidation
IG-D         failure-event coalescing
E5D          route repair
IG-G         RERR generation / propagation decision
Transport    physical transmission
R4-D         incoming RERR processing
```

No production source code was changed.

---

# 22. Next Gate — IG-G.4

The next controlled step is:

## **IG-G.4 — RERR Packet Construction & Generation Implementation**

IG-G.4 should implement the minimum service required by this interface:

1. add the RERR generation header/source;
2. validate the generation request;
3. build the existing canonical 16-byte RERR payload;
4. construct the ENP routing packet;
5. select/consume explicitly supplied dependent recipients;
6. submit through the existing transport boundary;
7. add bounded diagnostics;
8. compile-test against the current `ENP-0.3.1(3)` baseline.

The implementation must not modify the validated IG-F.7.8 path, E5C
invalidation, IG-D coalescing semantics, E5D repair semantics, R4-D receive
processing, or ESP-NOW transport ownership.
