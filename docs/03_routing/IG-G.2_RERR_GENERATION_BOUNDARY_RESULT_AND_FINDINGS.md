# IG-G.2 — RERR Generation Boundary
## Result and Findings

**Project:** ENP  
**Baseline:** `ENP-0.3.1(3).zip`  
**Status:** COMPLETE — Boundary Defined / Ready for Implementation  
**Scope:** Production RERR generation integration  
**Predecessor:** IG-G.1 — Production RERR Generation Implementation Audit  
**Next gate:** IG-G.3 — RERR Generation Service Interface

---

## 1. Purpose

IG-G.2 defines the production boundary for RERR generation using the
architecture and interfaces already established in the current ENP baseline.

The objective is to determine:

- where RERR generation belongs;
- what authorizes generation;
- how it relates to E5C, IG-D, and E5D;
- how the affected destination and sequence information are obtained;
- how dependent recipients are selected;
- how RERR packets enter the existing transport path;
- and which existing components must remain unchanged.

**No production source code was modified during IG-G.2.**

---

# 2. Defined Production Boundary

The approved production flow is:

```text
                 E5C
                  │
                  │ normalized route failure
                  ▼
                 IG-D
                  │
                  │ bounded/coalesced failure event
                  ▼
                 E5D
                  │
                  │ repair outcome /
                  │ authorized failure
                  ▼
        ┌─────────────────────┐
        │ IG-G RERR POLICY    │
        │ / GENERATION        │
        └──────────┬──────────┘
                   │
          affected destination
          + sequence + reason
                   │
                   ▼
        ┌─────────────────────┐
        │ RERR PACKET BUILD   │
        └──────────┬──────────┘
                   │
                   ▼
        dependent recipient(s)
                   │
                   ▼
        existing ENP transport
                   │
                   ▼
                 ESP-NOW
```

This boundary keeps RERR generation inside routing/maintenance integration
and prevents the transport callback path from becoming a routing-control
plane.

---

# 3. Finding G2-01 — Generation Ownership

### Finding

RERR generation belongs to the **routing maintenance/integration layer**.

It must not be implemented as part of the existing RERR receive processor
or as a transport callback action.

### Decision

**ACCEPTED**

The generation function executes in routing/task context.

### Consequence

The existing `enp_rerr_processor` remains receive-side processing only.

---

# 4. Finding G2-02 — Authorized Trigger

### Finding

A raw ESP-NOW or transport failure is not sufficient to generate an RERR.

RERR generation requires an authorized routing failure condition.

Examples of authorized routing reasons include:

```text
NO_ROUTE
NEXT_HOP_UNREACHABLE
ROUTE_EXPIRED
LOCAL_REPAIR_FAILED
TTL_EXPIRED
```

### Decision

**ACCEPTED**

The transport layer remains a source of evidence for routing failure but does
not independently decide to emit an RERR.

### Consequence

The IG-G implementation must consume a normalized routing failure/repair
outcome rather than directly converting `esp_now_send()` errors into RERRs.

---

# 5. Finding G2-03 — E5C Relationship

### Finding

E5C remains responsible for the route-failure/invalidation side of the
existing data path.

The established relationship remains:

```text
failure
  ↓
E5C invalidation
  ↓
route becomes STALE
  ↓
failure event / RERR policy
```

### Decision

**PRESERVE**

IG-G must not introduce a second route-invalidation mechanism.

### Consequence

RERR generation observes the authoritative routing state rather than
duplicating E5C invalidation logic.

---

# 6. Finding G2-04 — IG-D Relationship

### Finding

IG-D is the existing bounded asynchronous failure-coalescing boundary
between routing failure detection and repair processing.

The production architecture is:

```text
E5C
 ↓
IG-D
 ↓
E5D
```

### Decision

**PRESERVE**

IG-G must integrate with the existing failure/repair path and must not bypass
IG-D with a second direct E5C → RERR path.

### Consequence

The implementation must identify the precise failure event/outcome that is
safe and semantically appropriate for RERR generation.

---

# 7. Finding G2-05 — E5D Relationship

### Finding

E5D remains responsible for route repair.

A terminal repair failure can provide the evidence required for a
`LOCAL_REPAIR_FAILED` RERR.

### Decision

**PRESERVE**

RERR generation follows the repair decision; it does not replace E5D.

### Consequence

IG-G must not create a competing repair mechanism or alter the existing
repair state machine.

---

# 8. Finding G2-06 — Destination and Sequence Information

### Finding

The RERR must identify the affected unreachable destination and carry the
appropriate destination sequence information.

Existing routing state and established ENP sequence semantics are the
authoritative sources.

### Decision

**REUSE EXISTING ROUTING EVIDENCE**

No second RERR-specific sequence-number system is introduced.

### Consequence

IG-G.3 must identify the exact existing structure/API from which the
destination and sequence number are obtained.

---

# 9. Finding G2-07 — Dependent-Recipient Selection

### Finding

Production RERR generation requires identifying which upstream/dependent
recipient(s) need to be informed about the unreachable destination.

The current baseline does not provide a completed production dependent-
recipient selection component.

### Decision

**DEFINE BEFORE IMPLEMENTATION**

Recipient selection must use actual routing evidence available at generation
time.

### Explicit architectural constraint

Do **not** introduce a persistent AODV-style precursor table merely for RERR
generation.

### Consequence

IG-G.3 must define the concrete routing evidence and lookup operation used to
identify recipients.

---

# 10. Finding G2-08 — Propagation Model

### Finding

RERR propagation is selective.

The approved model is:

```text
one affected destination
        ↓
identify dependent recipient(s)
        ↓
selective unicast / multi-unicast
```

RERRs are not broadcast.

