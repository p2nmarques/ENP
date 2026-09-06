# IG-G.8c — Minimal Upstream Evidence Propagation Implementation

**Project:** ENP  
**Baseline:** `ENP-0.3.1_IG-G.7_Authorized_Failure_Trigger_Integration.zip`  
**Status:** COMPLETE — Minimal Evidence Propagation Implemented  
**Predecessor:** IG-G.8b — Upstream Evidence Propagation Interface Audit  
**Next gate:** IG-G.8 — End-to-End RERR Generation Trigger Validation

---

## 1. Purpose

IG-G.8c implements only the missing propagation path identified by IG-G.8b:

```text
received/forwarded ENP packet
        ↓
transient upstream evidence
        ↓
E5C failure notification
        ↓
IG-D failure event
        ↓
E5D repair request
```

The implementation does **not** generate an RERR automatically.

---

## 2. Implementation

### 2.1 Forwarding evidence

`enp_routing_data_path_forward()` records the following bounded tuple before
transmission:

```text
destination
failed_next_hop
upstream = packet.source
```

The evidence is held in a fixed-size table of 16 entries.

Existing entries are reused for the same exact tuple. When the table is full,
new unverifiable evidence is not allowed to evict existing evidence.

This is transient forwarding evidence, not a persistent precursor table.

### 2.2 Failure propagation

When the existing transport-result callback identifies a failed transport
address, E5C continues to perform the existing route invalidation first.

For each affected route, the data path additionally retrieves matching
upstream evidence and emits the new extended notification:

```c
void (*enp_routing_route_failure_ex_fn)(
    void *context,
    enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop,
    enp_route_destination_t upstream);
```

The legacy route-failure callback remains available and unchanged for existing
callers/tests.

### 2.3 IG-D propagation

The IG-D event now retains:

```text
destination
failed_next_hop
upstream
state
```

The coalescing identity remains unchanged:

```text
(destination, failed_next_hop)
```

Upstream is evidence carried with the event; it is not part of the duplicate
identity.

### 2.4 E5D propagation

The E5D repair request now carries the same optional upstream evidence:

```text
destination
failed_next_hop
upstream
```

The existing repair-request API remains source-compatible through its wrapper;
the new extended admission API is used by IG-D when upstream evidence exists.

The E5D adapter may ignore this field until the authorized RERR trigger is
implemented. No E5D repair behaviour is changed by IG-G.8c.

---

## 3. Production Runtime Connection

The production routing runtime registers only the extended failure callback.

This avoids observing the same physical failure twice and ensures that the
upstream evidence is handed to IG-D atomically through the new boundary.

The production flow is therefore:

```text
Forward packet
    │
    ├── source = upstream
    ├── destination = affected destination
    └── next_hop = selected route next hop
             │
             ▼
       transport failure
             │
             ▼
            E5C
             │
             ├── route → STALE
             │
             └── failure + upstream evidence
                         │
                         ▼
                       IG-D
                         │
                         ▼
                       E5D
```

No RERR is emitted by this path.

---

## 4. Preserved Behaviour

IG-G.8c does not modify:

- route selection;
- route-table ACTIVE/STALE semantics;
- E5C transport-failure detection;
- E5C route invalidation;
- E5D repair discovery;
- R4 behaviour;
- ESP-NOW transport;
- dispatcher behaviour;
- RERR wire format;
- RERR receive processing;
- IG-F.7.8 application forwarding;
- reliability transaction ownership.

No persistent precursor table is introduced.

---

## 5. Safety Properties

### No dependency from neighbour visibility

Discovery/visibility remains insufficient to create RERR recipients.

### No automatic RERR

Transport failure still means route failure and repair initiation; it does not
implicitly mean RERR generation.

### No guessed recipient

If upstream forwarding evidence is absent, no extended evidence event is
created for that missing upstream.

### Bounded resources

Forwarding evidence is statically bounded to 16 records.

### Legacy compatibility

The existing two-argument route-failure callback remains available for older
callers and tests.

---

## 6. IG-G.8c Acceptance

| Criterion | Result |
|---|---|
| Capture upstream from forwarded packet | PASS |
| Preserve destination | PASS |
| Preserve failed next hop | PASS |
| Carry evidence through E5C boundary | PASS |
| Carry evidence through IG-D | PASS |
| Carry evidence through E5D request | PASS |
| Preserve existing coalescing identity | PASS |
| Preserve E5C route invalidation | PASS |
| Preserve E5D repair semantics | PASS |
| Avoid persistent precursor table | PASS |
| Avoid automatic RERR generation | PASS |
| Avoid transport modification | PASS |
| Keep resources statically bounded | PASS |

## Result

**PASS — Minimal Upstream Evidence Propagation Implemented**

The current architecture now retains the upstream logical identity associated
with a failed forwarding operation long enough for the subsequent authorized
RERR integration to use it.

The next gate is the actual controlled/hardware **IG-G.8 end-to-end RERR
trigger validation**.
