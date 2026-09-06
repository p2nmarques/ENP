# IG-G.1 — Production RERR Generation Implementation Audit

**Project:** ENP  
**Baseline:** `ENP-0.3.1(3).zip`  
**Status:** Audit complete — no source changes made  
**Scope:** Production RERR generation integration  
**Predecessor:** IG-F.7.8 — Multi-Hop Application Delivery, hardware validated  
**Next gate:** IG-G.2 — RERR Generation Boundary

---

## 1. Purpose

This document records the read-only implementation audit performed before
introducing production RERR generation.

The objective was to determine whether the current ENP production baseline
already provides the required protocol, routing, failure-detection, repair,
and transport primitives, and to identify the exact missing production
integration boundary.

No routing, discovery, transport, dispatcher, E5C, IG-D, or E5D source
behaviour was modified during this audit.

---

## 2. Baseline and Architectural Position

The current production routing runtime already owns and initializes the
relevant routing components:

- route table;
- route repair;
- E5D route-repair coordination;
- IG-D route-failure coalescing;
- E5C routing data path.

The existing E5C → IG-D failure callback remains the established production
failure boundary.

The audit therefore does **not** recommend creating a second failure path or
bypassing IG-D.

The existing architecture remains:

```text
E5C
 │
 │ authorized route failure
 ▼
IG-D failure coalescer
 │
 ▼
E5D route-repair coordinator
```

RERR generation is the missing production integration after an authorized
routing failure condition.

---

## 3. Existing RERR Protocol Capability

The protocol layer already defines the RERR wire format.

The existing RERR subtype is:

```text
ENP_ROUTING_SUBTYPE_RERR = 3
```

with a fixed 16-byte RERR wire representation containing:

- payload version;
- routing subtype;
- unreachable network ID;
- unreachable node ID;
- destination sequence number;
- route error reason;
- reserved fields.

The existing route-error reasons are:

```text
NO_ROUTE                  = 1
NEXT_HOP_UNREACHABLE      = 2
ROUTE_EXPIRED             = 3
LOCAL_REPAIR_FAILED       = 4
TTL_EXPIRED               = 5
```

### Finding

**RERR wire definition and reason codes already exist.**

No new wire-format definition is required for IG-G.

---

## 4. Existing RERR Processing Capability

The existing `enp_rerr_processor` provides the receive-side RERR processing
path, including validation/freshness handling and route invalidation.

It is not a RERR generator or transmitter.

### Finding

The project already has a receiving/processing implementation, but it does
not provide the production mechanism that constructs and transmits a new RERR
when a local routing failure requires one.

---

## 5. Route Table Capability

The route table already provides the authoritative routing state through:

```text
enp_route_table_lookup()
enp_route_table_lookup_const()
enp_route_table_insert()
enp_route_table_update()
enp_route_table_invalidate()
enp_route_table_remove()
```

A normal lookup returns an entry only when the entry is `ACTIVE`.

Route invalidation changes an existing route to `STALE`.

### Finding

The existing route table is sufficient as the authoritative source for
production RERR generation.

A separate AODV-style precursor route database is **not required** by the
audit.

---

## 6. E5C → IG-D Failure Path

The production baseline already connects E5C route failures to IG-D through
the established callback mechanism.

IG-D provides the bounded asynchronous boundary between routing failure
detection and repair processing.

The existing path is:

```text
E5C route failure
       │
       ▼
IG-D failure coalescer
       │
       ▼
E5D repair coordinator
```

### Finding

IG-G must integrate with the existing routing failure/repair architecture.

It should **not**:

- generate RERR synchronously from the ESP-NOW callback;
- bypass IG-D;
- create a second failure-event pipeline;
- replace E5D route-repair coordination.

---

## 7. Current Next-Hop Selection

The current production integration includes a conservative next-hop selection
policy.

The implementation rejects an alternate next hop when the destination itself
is the failed next hop.

For an admitted destination, the current policy requires:

- same network;
- destination is not the local node;
- an `ACTIVE` neighbour exists;
- a transport address can be resolved.

The current production implementation does not constitute a general
multi-hop alternate-route selection mechanism.

### Finding

IG-G should not expand the scope into general route selection.

RERR generation must use the routing evidence already available rather than
introducing a new route-selection subsystem.

---

## 8. Transport Resolution Capability

The existing production routing integration already resolves logical next-hop
addresses through the neighbour subsystem.

The admission path verifies:

