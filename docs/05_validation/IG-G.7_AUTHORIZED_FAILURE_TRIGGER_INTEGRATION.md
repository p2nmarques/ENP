# IG-G.7 — Authorized Failure Trigger Integration

**Project:** ENP  
**Baseline:** ENP-0.3.1 / IG-G.6  
**Status:** PASS — Authorized trigger boundary integrated  
**Scope:** Connect an explicitly authorized routing failure decision to the existing RERR generation, recipient selection, and transport path.

## 1. Objective

IG-G.7 establishes the production integration point that permits routing logic to request an RERR after it has made an authorized failure decision.

The implementation deliberately does **not** convert an arbitrary ESP-NOW send failure, E5D request admission, or E5D request consumption directly into an RERR.

## 2. Production flow

```text
routing failure / repair logic
          |
          | explicit authorized decision
          v
 enp_routing_runtime_generate_authorized_rerr()
          |
          +--> authoritative route-table sequence
          |
          +--> IG-G.6 dependent-recipient selection
          |
          v
 enp_rerr_generation_submit()
          |
          v
 enp_routing_runtime_submit_packet()
          |
          v
 existing logical -> transport resolution
          |
          v
 existing ENP transport / ESP-NOW
```

## 3. Implementation

The routing runtime now owns initialized instances of:

- `enp_rerr_generation_t`
- `enp_rerr_recipient_selection_t`

The runtime connects the generator's submit callback to the already-existing
`enp_routing_runtime_submit_packet()` boundary.

A new public API is provided:

```c
esp_err_t enp_routing_runtime_generate_authorized_rerr(
    enp_route_destination_t unreachable,
    enp_route_error_reason_t reason);
```

The API:

1. requires an initialized routing runtime;
2. validates the unreachable destination;
3. obtains the destination's authoritative route-table entry;
4. uses the stored route sequence as the RERR destination sequence;
5. constructs the IG-G generation request;
6. invokes the existing IG-G.6 recipient selector;
7. invokes the existing IG-G.4 RERR generator;
8. submits through the existing IG-G.5 transport boundary.

## 4. Deliberate trigger constraint

The new API is an **authorized-trigger API**, not an automatic transport-error handler.

The following remain prohibited:

```text
ESP-NOW send failure -> automatic RERR
E5D request accepted  -> LOCAL_REPAIR_FAILED
E5D request consumed  -> LOCAL_REPAIR_FAILED
unknown route evidence -> guessed recipient
no recipient          -> broadcast RERR
```

This preserves the E5C -> IG-D -> E5D separation and prevents premature RERR generation while repair is still pending.

## 5. E5C preservation

The existing E5C callback continues to perform only its established handoff:

```text
transport result
    -> route invalidation
    -> IG-D observation
```

No RERR packet is generated synchronously from the transport send-result callback.

## 6. E5D preservation

The existing E5D interface remains unchanged.

In particular, request admission/consumption is not treated as a terminal repair failure. A future E5D terminal failure outcome may call the authorized RERR API once that outcome is explicitly available.

## 7. Sequence-number source

The RERR destination sequence is obtained from the authoritative route-table entry for the affected destination.

No independent RERR sequence database is introduced.

The route entry is permitted to be STALE because E5C may already have invalidated it while retaining the routing evidence required for the RERR.

## 8. Recipient selection

The existing IG-G.6 selector remains responsible for enumerating evidenced dependent recipients.

No persistent precursor table is introduced.

If no recipient is evidenced, the generator does not broadcast an RERR.

## 9. Transport preservation

RERR transmission continues to use:

```text
RERR generation
    -> enp_routing_runtime_submit_packet()
    -> existing resolve_transport()
    -> enp_transport_send()
    -> ESP-NOW
```

There is no direct IG-G -> `esp_now_send()` path.

## 10. Source changes

Modified:

- `main/core/routing/enp_routing_runtime.c`
- `main/core/routing/enp_routing_runtime.h`

No changes were made to:

- E5C data-path invalidation logic;
- IG-D coalescer;
- E5D repair coordinator;
- route table semantics;
- discovery;
- ESP-NOW transport;
- RERR wire format;
- RERR receive processor;
- IG-F.7.8 application path.

## 11. Acceptance criteria

- [x] Explicit authorized RERR trigger API exists.
- [x] Trigger is task/integration-context callable rather than a transport callback action.
- [x] Destination sequence comes from the authoritative route table.
- [x] Existing IG-G.6 recipient selection is reused.
- [x] Existing IG-G.4 packet generation is reused.
- [x] Existing IG-G.5 transport submission is reused.
- [x] No direct ESP-NOW dependency is introduced.
- [x] E5C remains unchanged.
- [x] IG-D remains unchanged.
- [x] E5D remains unchanged.
- [x] No automatic transport-failure-to-RERR conversion is introduced.
- [x] No precursor database is introduced.

## 12. Result

### PASS — Authorized Failure Trigger Boundary Integrated

IG-G.7 now provides the concrete routing-runtime entry point required to turn an explicitly authorized routing failure into the existing RERR generation and propagation path.

The implementation intentionally stops short of inventing a terminal E5D repair-failure signal. That signal remains a separate integration requirement if `LOCAL_REPAIR_FAILED` is to be generated automatically from E5D.

## 13. Next gate

**IG-G.8 — End-to-End RERR Generation Trigger Validation**

The next validation should exercise a controlled authorized trigger and correlate:

```text
trigger
 -> RERR generation
 -> recipient selection
 -> logical-to-transport resolution
 -> ESP-NOW submission
 -> receiving node RERR processing
 -> route invalidation
```

The test should also verify that untriggered transport failures do not generate RERRs.