### Decision

**ACCEPTED**

### Consequence

The implementation must submit RERRs only to explicitly identified
recipients.

---

# 11. Finding G2-09 — One Destination per RERR

### Finding

The current RERR wire representation describes one unreachable destination.

Therefore multiple affected destinations require separate RERR generation
instances.

### Decision

**PRESERVE EXISTING WIRE SEMANTICS**

### Consequence

IG-G.3 should model generation around an individual affected destination and
recipient relationship rather than inventing a new aggregate wire format.

---

# 12. Finding G2-10 — Transport Integration

### Finding

RERR packets should use the existing ENP packet and transport submission
path.

The intended flow is:

```text
routing task
    ↓
RERR generation
    ↓
ENP packet construction
    ↓
existing transport submission
    ↓
ESP-NOW
```

### Decision

**REUSE EXISTING TRANSPORT**

### Consequence

No direct IG-G → `esp_now_send()` path should be introduced.

This preserves the existing transport abstraction and the validated IG-F.7.8
transport behaviour.

---

# 13. Finding G2-11 — RERR Receive Processing Remains Separate

### Finding

The existing RERR processor is responsible for receiving and processing
incoming RERRs.

It must not become the owner of locally generated RERR policy.

### Decision

**KEEP SEPARATE**

The architecture therefore has two distinct responsibilities:

```text
LOCAL FAILURE
     ↓
IG-G generation
     ↓
outgoing RERR
```

and:

```text
incoming RERR
     ↓
enp_rerr_processor
     ↓
route invalidation / processing
```

---

# 14. Finding G2-12 — Reason-Code Contract

The production RERR reasons are:

```text
NO_ROUTE                  = 1
NEXT_HOP_UNREACHABLE      = 2
ROUTE_EXPIRED             = 3
LOCAL_REPAIR_FAILED       = 4
TTL_EXPIRED               = 5
```

`UNKNOWN = 0` is not an accepted production RERR reason.

### Decision

**USE THE FROZEN CURRENT CONTRACT**

The implementation must not silently redefine the enum or wire semantics.

Any historical discrepancy found in older documentation or tests must be
handled explicitly rather than changing the current production contract as
an incidental part of IG-G.

---

# 15. Components Explicitly Preserved

IG-G.2 establishes that the following components remain outside the RERR
generation implementation unless a later interface audit identifies a
specific dependency:

```text
enp_routing.h wire definitions
enp_rerr_processor.c receive semantics
enp_route_table.c semantics
E5C failure detection/invalidation
IG-D failure coalescing
E5D repair coordination
ESP-NOW transport implementation
IG-F.7.8 application service
Discovery → Routing direct-neighbour synchronization
```

The validated IG-F.7.8 baseline therefore remains intact.

---

# 16. Resulting Architecture

The resulting production architecture is:

```text
                 ┌──────────────────────┐
                 │      Data Path       │
                 │        E5C           │
                 └──────────┬───────────┘
                            │
                            │ failure
                            ▼
                 ┌──────────────────────┐
                 │         IG-D         │
                 │ failure coalescer    │
                 └──────────┬───────────┘
                            │
                            ▼
                 ┌──────────────────────┐
                 │         E5D          │
                 │ route repair         │
                 └──────────┬───────────┘
                            │
                 authorized failure /
                 repair outcome
                            │
                            ▼
                 ┌──────────────────────┐
                 │         IG-G         │
                 │ RERR policy +        │
                 │ generation           │
                 └──────────┬───────────┘
                            │
                            ▼
                 ┌──────────────────────┐
                 │ Existing ENP packet  │
                 │ + transport path     │
                 └──────────┬───────────┘
                            │
                            ▼
                          ESP-NOW
```

Incoming RERR processing remains independent:

```text
ESP-NOW
   ↓
ENP routing packet
   ↓
enp_rerr_processor
   ↓
validation / freshness
   ↓
route invalidation / processing
```

---

# 17. IG-G.2 Acceptance Criteria

IG-G.2 is considered complete when the following are defined:

- [x] RERR generation owner identified.
- [x] Generation task context identified.
- [x] Authorized trigger principle defined.
- [x] E5C relationship preserved.
- [x] IG-D relationship preserved.
- [x] E5D relationship preserved.
- [x] Destination/sequence source constrained to existing routing evidence.
- [x] Dependent-recipient selection identified as a required implementation
      boundary.
- [x] Selective unicast/multi-unicast propagation defined.
- [x] Existing transport path selected.
- [x] Incoming RERR processing kept separate.
- [x] Existing IG-F.7.8 baseline explicitly preserved.
- [x] No production source changes made.

---

# 18. IG-G.2 Result

## **PASS — RERR Generation Boundary Defined**

IG-G.2 establishes a concrete production integration boundary without
requiring changes to the existing routing, discovery, transport, dispatcher,
E5C, IG-D, or E5D foundations.

The remaining implementation work is localized to:

1. the RERR generation service interface;
2. authorized failure-to-RERR reason mapping;
3. affected-destination/sequence acquisition;
4. dependent-recipient selection;
5. RERR packet construction;
6. submission through the existing ENP transport path.

---

# 19. Next Gate — IG-G.3

The next step is:

## **IG-G.3 — RERR Generation Service Interface**

IG-G.3 should define the concrete C-level interface before implementation.

It should establish:

```text
failure event
     ↓
RERR generation request
     ↓
affected destination
     ↓
sequence number
     ↓
reason
     ↓
dependent recipient(s)
     ↓
RERR packet
     ↓
existing transport submission
```

The implementation should remain minimal and should not reopen any frozen
routing or transport architecture.

**IG-G.2 is therefore complete and ready for IG-G.3.**
