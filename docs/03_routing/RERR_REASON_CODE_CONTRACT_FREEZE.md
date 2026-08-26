# ENP v0.2 — RERR Reason-Code Contract Freeze

**Baseline:** ENP-0.2-r5(20260825-203116)  
**Date:** 2026-08-25  
**Status:** FROZEN — DOCUMENTATION/CONTRACT ONLY  
**Production source changes:** NONE

## 1. Authority decision

The project adopts the canonical routing wire definitions in
`main/core/protocol/payloads/enp_routing.h/.c` as the authoritative RERR
reason-code contract.

This decision follows the provenance audit of the baseline documentation,
including historical routing/protocol material and the R4-D/E1.3 implementation
and tests.

No authoritative earlier document was found that establishes the alternative
reason-code mapping previously recorded in the draft RERR audit.

## 2. Frozen reason-code values

| Value | Symbol | Meaning |
|---:|---|---|
| 0 | `ENP_ROUTE_ERROR_UNKNOWN` | Reserved/invalid unknown reason |
| 1 | `ENP_ROUTE_ERROR_NO_ROUTE` | No usable route exists |
| 2 | `ENP_ROUTE_ERROR_NEXT_HOP_UNREACHABLE` | Selected next-hop is unreachable |
| 3 | `ENP_ROUTE_ERROR_ROUTE_EXPIRED` | Route lifetime/validity expired |
| 4 | `ENP_ROUTE_ERROR_LOCAL_REPAIR_FAILED` | Local route repair failed |
| 5 | `ENP_ROUTE_ERROR_TTL_EXPIRED` | Routing TTL expired |

Reason `0` is not a valid accepted RERR reason. Values outside 1..5 are
invalid/reserved.

## 3. Evidence chain

The canonical RERR wire definition is 16 bytes and contains the reason field
at byte offset 10. The routing architecture explicitly requires the canonical
`enp_routing.h/.c` definitions to be the single source of truth.

R4-D accepts exactly reasons 1..5 and rejects UNKNOWN/reserved values. Its unit
test exercises the complete valid reason range.

E1.3 integration tests use the contract operationally, including
`NEXT_HOP_UNREACHABLE` for the failed-next-hop invalidation scenario and
`NO_ROUTE` for absent/stale route cases.

## 4. Ownership

This freeze does not assign new ownership.

- RERR wire definition: routing protocol definitions.
- RERR validation/freshness: R4-D.
- Persistent route state: route table.
- Route discovery: R4.
- Route repair coordination: E5D.
- Transport failure observation: existing transport/E5C boundary.
- Reliability transaction/retry state: Reliability.

E5D must not redefine RERR reason semantics or wire format.

## 5. Explicit non-change

The following remain frozen and untouched:

- P4-E5B
- P4-E5C
- P4-E5D Step-3
- R4 production implementation
- routing production implementation
- transport
- Reliability
- P4-E5E-I33 test

## 6. Contract status

**RERR REASON-CODE CONTRACT: FROZEN**

This freeze authorizes documentation synchronization only. It does not authorize
new RERR generation/propagation implementation.