1. network identity;
2. non-local destination;
3. neighbour existence;
4. neighbour `ACTIVE` state;
5. successful transport-address resolution.

The neighbour subsystem provides the existing logical-address to transport
address boundary.

### Finding

IG-G can reuse the existing transport-resolution and transport-submission
interfaces.

No new physical-address mapping mechanism is required.

---

## 9. Missing Production Capability

The audit identified the following existing capabilities:

| Capability | Status |
|---|---|
| RERR wire definition | Present |
| RERR reason codes | Present |
| RERR receive/processing | Present |
| Route invalidation | Present |
| Authoritative route table | Present |
| E5C failure detection | Present |
| IG-D failure coalescing | Present |
| E5D repair coordination | Present |
| Transport abstraction | Present |
| Logical → transport resolution | Present |
| Production RERR generation | **Missing** |
| Production RERR transmission | **Missing** |
| Dependent-recipient selection | **Missing** |

### Audit conclusion

The missing functionality is an **integration component**, not a missing
protocol primitive.

---

## 10. Required IG-G Production Boundary

The recommended production boundary is:

```text
                    E5C
                     │
                     │ route failure
                     ▼
                    IG-D
                     │
                     │ coalesced failure
                     ▼
                    E5D
                     │
                     │ authorized failure /
                     │ repair exhausted
                     ▼
              ┌──────────────┐
              │ IG-G RERR    │
              │ generation  │
              └──────┬───────┘
                     │
                     │ construct RERR
                     ▼
              existing routing /
              transport boundary
```

The important architectural rule is that an arbitrary transport error must
not by itself become an RERR.

RERR generation requires an authorized routing failure condition.

---

## 11. Areas Explicitly Frozen for IG-G

The following existing components should remain unchanged unless a later
implementation audit proves a specific interface gap:

```text
enp_routing.h wire definitions
enp_rerr_processor.c receive semantics
enp_route_table.c semantics
E5C failure detection
IG-D failure coalescing
E5D repair coordination
ESP-NOW transport implementation
IG-F.7.8 application service
Discovery → Routing direct-neighbour synchronization
```

In particular, the IG-F.7.8 validated baseline must remain intact.

---

## 12. IG-G.1 Findings

### Finding G1-01 — Protocol primitives exist

The RERR wire representation and reason codes are already implemented.

**Disposition:** Reuse.

### Finding G1-02 — Route state is authoritative

The route table provides sufficient route state for RERR generation.

**Disposition:** Reuse; do not introduce a parallel route database.

### Finding G1-03 — Failure events already have an established boundary

E5C feeds IG-D, which feeds E5D.

**Disposition:** Preserve this architecture.

### Finding G1-04 — RERR processing exists only on the receive side

`enp_rerr_processor` does not provide production RERR generation/transmission.

**Disposition:** Add the missing generation integration without changing
receive semantics.

### Finding G1-05 — Transport primitives are reusable

Existing logical-address and transport-address resolution can support RERR
transmission.

**Disposition:** Reuse existing interfaces.

### Finding G1-06 — Dependent-recipient selection is not implemented

There is currently no production component that determines which upstream
dependent recipients require an RERR.

**Disposition:** Define this explicitly in IG-G.2 before implementation.

### Finding G1-07 — No architectural blocker identified

All required lower-level primitives are present.

**Disposition:** Proceed to IG-G.2.

---

## 13. IG-G.1 Audit Result

**IG-G.1 — Production RERR Generation Implementation Audit: COMPLETE**

**Result: PASS**

The current `ENP-0.3.1(3)` baseline provides the necessary protocol, routing,
failure, repair, and transport primitives for a minimal production RERR
generation integration.

The missing functionality is isolated to the RERR generation/transmission
integration boundary and dependent-recipient selection.

No existing IG-F.7.8 or E5C production behaviour needs to be reverted or
reworked.

---

## 14. Next Gate — IG-G.2

Before modifying source code, IG-G.2 should define the exact production
RERR-generation boundary:

1. Which IG-D/E5D failure event authorizes RERR generation.
2. How the failure reason maps to `enp_route_error_reason_t`.
3. How the unreachable destination is identified.
4. How the destination sequence number is obtained.
5. How dependent recipient(s) are determined.
6. How RERR packets are constructed.
7. How generated RERR packets enter the existing transport path.
8. How duplicate generation and loops are prevented.
9. How RERR generation interacts with local repair and route invalidation.

Only after these interfaces are pinned down should the first production RERR
source patch be introduced.
