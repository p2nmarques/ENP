# P4-E5D — ENP_DISCOVERY_TIMEOUT_MS Configuration Resolution

**Baseline:** ENP-0.2-r5(20260818-183327)  
**Target:** ESP-IDF 6.0.2  
**Status:** RESOLVED — IMPLEMENTED SOURCE CHANGE  
**Date:** 2026-08-18

## Decision

The authoritative route-discovery timeout is:

```c
#define ENP_DISCOVERY_TIMEOUT_MS 2000U
```

The existing 6000 ms value was not a route-discovery timeout. It was being used
by the maintenance task as the neighbor-expiration timeout.

The configuration has therefore been separated into:

```c
#define ENP_DISCOVERY_INTERVAL_MS 2000U
#define ENP_DISCOVERY_TIMEOUT_MS 2000U
#define ENP_NEIGHBOR_TIMEOUT_MS 6000U
```

## Why 2000 ms is retained for route discovery

The actual R4 route-discovery state machine already used a 2000 ms local fallback
in `enp_route_discovery.h`. The R4 tests also use `ENP_DISCOVERY_TIMEOUT_MS` as
the discovery transaction timeout.

The architecture documentation defines the discovery interval as 2000 ms and
the neighbor timeout as 6000 ms. Therefore the source naming was the
inconsistency, not the intended timing values.

## Changes made

### 1. `main/config/enp_defaults.h`

Changed:

```c
#define ENP_DISCOVERY_TIMEOUT_MS 6000U
```

to:

```c
#define ENP_DISCOVERY_TIMEOUT_MS 2000U
#define ENP_NEIGHBOR_TIMEOUT_MS 6000U
```

### 2. `main/core/routing/enp_route_discovery.h`

The route-discovery header now includes the canonical configuration:

```c
#include "config/enp_defaults.h"
```

and its private 2000 ms fallback definition was removed.

This makes `enp_defaults.h` the single authoritative definition.

### 3. `main/core/enp_maintenance.c`

Neighbor expiration now explicitly uses:

```c
ENP_NEIGHBOR_TIMEOUT_MS
```

instead of incorrectly reusing the discovery-timeout symbol.

The maintenance startup log also reports the neighbor timeout using the new
symbol.

## Result

The meanings are now unambiguous:

| Symbol | Value | Meaning |
|---|---:|---|
| `ENP_DISCOVERY_INTERVAL_MS` | 2000 ms | Periodic maintenance/discovery interval |
| `ENP_DISCOVERY_TIMEOUT_MS` | 2000 ms | R4 route-discovery transaction timeout |
| `ENP_NEIGHBOR_TIMEOUT_MS` | 6000 ms | Neighbor expiration timeout |

## E5D impact

This resolves the P4-E5D pre-implementation gate concerning discovery timing
ownership.

E5D must use the canonical:

```c
ENP_DISCOVERY_TIMEOUT_MS
```

and must not introduce another repair-specific discovery timeout.

No E5D behaviour has been implemented by this change.

## Validation performed

Static source verification confirms that production route-discovery deadline
calculations use `ENP_DISCOVERY_TIMEOUT_MS`, while neighbor expiration uses
`ENP_NEIGHBOR_TIMEOUT_MS`.

```text
idf.py fullclean
idf.py build
```

was done with no errors followed by the existing R4 controlled tests:

E3.2.2 real multi-hop	            ✅ PASS
E3.2.3 neighbor stability	        ✅ PASS
E3.2.4 logical → transport identity	✅ PASS


## Scope discipline

No changes were made to:

- R4 state-machine behaviour;
- RREQ/RREP wire format;
- E5A/E5B/E5C interfaces;
- reliability;
- transport;
- route-table state semantics;
- E5D implementation.

Only the configuration ownership inconsistency was resolved.
