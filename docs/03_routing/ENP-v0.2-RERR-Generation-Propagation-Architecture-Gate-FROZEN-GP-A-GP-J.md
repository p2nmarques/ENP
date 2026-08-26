# ENP v0.2 --- RERR Generation & Propagation Architecture Gate

**Gate ID:** `RERR-GP-ARCH-01`\
**Status:** **FROZEN --- PASS**\
**Baseline:** `ENP-0.2-r5 (2026-08-25)`\
**Freeze scope:** **GP-A through GP-J**

## 1. Purpose

This document freezes the approved architecture for RERR generation and
propagation before production implementation.

The baseline already contains a validated RERR reception / validation /
freshness / invalidation path. This gate establishes the architecture
for RERR generation, dependency evidence, propagation, and transport
integration.

This is an architecture freeze, not production implementation
authorization.

## 2. Frozen baseline

The following remain validated/frozen and are not reopened by this gate:

  Area                Status
  ------------------- --------------------------------------------------------------
  RERR wire format    **FROZEN**
  RERR reason codes   **FROZEN**
  R4-D                **FROZEN --- receive / validate / freshness / invalidation**
  Route table         **FROZEN**
  P4-E5B              **FROZEN**
  P4-E5C              **FROZEN**
  P4-E5D Step-3       **FROZEN**
  P4-E5E-I33          **HARDWARE VALIDATED / FROZEN**
  Transport           **FROZEN existing boundary**
  Reliability         **FROZEN existing boundary**

### Frozen RERR reason-code contract

``` text
1 = NO_ROUTE
2 = NEXT_HOP_UNREACHABLE
3 = ROUTE_EXPIRED
4 = LOCAL_REPAIR_FAILED
5 = TTL_EXPIRED
```

Reason `0` is `UNKNOWN` and is not a valid accepted RERR reason.

## 3. Frozen architectural principle

R4-D remains a **receive/process/invalidate** component. It does not own
RERR generation, RERR forwarding, route-maintenance state, precursor
management, or transport submission.

``` text
failure observation
        ↓
routing maintenance / integration
        ↓
route-state decision
        ↓
RERR generation
        ↓
RERR propagation
        ↓
transport integration
        ↓
remote RERR reception
        ↓
R4-D validation / freshness / invalidation
        ↓
route table
```

# 4. Frozen GP-A ... GP-J decisions

## GP-A --- RERR generator ownership

**Decision: PASS --- FROZEN**

RERR generation belongs to the **routing maintenance / integration
responsibility**, not R4-D.

Generation executes in a safe routing/task context and is not performed
from the ESP-NOW callback.

## GP-B --- Failure trigger

**Decision: PASS --- FROZEN**

The architecture distinguishes **failure observation** from **RERR
generation**.

A transport failure is not automatically an RERR. Generation requires an
authorized routing failure condition and maps to the frozen reason
contract:

  -----------------------------------------------------------------------
  Reason                              Architectural meaning
  ----------------------------------- -----------------------------------
  `NO_ROUTE`                          Required route is absent under an
                                      authorized RERR-generation
                                      condition.

  `NEXT_HOP_UNREACHABLE`              Routing-relevant next-hop failure
                                      has been established and authorized
                                      for RERR generation.

  `ROUTE_EXPIRED`                     Defined route-expiry condition
                                      authorizes notification.

  `LOCAL_REPAIR_FAILED`               Existing E5D repair reaches its
                                      defined terminal failure condition
                                      and RERR generation is authorized.

  `TTL_EXPIRED`                       Routing TTL termination condition
                                      authorizes the corresponding RERR
                                      behavior.
  -----------------------------------------------------------------------

The reason vocabulary does not by itself make every reason an
unconditional trigger.

## GP-C --- Precursor / dependent-route model

**Decision: PASS --- FROZEN**

RERR recipients are selected using **actual ENP routing evidence
available at the point of generation**.

The architecture does not import an AODV-style precursor mechanism by
assumption.

The route table remains authoritative for persistent route state. R4-D
does not create precursor state. Multiple dependent nodes and multiple
affected destinations are supported where the existing routing evidence
establishes them. If required dependency evidence is unavailable, the
implementation must not invent it.

