# RERR Documentation Synchronization — Current Status Addendum

This addendum is authoritative for the RERR workstream as of 2026-08-25.

The canonical RERR reason-code contract is frozen from `enp_routing.h/.c`:

- 1 `NO_ROUTE`
- 2 `NEXT_HOP_UNREACHABLE`
- 3 `ROUTE_EXPIRED`
- 4 `LOCAL_REPAIR_FAILED`
- 5 `TTL_EXPIRED`

The previous draft RERR/protocol reason-code mapping is **HISTORICAL / SUPERSEDED**.
It is retained only as historical documentation and is not authoritative. The
frozen `enp_routing.h/.c` mapping above is the sole authoritative ENP v0.2
reason-code contract for production code, tests, and new documentation.

No production source was changed by this synchronization.

P4-E5B, P4-E5C, P4-E5D Step-3 and P4-E5E-I33 remain frozen.
