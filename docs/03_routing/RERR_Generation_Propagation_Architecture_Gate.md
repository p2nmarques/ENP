# ENP v0.2 — RERR Generation & Propagation Architecture Gate

**Gate ID:** `RERR-GP-ARCH-01`  
**Status:** **OPEN — ARCHITECTURE ONLY**  
**Production code changes:** **FORBIDDEN during this gate**  
**Baseline:** `ENP-0.2-r5 (2026-08-25)`

---

## 1. Purpose

Define and freeze the architecture for **RERR generation and propagation** before any production implementation is changed.

The gate exists because the current baseline has a validated RERR **reception/invalidation** path, while RERR generation, precursor handling, and propagation policy remain intentionally open.

This gate is therefore an **architecture gate**, not an implementation gate.

---

## 2. Frozen baseline

The following are already validated/frozen and must remain untouched during this gate:

| Area | Status / boundary |
|---|---|
| RERR wire format | **FROZEN** — canonical routing wire definition |
| RERR reason codes | **FROZEN** |
| R4-D | **FROZEN current receive/validate/freshness/invalidation responsibility** |
| Route table | **FROZEN ownership of persistent route state** |
| P4-E5B | **FROZEN** |
| P4-E5C | **FROZEN** |
| P4-E5D Step-3 | **FROZEN** |
| P4-E5E-I33 | **HARDWARE VALIDATED / FROZEN** |
| Transport | **FROZEN existing boundary** |
| Reliability | **FROZEN existing boundary** |

### Frozen RERR reason-code contract

```text
1 = NO_ROUTE
2 = NEXT_HOP_UNREACHABLE
3 = ROUTE_EXPIRED
4 = LOCAL_REPAIR_FAILED
5 = TTL_EXPIRED
```

Reason `0` is `UNKNOWN` and is not a valid accepted RERR reason.

---

## 3. Architectural principle

R4-D remains a **receive/process/invalidate** component.

It must not become a combined:

- RERR generator;
- RERR forwarder;
- route-maintenance state machine.

The architecture must preserve separation between:

```text
failure observation
        ↓
route-state management
        ↓
RERR generation
        ↓
RERR propagation
        ↓
RERR reception
        ↓
R4-D processing
        ↓
route invalidation
```

The exact ownership of each stage is what this gate must establish.

---

## 4. Gate scope

This architecture gate covers:

1. Failure event → RERR generation ownership.
2. RERR generation triggers.
3. Precursor/dependent-route model.
4. RERR destination selection.
5. RERR propagation model.
6. RERR forwarding behavior.
7. Sequence-number semantics.
8. Duplicate suppression.
9. Loop prevention.
10. Multiple destinations affected by one failed next hop.
11. Interaction with E5C route invalidation.
12. Interaction with E5D repair and existing R4 discovery.
13. Transport integration boundary.
14. Reliability integration boundary.
15. Acceptance criteria for the subsequent implementation gate.

---

# 5. Architectural decisions required

## GP-A — RERR generator ownership

Determine which routing/integration component is allowed to generate an RERR.

### Preferred starting hypothesis

RERR generation should belong to a **routing maintenance/integration responsibility**, not to R4-D.

Conceptually:

```text
Failure observation
        ↓
Routing maintenance / integration
        ↓
Route state decision
        ↓
RERR generation
```

This is a proposal to evaluate during the gate, **not yet a frozen implementation decision**.

---

## GP-B — Failure trigger

Define exactly which events can request RERR generation.

Candidates may include:

```text
transport failure
route expiration
next-hop disappearance
local repair failure
other protocol-defined routing failure
```

Each supported trigger must map explicitly to one of the frozen RERR reason codes.

The architecture must distinguish:

> **failure observation**

from:

> **RERR generation**

A transport error must not automatically become an RERR unless the routing architecture explicitly authorizes that transition.

---

## GP-C — Precursor / dependent-route model

Define how a node determines which upstream nodes depend upon a route or failed next hop.

Example:

```text
A → B → C
    ↑
  failed
```

If B loses its route to C, B needs a defined mechanism for determining whether A must be informed.

The gate must answer:

- What is a precursor?
- Where is precursor information stored?
- Who maintains it?
- When is it added?
- When is it removed?
- What happens when precursor information is unavailable?
- Can multiple precursors exist?
- Can one failed next hop affect multiple destinations?

R4-D must not invent a precursor model implicitly.

---

## GP-D — RERR propagation model

Define whether generated RERRs are:

```text
unicast
multicast
broadcast
selective multi-unicast
```

or use another explicitly approved mechanism.

The architecture must also define whether an intermediate node:

```text
receives RERR
     ↓
invalidates route
     ↓
generates a new RERR
```

or:

```text
receives RERR
     ↓
forwards received RERR
```

or uses another controlled mechanism.

No forwarding behavior should be inferred from the current R4-D processor.

---

## GP-E — Destination sequence semantics

Define exactly which destination sequence number is placed into a generated RERR.

This must remain compatible with the existing R4-D freshness rules, including serial-number comparison and wrap-around behavior.

The RERR architecture must not introduce a second or conflicting sequence-number system.

---

## GP-F — Duplicate and loop control

