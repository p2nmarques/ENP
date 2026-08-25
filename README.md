# ENP — ESP Network Protocol

![Status](https://img.shields.io/badge/status-v0.2.0--Frozen-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.2-red)
![License](https://img.shields.io/badge/license-GPLv3.0-green)

ENP (ESP Network Protocol) is a lightweight, modular, transport-independent
network protocol being developed for ESP32 nodes using **ESP-IDF 6.0.2** and
**ESP-NOW 2.0** as the current transport.

ENP provides a protocol and networking layer above the transport, keeping
transport-specific details below the routing, reliability and application
services.

The v0.2 milestone established the frozen one-hop protocol foundation. The
project has subsequently extended that foundation through routing, multi-hop
forwarding, DATA/ACK reliability, duplicate suppression, production runtime
integration, next-hop failure detection, route invalidation and route repair /
rediscovery.

---

## 1. Current Project Status

**Current baseline:** ENP v0.2-r5  
**Target:** ESP-IDF 6.0.2  
**Current transport:** ESP-NOW 2.0  
**Overall status:** **Phase 4 P4-E5E-I33 HARDWARE VALIDATED / FROZEN — 2026-08-25**

The current validated progression is:

```text
v0.2 foundation
      ↓
E3 reliability + multi-hop
      ↓
Phase 4 consolidation
      ↓
P4-E4 production runtime
      ↓
P4-E5A failure detection
      ↓
P4-E5B real TX-result observation
      ↓
P4-E5C route invalidation
      ↓
P4-E5D Step-3 route repair / R4 rediscovery
      ↓
HARDWARE VALIDATED / FROZEN
```

### Current Phase 4 milestones

| Milestone | Status |
|---|---|
| P4-E1 reusable DATA/ACK data plane | 🔒 Validated / Frozen |
| P4-E2 reliability maintenance | 🔒 Validated / Frozen |
| P4-E3 E3C consolidation | 🔒 Validated / Frozen |
| P4-E4A dispatcher local dispatch | 🔒 Validated / Frozen |
| P4-E4B production receive path | 🔒 Validated / Frozen |
| P4-E4C production runtime wiring | 🔒 Hardware validated / Frozen |
| P4-E4D production runtime hardware validation | 🔒 Hardware validated / Frozen |
| P4-E5A next-hop failure detection / propagation | 🔒 Validated / Frozen |
| P4-E5B real ESP-NOW TX-result observation | 🔒 Hardware validated / Frozen |
| P4-E5C transport failure → route invalidation | 🔒 Hardware validated / Frozen |
| P4-E5D Step-3 E5D → R4 route rediscovery | 🔒 Hardware validated / Frozen |
| **P4-E5E-I33 Reliability + route-repair resume** | **🔒 Hardware validated / Frozen — 2026-08-25** |

The detailed evidence for each milestone is maintained under
`docs/05_validation/`.

---

## 2. Frozen and Validated Features

ENP development distinguishes between **FROZEN** contracts, **VALIDATED**
implementation, and **HARDWARE VALIDATED** behavior.

| Component / Feature | Status |
|---|---|
| ENP core types | 🔒 Frozen |
| Logical addressing | 🔒 Frozen |
| Packet format | 🔒 Frozen |
| CRC16 | 🔒 Frozen |
| Transport abstraction | 🔒 Frozen |
| Service contract | 🔒 Frozen |
| Sequence-number identity | 🔒 Frozen |
| ESP-NOW 2.0 transport | ✅ Hardware validated |
| Static receive path | ✅ Hardware validated |
| Dispatcher | ✅ Hardware validated |
| Discovery | ✅ Hardware validated |
| Neighbor table | ✅ Hardware validated |
| Periodic Discovery | ✅ Hardware validated |
| Neighbor aging | ✅ Hardware validated |
| Neighbor stale/recovery | ✅ Hardware validated |
| Duplicate cache | ✅ Hardware validated |
| Duplicate suppression | 🔒 Frozen / hardware validated |
| DATA wire format / validation | ☑️ Validated |
| DATA multi-hop forwarding | ☑️ Validated |
| ACK multi-hop forwarding | ☑️ Validated |
| DATA duplicate suppression in multi-hop path | ☑️ Validated |
| ACK duplicate suppression | ☑️ Validated |
| DATA retransmission / ACK recovery | ✅ Hardware validated |
| E3A reliability → routing integration | ☑️ Validated |
| E3B reliability → routing → ESP-NOW | ✅ Hardware validated |
| E3C three-node reliability | ✅ Hardware validated |
| P4-E1 reusable DATA/ACK data plane | 🔒 Frozen / validated |
| P4-E2 reliability maintenance | 🔒 Frozen / validated |
| P4-E3 E3C consolidation | 🔒 Frozen / validated |
| P4-E4A dispatcher local dispatch | 🔒 Frozen / validated |
| P4-E4B production receive path | 🔒 Frozen / validated |
| P4-E4C production runtime wiring | 🔒 Frozen / hardware validated |
| P4-E4D production runtime hardware validation | 🔒 Frozen / hardware validated |
| P4-E5A next-hop failure detection | 🔒 Frozen / validated |
| P4-E5B real ESP-NOW TX-result observation | 🔒 Frozen / hardware validated |
| P4-E5C transport failure → route invalidation | 🔒 Frozen / hardware validated |
| P4-E5D Step-3 route repair / R4 rediscovery | 🔒 Frozen / hardware validated |
| **P4-E5E-I33 Reliability recovery across route repair** | **🔒 Frozen / hardware validated** |

### Status interpretation

- 🔒 **FROZEN** — approved contract or validated implementation baseline that
  should not be changed casually.
- ☑️ **VALIDATED** — implemented and validated through a controlled self-test
  or integration test.
- ✅ **HARDWARE VALIDATED** — behavior demonstrated on real ESP32 hardware using
  the relevant production path and acceptance criteria.
- 🟡 **PROPOSED** — specified design that has not yet become the implementation
  baseline.
- 🚧 **IMPLEMENTING** — implementation work is active and not yet validated.
- 📜 **HISTORICAL** — previous project state; not the current status.

The status table above is the authoritative current feature/status summary for
this README. Detailed validation evidence is maintained in
`docs/05_validation/`.

### Validation-level rule

**VALIDATED** means that the defined behavior has been demonstrated through a
controlled self-test or integration test. **HARDWARE VALIDATED** is stricter:
the feature must run on the real ESP32 hardware, use the relevant real hardware
interface and production path, satisfy its acceptance criteria, produce
observable evidence, and have a reproducible documented hardware test.

**FROZEN** is independent of test level. It means that the validated contract
or implementation has been accepted as the project baseline.

The complete criteria are defined in
`docs/05_validation/HARDWARE_VALIDATION_AND_FREEZE_CRITERIA.md`.

---

## 3. P4-E5D Step-3 — Route Repair / R4 Rediscovery

P4-E5D Step-3 connects the validated E5D route-repair coordinator to the
existing R4 route-discovery implementation through a thin orchestration
adapter.

The frozen ownership model is:

```text
E5C
 │
 │ transport failure / route invalidation
 ▼
E5D repair coordinator
 │
 │ repair request
 ▼
Step-3 orchestration adapter
 │
 ├── R4-A discovery lifecycle
 ├── R4-B RREQ processing
 ├── R4-C RREP processing
 └── existing route table
```

The Step-3 boundary deliberately preserves:

- R4-A ownership of discovery state/lifecycle;
- R4-B ownership of RREQ processing;
- R4-C ownership of RREP processing;
- route-table ownership of route state and metadata;
- the existing RREQ/RREP wire format;
- transport independence;
- bounded/static resource policy;
- failed-next-hop exclusion at the routing integration boundary;
- RREP correlation and completion of the active discovery transaction.

No second routing protocol was introduced and no new RREQ/RREP wire format was
required.

### Step-3 controlled validation

The controlled Step-3 validation passed the expanded lifecycle suite,
including:

```text
Initial RREQ transmission                          ✅
Stale destination sequence preservation            ✅
Failed-next-hop RREP candidate rejection           ✅
Alternative next-hop RREP completion               ✅
Route ACTIVE after repair                          ✅
Duplicate repair suppression                       ✅
Queued affected destination                        ✅
Old/insufficient RREP rejection                    ✅
Maximum retry / discovery failure                  ✅
Queued repair activation after failure             ✅
Unrelated route preservation                       ✅
```

The controlled test concluded:

```text
E3.3.7 Phase 4 / P4-E5D Step 3 self-test PASS
```

### Step-3 hardware validation

The V3.4 three-node hardware validation demonstrated the real end-to-end
failure and repair path:

```text
A / Gateway
     │
     │ DATA
     ▼
B / failed next-hop
     │
     X  ESP-NOW failure
     │
     ▼
A / E5C → E5D
     │
     │ RREQ
     ▼
C / destination
     │
     │ RREP
     ▼
A / Gateway
     │
     ▼
R4 completion
     │
     ▼
route ACTIVE via C
sequence preserved = 7
```

The Gateway observed:

```text
PASS: real E5C failure transitioned route C before rapid E5D repair completion
PASS: real E5C failure created/consumed an active E5D repair
PASS: real R4 rediscovery repaired route C via next-hop C
PASS: repaired route C is ACTIVE
PASS: repaired route C uses alternative next-hop C
PASS: repaired route C preserves sequence 7
E3.3.7 Phase 4 / P4-E5D Step-3 hardware validation PASS
```

The real RREP was also observed at the Gateway RX boundary and entered the
Step-3 adapter. The diagnostic run recorded zero RX verification failures and
one correctly classified/processed RREP.

The V3.4 change was **validation-harness-only**: it corrected the assertion for
the very short-lived `STALE` transition. It did not add functionality to the
Step-3 adapter or modify the routing protocol.

### Step-3 freeze

**P4-E5D Step-3 is HARDWARE VALIDATED / FROZEN as of 2026-08-23.**

The frozen Step-3 implementation is now the regression baseline for subsequent
routing development. Future changes must be introduced as a new revision or
phase and must not silently modify the frozen Step-3 contract.

Primary records:

- `docs/05_validation/E3.3.7_PHASE4_P4-E5D_STEP3_HARDWARE_VALIDATION_RECORD.md`
- `docs/05_validation/E3.3.7_PHASE4_P4-E5D_STEP3_CONTROLLED_VALIDATION_EXPANSION.md`
- `docs/05_validation/E3.3.7_PHASE4_P4-E5D_STEP3_AUDIT_CLEANUP_REGRESSION.md`
- `docs/05_validation/E3.3.7_PHASE4_P4-E5D_STEP3_FINAL_IMPLEMENTATION_AUDIT.md`
- `docs/05_validation/E3.3.7_PHASE4_P4-E5D_STEP3_INTERFACE_EXTENSION_DESIGN_v2.md`

---

## 4. Hardware Validation Summary

Hardware validation has been performed incrementally as ENP moved from the
frozen v0.2 one-hop foundation into routing, multi-hop forwarding, reliability
and production runtime integration.

### v0.2 foundation

The Gateway/Sensor hardware validation confirmed:

```text
Gateway ↔ Sensor communication          ✅
ESP-NOW transport                       ✅
Packet reception                        ✅
Discovery TX/RX                         ✅
Bidirectional Discovery                 ✅
Periodic Discovery                      ✅
Neighbor table updates                  ✅
Neighbor aging                          ✅
STALE transition                        ✅
STALE → ACTIVE recovery                 ✅
Duplicate cache                         ✅
Dispatcher duplicate suppression        ✅
NEW → DUPLICATE → EXPIRED → NEW         ✅
Normal Discovery after duplicate test   ✅
```

The normal Gateway and Sensor firmware were subsequently flashed again and
confirmed operational after removal of the temporary replay diagnostic.

### E3 hardware validation

The E3 hardware validation extended the validated behavior beyond the original
one-hop foundation:

```text
Real ESP-NOW DATA transmission           ✅
Reliability → routing integration        ✅
Reliability → routing → ESP-NOW          ✅
Two-node reliability transaction         ✅
DATA retransmission                      ✅
ACK generation and correlation           ✅
ACK loss / recovery                      ✅
Three-node A → B → C DATA path           ✅
Three-node C → B → A ACK path            ✅
Duplicate DATA suppression at B          ✅
Cached-ACK recovery at B                 ✅
Exactly-once application delivery        ✅
Final reliability result = DELIVERED     ✅
```

### Phase 4 hardware milestones

```text
P4-E4C  Production runtime wiring               HARDWARE VALIDATED / FROZEN
P4-E4D  Production runtime hardware validation  HARDWARE VALIDATED / FROZEN
P4-E5B  Real ESP-NOW TX-result observation      HARDWARE VALIDATED / FROZEN
P4-E5C  Transport failure → route invalidation  HARDWARE VALIDATED / FROZEN
P4-E5D  Step-3 route repair / R4 rediscovery    HARDWARE VALIDATED / FROZEN
P4-E5E  Reliability + route-repair resume (I33) HARDWARE VALIDATED / FROZEN
```

Detailed records are maintained under `docs/05_validation/`.

---

## 5. Current Project Scope

The original v0.2 one-hop foundation remains frozen.

The following higher-level capabilities have been implemented and validated:

```text
Route discovery
Route selection
Multi-hop forwarding
TTL handling in tested forwarding paths
DATA multi-hop delivery
ACK multi-hop delivery
DATA duplicate suppression
ACK duplicate suppression
DATA retransmission / ACK recovery
Next-hop failure detection
Transport failure → route invalidation
Route repair / R4 rediscovery through the frozen Step-3 adapter
Reliability recovery across repaired route / E5E-I33
```

The E3.3.7 reliability subsystem uses static transaction storage, ACK
correlation, timeout/retry processing, result reporting and a
transport/routing-independent submit callback.

Phase 4 consolidated this validated behavior into reusable DATA/ACK,
reliability-maintenance, dispatcher, production receive/runtime and routing
failure/repair boundaries.

### Explicitly outside the current frozen Step-3 scope

The following are **not claimed as validated by P4-E5D Step-3 alone**:

```text
RERR integration
Multi-hop RREQ forwarding through a relay
Multi-hop RREP forwarding through a relay
Multi-path routing
Larger-topology route-repair validation
Fragmentation
Security
OTA
```

These remain separate future work unless a dedicated validation milestone is
created.

---

## 6. Current Milestones

```text
E3.3.1  DATA wire/self-test                     VALIDATED
E3.3.2  DATA multi-hop forwarding               VALIDATED
E3.3.3  DATA + ACK multi-hop path               VALIDATED
E3.3.4  DATA duplicate suppression              VALIDATED
E3.3.5  ACK duplicate suppression               VALIDATED
E3.3.6  DATA retransmission / ACK recovery      VALIDATED
E3.3.7  Reliability layer + Phase 3             VALIDATED / FROZEN

P4-E1   Reusable DATA/ACK data plane            VALIDATED / FROZEN
P4-E2   Reliability maintenance                 VALIDATED / FROZEN
P4-E3   E3C consolidation                       VALIDATED / FROZEN
P4-E4A  Dispatcher local dispatch               VALIDATED / FROZEN
P4-E4B  Production receive path                 VALIDATED / FROZEN
P4-E4C  Production runtime wiring               HARDWARE VALIDATED / FROZEN
P4-E4D  Production runtime hardware validation  HARDWARE VALIDATED / FROZEN
P4-E5A  Next-hop failure/propagation            VALIDATED / FROZEN
P4-E5B  Real ESP-NOW TX-result observation      HARDWARE VALIDATED / FROZEN
P4-E5C  Transport failure → route invalidation  HARDWARE VALIDATED / FROZEN
P4-E5D  Step-3 route repair / R4 rediscovery    HARDWARE VALIDATED / FROZEN
```

---

## 7. Development / Freeze Rule

ENP development follows:

```text
Design
  ↓
Specification
  ↓
API / Interface Audit
  ↓
Implementation
  ↓
Clean build
  ↓
Controlled test
  ↓
Hardware validation
  ↓
Documentation synchronization
  ↓
Freeze
```

A feature is not considered complete merely because its headers, enums or
placeholders exist.

Once a feature is frozen, subsequent changes must be explicitly treated as a
new revision or phase and must preserve the frozen baseline as a regression
reference.

---

## 8. Documentation Structure

The `docs/` directory is organized by subject:

```text
docs/
├── 00_project/       Current roadmap and implementation status
├── 01_architecture/  Core architecture, API and freeze rules
├── 02_protocol/      Wire protocol and duplicate suppression
├── 03_routing/       Routing architecture and routing protocol
├── 04_reliability/   Reliability specification and contracts
├── 05_validation/    Implementation, regression and hardware validation records
├── 99_historical/    Historical reviews and snapshots
└── INDEX.md          Documentation map
```

Historical documents are intentionally preserved and are not used as the
current project-status source. Current status is maintained in
`docs/00_project/E3.3.7_IMPLEMENTATION_STATUS.md` and
`docs/00_project/ROADMAP.md`.

---

## 9. Repository Baseline

This README describes the project baseline **ENP-0.2-r5**, synchronized through the **P4-E5E-I33 freeze on 2026-08-25**.

The current baseline includes the validated P4-E5D Step-3 source, the validated P4-E5E-I33 integration test, and their corresponding audit, controlled-validation and hardware-validation records.

The Step-3 freeze is a project-process boundary as well as an implementation
boundary: future routing work should build on this baseline rather than modify
it implicitly.
