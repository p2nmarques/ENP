# P4-E5D Step 2 — E5C Route Failure → E5D Repair Event Integration

**Status:** IMPLEMENTED — CONTROLLED VALIDATION PENDING
**Target:** ESP-IDF 6.0.2

## Scope

Step 2 adds only the integration boundary between the already validated E5C
route-invalidation path and the Step-1 E5D repair coordinator.

## Added

`enp_routing_data_path_t` now exposes:

```c
typedef void (*enp_routing_route_failure_fn)(
    void *context,
    enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop);
```

with registration through:

```c
bool enp_routing_data_path_set_route_failure_callback(...);
```

After E5C transitions an ACTIVE route to STALE, the routing data path reports
that affected route to the registered E5D callback with the logical destination
and failed logical next-hop.

The notification is emitted only after a successful route-table invalidation.

## Preserved boundaries

- E5C remains the owner of transport-failure detection and route invalidation.
- E5D remains the owner of repair-request coordination.
- R4 is not modified.
- Reliability is not modified.
- Transport is not modified.
- The ENP wire format is not modified.
- No new route-table state is introduced.
- The E5D callback is notification-only and does not perform discovery inline.

## Controlled test

`E3.3.7_p4_E5D_step2_test_enp_e5c_repair_event_main.c` verifies:

1. E5C invalidates affected routes.
2. Each affected destination generates one E5D repair request.
3. The logical destination is preserved.
4. The failed logical next-hop is preserved.
5. An unrelated route remains ACTIVE and generates no repair request.
6. Two affected destinations can coexist in the bounded E5D repair state.
7. The E5D task consumes the resulting repair events.

## Not yet implemented

- R4 discovery integration.
- RREP route-update adapter.
- Failed-next-hop exclusion during route repair.
- Route freshness decision.
- Repair completion/failure semantics.
- Reliability/retransmission integration.

These remain later E5D steps.