Define how duplicate RERRs and propagation loops are prevented.

The design must cover:

- duplicate RERR reception;
- repeated generation;
- simultaneous failures;
- multiple upstream precursors;
- forwarding loops;
- repeated RERRs for the same destination.

The mechanism must not introduce an unrelated duplicate-cache architecture.

---

## GP-G — Multiple destinations

Explicitly define behavior when one failed next hop affects multiple destinations.

Example:

```text
             → C
A → B
             → D
             → E
```

A single failed B→next-hop relationship may invalidate several routes.

The architecture must define whether:

- one RERR represents multiple destinations;
- one RERR is generated per destination;
- destinations are aggregated;
- propagation is performed separately for each affected destination.

The current RERR wire format must not be changed during this gate merely to support a preferred model.

---

## GP-H — E5C interaction

Define the exact boundary between route invalidation and RERR generation.

For example, the architecture could establish:

```text
failure observation
        ↓
E5C route invalidation
        ↓
RERR generation
```

or:

```text
failure observation
        ↓
routing maintenance decision
        ├── route invalidation
        └── RERR generation
```

The decision must preserve the frozen E5C semantics.

No change to P4-E5C is authorized by this architecture gate.

---

## GP-I — E5D interaction

The architecture must preserve the existing E5D relationship:

```text
STALE
  ↓
repair / discovery
  ↓
ACTIVE
```

E5D decides **when repair is needed**.

Existing R4 decides **how route discovery is performed**.

E5D must not create a parallel RERR subsystem or redefine the RERR wire format.

The existing frozen E5D Step-3 and I33 behavior remains authoritative.

---

## GP-J — Transport boundary

Define how an approved RERR becomes a transport transmission.

The design must preserve the existing routing/transport boundary and must not put complex routing state-machine behavior into the ESP-NOW RX callback.

Conceptually:

```text
Routing task / routing integration
        ↓
RERR generation
        ↓
transport submission
        ↓
ESP-NOW
```

The exact API and implementation are outside this architecture-only gate.

---

# 6. Proposed conceptual boundary

The following is a proposal to evaluate, not yet a frozen implementation design:

```text
Failure observation
        ↓
Routing maintenance / integration
        ↓
Route state decision
        ↓
RERR generation service
        ↓
RERR propagation / transport integration
        ↓
Remote RERR reception
        ↓
R4-D validation / freshness / invalidation
        ↓
Route table
```

The final architecture may modify this structure if the review establishes a better ownership boundary.

---

# 7. Explicit non-goals

This gate does **not** authorize:

- modification of P4-E5B;
- modification of P4-E5C;
- modification of P4-E5D Step-3;
- modification of P4-E5E-I33;
- modification of the frozen RERR wire format;
- modification of the frozen RERR reason-code values;
- redesign of R4-D;
- modification of Reliability transaction semantics;
- modification of Reliability retry semantics;
- production RERR generation code;
- production RERR propagation code;
- hardware validation.

---

# 8. Acceptance criteria

The architecture gate can PASS only when all of the following are explicitly defined:

1. A single documented owner exists for RERR generation.
2. Every supported generation trigger maps to one frozen RERR reason.
3. Failure observation is clearly separated from RERR generation.
4. Precursor/dependent-route semantics are defined.
5. RERR destination selection is defined.
6. Propagation and forwarding rules are explicit.
7. Duplicate suppression is explicit.
8. Loop prevention is explicit.
9. Sequence-number semantics are compatible with R4-D.
10. Multiple destinations sharing a failed next hop are covered.
11. E5C integration is explicitly defined without silently changing E5C.
12. E5D integration preserves the frozen repair architecture.
13. R4-D remains receive/process/invalidate only.
14. Transport ownership remains unchanged.
15. Reliability ownership remains unchanged.
16. A subsequent implementation gate can be derived directly from the approved architecture.

---

# 9. Gate exit states

## PASS

All architectural decisions are explicit, internally consistent, documented, and approved.

The **RERR implementation gate may then be opened**.

## HOLD

One or more architectural decisions remain unresolved.

Production implementation remains frozen.

## REJECT

The proposed architecture conflicts with the frozen protocol, routing, E5C/E5D, transport, or Reliability ownership boundaries, or requires reopening a frozen gate without explicit approval.

---

# 10. Current status

**RERR-GP-ARCH-01: OPEN — ARCHITECTURE ONLY**

No production implementation is authorized by this document.

The first decision to evaluate is:

> **GP-A — Who owns RERR generation?**

### Preferred starting hypothesis

```text
RERR generation
        ↓
Routing maintenance / integration
        ↓
NOT R4-D
```

This remains a proposal until explicitly reviewed and accepted.

---

## 11. Freeze boundary

Until this gate reaches PASS:

```text
P4-E5B          FROZEN
P4-E5C          FROZEN
P4-E5D Step-3   FROZEN
P4-E5E-I33      HARDWARE VALIDATED / FROZEN

RERR wire       FROZEN
RERR reasons    FROZEN
R4-D            FROZEN

RERR generation       NOT OPEN
RERR propagation      NOT OPEN
```

**Document classification:** Current architecture workstream / not yet frozen.
