# IG-G.5 — Existing Transport Submission

**Project:** ENP  
**Baseline:** ENP-0.3.1 / IG-G.4  
**Status:** COMPLETE — Existing Transport Boundary Exposed  
**Next gate:** IG-G.6 — Dependent-Recipient Selection / RERR Propagation Integration

## 1. Objective

Expose the existing routing-to-transport submission boundary required by the
IG-G RERR generator without modifying ESP-NOW, E5C, E5D, IG-D, R4-D, or the
validated IG-F.7.8 path.

## 2. Implementation

The routing runtime now exposes:

```c
esp_err_t enp_routing_runtime_submit_packet(
    enp_route_destination_t recipient,
    const enp_packet_t *packet);
```

The function performs only the following operations:

```text
logical recipient
       ↓
existing resolve_transport callback
       ↓
transport address
       ↓
enp_transport_send()
       ↓
active transport implementation
```

The active ESP-NOW transport remains responsible for queueing the raw frame
and performing the actual ESP-NOW transmission. Its existing send path
validates the destination, adds the peer when necessary, copies the frame into
the static TX request, and queues it for the transport worker.

## 3. Architectural Constraints Preserved

The implementation does **not**:

- call `esp_now_send()` from routing code;
- perform route selection;
- modify route state;
- invalidate routes;
- initiate route repair;
- maintain RERR recipient state;
- broadcast RERRs;
- alter the RERR wire format;
- alter RERR reason codes;
- modify E5C/IG-D/E5D semantics;
- modify R4-D;
- modify IG-F.7.8.

## 4. Error Semantics

The boundary returns:

- `ESP_ERR_INVALID_ARG` for an uninitialized runtime, invalid packet, or
  invalid logical recipient;
- `ESP_ERR_NOT_FOUND` when the configured routing integration cannot resolve
  the logical recipient to a transport address;
- the existing transport error when transport submission fails.

The distinction is intentional: logical-to-transport resolution remains a
routing-integration concern, while physical transmission remains a transport
concern.

## 5. Relationship to IG-G.4

IG-G.4 constructs the canonical RERR packet and invokes an abstract
`enp_rerr_submit_fn`. IG-G.5 provides the production routing-runtime boundary
that an adapter can use to implement that callback.

No direct generator → ESP-NOW dependency is introduced.

## 6. Verification

The implementation was reviewed against the current source baseline for:

- existing `enp_transport_send()` API;
- existing `enp_transport_t` ownership;
- existing `resolve_transport` callback;
- existing ESP-NOW TX queue path;
- packet length/data access APIs.

The new function uses the existing packet frame length and frame-data accessors
and delegates through `enp_transport_send()`.

## 7. Result

### PASS — Existing Transport Submission Boundary Implemented

IG-G.5 is complete. The existing routing → transport submission boundary is
now explicitly exposed for RERR generation integration. Recipient selection and
the actual wiring of an authorized RERR-generation event remain outside this
gate.
