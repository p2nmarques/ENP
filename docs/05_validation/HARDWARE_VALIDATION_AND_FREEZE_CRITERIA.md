# ENP Hardware Validation and Freeze Criteria

**Document:** E3.3.7 / ENP validation policy  
**Status:** FROZEN as the validation rule for the current development workflow  
**Target:** ESP-IDF 6.0.2  
**Scope:** ENP implementation, integration and hardware-validation milestones

---

## 1. Purpose

This document defines the distinction between:

- **IMPLEMENTED**
- **VALIDATED**
- **HARDWARE VALIDATED**
- **FROZEN**

The purpose is to prevent a controlled self-test or integration test from
being described as hardware validation when the actual target hardware and
production transport path have not yet been exercised.

This rule applies to future ENP phases and features unless a later approved
validation specification explicitly supersedes it.

---

## 2. Validation hierarchy

ENP development uses the following progression:

```text
IMPLEMENTED
     |
     +--> SELF-TEST VALIDATED
     |
     +--> INTEGRATION VALIDATED
     |
     +--> HARDWARE VALIDATED
              |
              v
           FROZEN
```

**FROZEN is not a higher testing level.** Freeze is a project decision that
establishes a validated implementation or contract as the baseline that should
not be changed casually.

A feature can therefore be:

```text
VALIDATED / FROZEN
```

without yet being:

```text
HARDWARE VALIDATED
```

Likewise, a hardware-validated feature should normally be frozen only after
the acceptance evidence and documentation have been reviewed and accepted.

---

## 3. IMPLEMENTED

A feature is **IMPLEMENTED** when the required source code and interfaces
exist and compile as part of the intended project.

Implementation alone is not validation.

The following are not sufficient by themselves:

- headers or enums existing;
- placeholder functions existing;
- a clean compilation;
- successful linking;
- a code inspection showing that the intended behavior appears present.

---

## 4. VALIDATED

A feature is **VALIDATED** when its defined behavior has been demonstrated by
a controlled self-test, unit-style test, integration test, or equivalent
repeatable test, but a dedicated real-hardware validation of the feature has
not necessarily been recorded.

A validated test should:

1. exercise the intended interface;
2. verify the relevant acceptance criteria;
3. observe the resulting behavior;
4. report PASS/FAIL explicitly;
5. be reproducible from the documented test procedure.

A controlled transport, test double, deterministic callback, or synthetic
packet may be appropriate for this level of validation.

Examples from Phase 4 include:

```text
P4-E1  Reusable DATA/ACK data plane
P4-E2  Reliability maintenance
P4-E3  E3C consolidation
P4-E4A Dispatcher local dispatch
P4-E4B Production receive path
P4-E4C Production runtime wiring
```

These phases are validated and frozen, but their individual Phase 4 self-tests
must not automatically be labelled hardware validation.

---

## 5. HARDWARE VALIDATED

A feature or phase is **HARDWARE VALIDATED** only when all of the following
conditions are satisfied.

### 5.1 Real target hardware

The implementation is running on the actual target ESP32 hardware for which
the feature is intended.

A host-side test, simulator, mock device, or controlled software-only test is
not sufficient.

### 5.2 Real relevant hardware interface

The hardware interface relevant to the feature is exercised.

For the current ENP transport this normally means the actual ESP-NOW transport
running on the target ESP32 devices.

If the feature depends on Wi-Fi channel selection, peer configuration,
physical reception, or another hardware-dependent property, that behavior
must be exercised as part of the validation where relevant.

### 5.3 Real production path relevant to the feature

The test must exercise the actual production path that the feature is intended
to use.

Calling an internal function directly is not sufficient when the feature's
production behavior depends on a receive, routing, dispatch or transport
boundary.

For example:

```text
Real ESP-NOW RX
      |
      v
Production receive path
      |
      +--> DATA / ACK data plane
      |
      +--> normal dispatcher
      |
      v
Application/service
```

The exact path depends on the feature under test.

### 5.4 Observable evidence

The important behavior must be observed rather than inferred.

Logs, counters, packet observations, transaction results, or other measurable
evidence are acceptable.

For example:

```text
DATA received       = 2
DATA forwarded      = 1
duplicate DATA      = 1
ACK recovered       = 1
final result        = DELIVERED
```

is stronger evidence than simply reporting that the test task completed.

### 5.5 Acceptance criteria satisfied

The defined acceptance criteria for the feature or phase must all pass.

A device booting successfully or a program running without a crash does not
constitute hardware validation.

### 5.6 Reproducible test

The hardware configuration and procedure must be sufficiently documented that
the validation can be repeated.

For a multi-node test this includes, where relevant:

- node roles;
- topology;
- logical ENP IDs;
- required transport configuration;
- boot/start order;
- intentional packet loss or fault injection;
- expected observations.

### 5.7 Recorded result

The validation evidence must be recorded in the project documentation,
including:

- hardware configuration;
- relevant test procedure;
- observed behavior;
- acceptance results;
- final PASS result.

---

## 6. FROZEN

A feature or contract is **FROZEN** when the project has accepted the validated
result as the baseline.

Freeze means:

- the interface/behavior is now an approved baseline;
- future changes require an explicit reason;
- changes should follow the design → specification → API → implementation →
  test → validation → documentation workflow;
- an existing frozen contract must not be silently changed to make a later
  test pass.

Freeze does not change the historical evidence level.

For example:

```text
P4-E4C
  VALIDATED
  FROZEN
  not yet HARDWARE VALIDATED
```

is a valid status.

---

## 7. Examples from the current ENP baseline

### 7.1 E3C — Hardware validated

E3C qualifies as **HARDWARE VALIDATED / FROZEN** because the reliability
transaction was exercised across the actual three-node ESP-NOW topology:

```text
A -> B -> C       DATA
C -> B -> A       ACK
```

The validation included retransmission, duplicate DATA suppression and cached
ACK recovery, with the final transaction completing as `DELIVERED`.

This is a hardware-validation milestone because the behavior was demonstrated
on real ESP32 nodes using the real ESP-NOW transport and the relevant
multi-hop production path.

### 7.2 P4-E1 — Validated / frozen

P4-E1 validated the reusable DATA/ACK data plane using controlled testing.

It demonstrated forwarding, TTL decrement, transaction-identity preservation
and separate DATA/ACK duplicate domains.

The result is:

```text
VALIDATED / FROZEN
```

unless a dedicated hardware validation of the P4-E1 feature itself is recorded.

### 7.3 P4-E2 — Validated / frozen

P4-E2 demonstrated that the ENP maintenance task causes the expected
reliability retransmission and preserves the DATA transaction identity.

The result is:

```text
VALIDATED / FROZEN
```

unless a dedicated hardware validation is recorded.

### 7.4 P4-E4A — Validated / frozen

P4-E4A validated the dispatcher local-dispatch boundary and generic duplicate
suppression behavior.

The result is:

```text
VALIDATED / FROZEN
```

unless a dedicated hardware validation is recorded.

### 7.5 P4-E4B — Validated / frozen

P4-E4B validated the production receive-path component, including the DATA/ACK
boundary and the normal dispatcher path.

The result is:

```text
VALIDATED / FROZEN
```

unless a dedicated hardware validation is recorded.

### 7.6 P4-E4C — Validated / frozen

P4-E4C validated the production runtime bootstrap composition:

```text
ENP context
    |
    v
Dispatcher
    |
    v
Discovery service
    |
    v
Production route table
    |
    v
Routing data path
    |
    v
Production receive path
    |
    v
Transport callback
```

The E4C self-test also exercised a Discovery frame through the production
receive callback and normal dispatcher path.

However, the E4C self-test used the controlled transport. Therefore:

```text
P4-E4C
  PASS
  VALIDATED
  FROZEN
  not yet HARDWARE VALIDATED
```

A subsequent test of the actual production application on real ESP32 hardware
using real ESP-NOW would be required before changing its status to hardware
validated.

---

## 8. Hardware-validation decision checklist

Before marking a feature or phase **HARDWARE VALIDATED**, answer YES to all:

```text
[ ] Real target ESP32 hardware used
[ ] Real relevant hardware interface used
[ ] Real production path exercised
[ ] Relevant behavior directly observed
[ ] All acceptance criteria passed
[ ] Test is reproducible
[ ] Hardware configuration documented
[ ] Result recorded in validation documentation
```

If any required item is NO, the feature should remain **VALIDATED** rather
than **HARDWARE VALIDATED**.

---

## 9. Relationship to the ENP freeze workflow

The ENP development workflow remains:

```text
Design
  |
  v
Specification
  |
  v
API
  |
  v
Implementation
  |
  v
Clean build
  |
  v
Controlled test
  |
  v
Hardware validation
  |
  v
Documentation
  |
  v
Freeze
```

Not every feature requires a separate hardware-validation milestone if its
behavior is genuinely independent of hardware. However, any feature described
as **HARDWARE VALIDATED** must satisfy the criteria in this document.

---

## 10. Terminology rule

Use the following terminology consistently throughout ENP documentation:

| Term | Meaning |
|---|---|
| IMPLEMENTED | Code exists and builds |
| VALIDATED | Defined behavior demonstrated by controlled/self/integration testing |
| HARDWARE VALIDATED | Defined behavior demonstrated on real target hardware through the relevant real hardware/production path |
| FROZEN | Accepted baseline whose contract/behavior should not be changed casually |
| PASS | The defined test execution satisfied its acceptance criteria |
| HISTORICAL | Describes a previous project state, not the current status |

**Do not use `PASS`, `VALIDATED`, `HARDWARE VALIDATED`, and `FROZEN` as
interchangeable terms.** They describe different properties of the project
state.