## GP-D --- RERR propagation model

**Decision: PASS --- FROZEN**

Propagation uses **selective unicast / multi-unicast to explicitly
identified dependent recipients**.

Received RERRs are not blindly broadcast or blindly forwarded.

``` text
receive RERR
      ↓
R4-D validation / applicability / freshness
      ↓
route invalidation
      ↓
routing integration determines whether
dependent recipients require notification
      ↓
new locally generated RERR(s), if authorized
```

R4-D itself does not become an RERR forwarder.

## GP-E --- Destination sequence semantics

**Decision: PASS --- FROZEN**

A generated RERR carries the destination sequence associated with the
affected routing destination according to the existing ENP
routing/discovery sequence semantics.

No second RERR-specific sequence system is introduced. Values remain
compatible with R4-D serial-number comparison and wrap-around behavior.

## GP-F --- Duplicate and loop control

**Decision: PASS --- FROZEN**

No independent routing duplicate-cache architecture is introduced.

Control comes from: 1. authorized generation conditions; 2. selective
propagation; 3. no blind forwarding; 4. existing ENP routing
identity/sequence semantics; 5. R4-D freshness rejection of stale RERRs.

A failed RERR transmission does **not** recursively generate another
RERR.

## GP-G --- Multiple destinations

**Decision: PASS --- FROZEN**

One failed next-hop relationship may affect multiple destination routes.

Because the canonical RERR wire format represents one unreachable
destination, the implementation generates **one RERR per affected
destination / propagation target as required by the routing decision**.

The RERR wire format is not expanded for aggregation.

## GP-H --- E5C interaction

**Decision: PASS --- FROZEN**

E5C remains authoritative for the existing route-invalidation semantics.

RERR generation does not modify E5C and does not introduce a second
invalidation mechanism.

The approved relationship is:

``` text
failure observation
        ↓
E5C route invalidation
        ↓
route becomes STALE
        ↓
routing maintenance / RERR policy
```

or, where the normalized routing event is already available, a single
routing-maintenance decision may coordinate the existing invalidation
and RERR decision without changing E5C.

## GP-I --- E5D interaction

**Decision: PASS --- FROZEN**

The existing E5D architecture remains authoritative:

``` text
STALE
  ↓
repair / discovery
  ↓
ACTIVE
```

E5D decides when repair is required and uses the existing R4 discovery
architecture.

A terminal E5D repair failure may provide the routing evidence required
for `LOCAL_REPAIR_FAILED`. RERR generation is a subsequent
routing-maintenance decision; it is not implemented by changing E5D
Step-3.

E5D Step-3, R4-A/B/C, R4-D, I33, and existing repair state transitions
remain untouched.

## GP-J --- Transport boundary

**Decision: PASS --- FROZEN**

An approved RERR uses the **existing ENP packet and transport path**:

``` text
routing task / routing integration
        ↓
RERR generation
        ↓
ENP packet construction
        ↓
existing transport submission
        ↓
ESP-NOW
```

No direct `esp_now_send()` RERR path, RERR-specific transport API, or
routing state-machine processing in the ESP-NOW callback is introduced.

Asynchronous results follow the existing boundary:

``` text
ESP-NOW callback
        ↓
bounded event handoff
        ↓
ENP / routing task
```

Immediate synchronous transport errors remain ordinary transport
submission results.

RERR transmission failure does not automatically create another RERR.

RERR transmission does not become a Reliability transaction and does not
alter Reliability retry semantics.

Logical ENP identities remain separate from transport-specific
addresses.

# 5. Frozen end-to-end architecture

