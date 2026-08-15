# ENP Project State Review — Post E3.3.6

**Project:** ENP v0.2-r5  
**Review point:** After E3.3.6 hardware validation, before E3.3.7 implementation freeze  
**Target:** ESP-IDF 6.0.2  
**Status:** Current-state review

## 1. Purpose

This document records the project state after E3.3.6 and establishes the
documentation baseline for E3.3.7.

It deliberately does not rewrite historical freeze records.

## 2. Status terminology

- **FROZEN** — approved contract/baseline that should not be changed casually.
- **VALIDATED** — implemented and validated, including hardware validation where applicable.
- **PROPOSED** — specified/approved design that is not yet frozen as an implementation.
- **IMPLEMENTING** — active implementation not yet validated.
- **HISTORICAL** — records a previous project state.

## 3. Current validated sequence

```text
E3.3.1  DATA wire/self-test
E3.3.2  DATA multi-hop forwarding
E3.3.3  DATA + ACK multi-hop path
E3.3.4  DATA duplicate suppression
E3.3.5  ACK duplicate suppression
E3.3.6  DATA retransmission / ACK recovery
```

All six are treated as validated at this review point.

## 4. Reliability boundary

E3.3.6 validates a concrete retransmission/ACK-recovery behavior.

It does **not** mean that the general-purpose ENP reliability subsystem
has been implemented.

The following remain E3.3.7 work:

- ACK scheduling;
- reliability transaction tracking;
- timeout handling;
- general retransmission management;
- retry accounting;
- delivery success/failure reporting;
- static transaction management;
- concurrency/event handling.

## 5. Documentation findings

| Area | Current state | Action |
|---|---|---|
| README | Previously described only the original one-hop baseline | Synchronized with current E3 state |
| ROADMAP | Routing and retransmission were still listed as future work | Synchronized with validated E3 milestones |
| CORE_FREEZE | Original freeze boundary was mixed with current status | Clarified historical baseline vs later validated extensions |
| ENP_PROTOCOL_v0.2 | Routing/reliability status was stale | Updated without changing the wire contract |
| ARCHITECTURE | Current topology was still two-node | Updated to the three-node E3 topology |
| Routing architecture | Mostly aligned but status was old | Status clarified |
| Routing protocol | Still described as an implementation gate | Status clarified; final freeze remains pending |
| Routing integration | Proposed status was stale | Status clarified |
| DUPLICATE_INTEGRATION | Frozen and hardware-validated | Preserved |
| DOCUMENTATION_FREEZE_REVIEW | Historical v0.2-m3 record | Preserved as historical |
| E3.3.7 reliability specification | Approved draft | Kept PROPOSED |

## 6. Documentation decision

The original v0.2 freeze documents remain historical records of the
foundation they froze.

Current-state documents must distinguish:

```text
original v0.2 foundation
        ↓
higher-level validated E3 extensions
        ↓
proposed E3.3.7 reliability subsystem
```

No historical document should be rewritten in a way that changes the
record of its original review point.

## 7. E3.3.7 gate

E3.3.7 implementation may proceed from the approved specification only
after this synchronized documentation baseline is accepted.

E3.3.7 remains:

```text
PROPOSED / APPROVED DRAFT
```

until implementation, self-test, hardware validation and final freeze
are complete.