``` text
                 FAILURE OBSERVATION
                         │
                         ▼
              ROUTING MAINTENANCE /
                  INTEGRATION
                         │
                         ▼
                ROUTE-STATE DECISION
                         │
             ┌───────────┴───────────┐
             │                       │
          no RERR                  RERR
                                     │
                                     ▼
                              RERR GENERATION
                                     │
                                     ▼
                           DEPENDENCY SELECTION
                                     │
                                     ▼
                           RERR PROPAGATION
                                     │
                                     ▼
                         EXISTING ENP PACKET PATH
                                     │
                                     ▼
                         EXISTING TRANSPORT API
                                     │
                                     ▼
                                  ESP-NOW
                                     │
                                     ▼
                           REMOTE RERR RECEPTION
                                     │
                                     ▼
                                  R4-D
                                     │
                                     ▼
                       VALIDATION / FRESHNESS
                                     │
                                     ▼
                            ROUTE INVALIDATION
```

# 6. Frozen ownership matrix

  -----------------------------------------------------------------------
  Responsibility                      Owner
  ----------------------------------- -----------------------------------
  Transport result                    Transport

  Transport-specific status           Transport adapter
  translation                         

  Logical next-hop association        Routing integration

  Interpretation of routing failure   Routing maintenance / integration

  Persistent route state              Route table

  Route invalidation                  Existing E5C / R4-D boundaries

  RERR generation decision            Routing maintenance / integration

  RERR construction                   RERR generation layer within
                                      routing integration

  Dependency / recipient selection    Routing integration using existing
                                      routing evidence

  RERR propagation decision           Routing integration

  RERR transport submission           Existing transport boundary

  Reliability transaction/retry state Reliability

  RERR reception and validation       R4-D

  RERR freshness decision             R4-D

  RERR-induced route invalidation     R4-D through existing route-table
                                      callback boundary

  Route discovery                     Existing R4 discovery subsystem

  E5D repair decision                 E5D
  -----------------------------------------------------------------------

# 7. Explicit non-goals

This freeze does **not** authorize: - modification of P4-E5B; -
modification of P4-E5C; - modification of P4-E5D Step-3; - modification
of P4-E5E-I33; - modification of the frozen RERR wire format; -
modification of frozen RERR reason-code values; - redesign of R4-D; -
redesign of the route table; - modification of Reliability transaction
or retry semantics; - direct routing logic in the ESP-NOW callback; -
introduction of an AODV-style precursor subsystem without separate
approval; - production implementation outside the subsequent RERR
Implementation Gate; - hardware validation as part of this architecture
gate.

# 8. Acceptance matrix

  Criterion                                       Result
  ----------------------------------------------- ----------
  Single RERR generation owner                    **PASS**
  Generation triggers mapped to frozen reasons    **PASS**
  Failure observation separated from generation   **PASS**
  Dependency / precursor evidence defined         **PASS**
  RERR destination selection defined              **PASS**
  Propagation / forwarding rules defined          **PASS**
  Duplicate suppression defined                   **PASS**
  Loop prevention defined                         **PASS**
  Sequence semantics compatible with R4-D         **PASS**
  Multiple affected destinations covered          **PASS**
  E5C boundary preserved                          **PASS**
  E5D boundary preserved                          **PASS**
  R4-D remains receive/process/invalidate         **PASS**
  Transport ownership preserved                   **PASS**
  Reliability ownership preserved                 **PASS**
  Implementation gate can be derived              **PASS**

# 9. Gate result

## **RERR-GP-ARCH-01 --- PASS / FROZEN**

GP-A through GP-J are frozen as the approved RERR Generation &
Propagation architecture.

The architecture is now sufficiently defined to derive a separate **RERR
Implementation Gate**.

**This document does not itself authorize production RERR-generation or
propagation code.**

# 10. Freeze boundary

``` text
P4-E5B             FROZEN
P4-E5C             FROZEN
P4-E5D Step-3      FROZEN
P4-E5E-I33         HARDWARE VALIDATED / FROZEN

RERR wire          FROZEN
RERR reasons       FROZEN
R4-D               FROZEN
Route table        FROZEN
Transport          FROZEN
Reliability        FROZEN

GP-A … GP-J        FROZEN / PASS

RERR generation    IMPLEMENTATION NOT YET OPEN
RERR propagation   IMPLEMENTATION NOT YET OPEN
```

**Document classification:** Frozen architecture baseline.\
**Next controlled artifact:** `RERR Implementation Gate`.
