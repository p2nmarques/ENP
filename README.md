# ENP — ESP Network Protocol

![Status](https://img.shields.io/badge/status-v0.2.0--Frozen-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.2-red)
![License](https://img.shields.io/badge/license-GPLv3.0-green)

ENP (ESP Network Protocol) is a lightweight, modular, transport-independent network protocol being developed for ESP32 nodes using **ESP-IDF 6.0.2** and the **ESP-NOW 2.0** transport.

ENP is intended to provide a protocol and networking layer above transports such as ESP-NOW without exposing transport-specific details to services and applications.

The v0.2 milestone established the frozen one-hop protocol foundation. The project has since extended that foundation with routing, multi-hop forwarding, DATA/ACK data-plane behavior, duplicate suppression across multi-hop traffic, and a hardware-validated retransmission/ACK-recovery test path.

---

**Project status:**
- v0.2 foundation frozen;
- E1/E2 validated and frozen;
- E3A self-test and E3B two-node ESP-NOW reliability integration validated;
- E3C three-node reliability validation is hardware validated and frozen;
- Phase 4 P4-E1 reusable data plane, P4-E2 reliability maintenance, P4-E3 E3C consolidation,
  P4-E4A local dispatch, P4-E4B production receive path and P4-E4C production runtime wiring are validated and frozen.
  P4-E4D production runtime hardware validation — HARDWARE VALIDATED / FROZEN
  P4-E5A next-hop failure detection and propagation — VALIDATED / FROZEN
  

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
| P4-E4C production runtime wiring | 🔒 Frozen / HARDWARE VALIDATED |
| P4-E5A next-hop failure detection | 🔒 Frozen / validated |

### Status interpretation

- 🔒 **FROZEN** — approved contract or validated implementation baseline that
  should not be changed casually.
- ☑️ **VALIDATED** — implemented and validated through a controlled self-test
  or integration test, but not necessarily a hardware-validation milestone.
- ✅ **HARDWARE VALIDATED** — behavior demonstrated on real ESP32 hardware.
- 🟡 **PROPOSED** — specified/approved design that is not yet frozen as an
  implementation.
- 🚧 **IMPLEMENTING** — implementation work is active and not yet validated.
- 📜 **HISTORICAL** — records a previous project state and should not be
  interpreted as the current state.

The status table above is the authoritative current feature/status summary for
this README. Validation records in `docs/05_validation/` provide the detailed
evidence for individual milestones.

### Validation-level rule

For ENP, **VALIDATED** means that the defined behavior has been demonstrated
through a controlled self-test or integration test. **HARDWARE VALIDATED** is a
stricter status: the feature must run on the real target ESP32 hardware, use the
real relevant hardware interface and production path, satisfy its acceptance
criteria, produce observable evidence, and have a reproducible documented
hardware test.

**FROZEN** is independent of the test level: it means the validated contract or
implementation has been accepted as the project baseline. Therefore a feature
may legitimately be **VALIDATED / FROZEN** without yet being **HARDWARE
VALIDATED**.

The complete criteria are defined in
`docs/05_validation/HARDWARE_VALIDATION_AND_FREEZE_CRITERIA.md`.



## 3. Hardware Validation Summary

Hardware validation has been performed incrementally as ENP moved from the
frozen v0.2 one-hop foundation into routing, multi-hop forwarding and
reliability integration.

### v0.2 foundation hardware validation

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

E3B validated the real ESP-NOW reliability integration between two nodes.
E3C then validated the complete reliability transaction over the three-node
A → B → C routing topology, including retransmission, duplicate DATA
suppression and cached-ACK recovery.

### P4-E5A validation evidence

Controlled validation evidence is recorded in `docs/05_validation/E3.3.7_PHASE4_P4-E5A_VALIDATION_RECORD.md`. P4-E5A is VALIDATED / FROZEN and is not yet HARDWARE VALIDATED.

### Phase 4 validation boundary

Phase 4 P4-E1 through P4-E4D consolidated the corresponding reusable ENP core
components and production-runtime boundaries. P4-E4C and P4-E4D are hardware
validated; P4-E5A is validated/frozen by controlled self-test. The earlier
Phase 4 stages remain validated/frozen based on their
defined test evidence:

```text
P4-E1  Reusable DATA/ACK data plane        ☑️ Validated / frozen
P4-E2  Reliability maintenance             ☑️ Validated / frozen
P4-E3  E3C consolidation                   ☑️ Validated / frozen
P4-E4A Dispatcher local dispatch           ☑️ Validated / frozen
P4-E4B Production receive path             ☑️ Validated / frozen
P4-E4C Production runtime wiring           ✅ Hardware validated / frozen
```

These Phase 4 stages are primarily controlled self-test and integration
validation milestones. They should not be described as additional hardware
validation milestones unless a dedicated hardware test has been performed.

E3C remains the validated three-node reliability hardware milestone and is
already **PASS / FROZEN**. P4-E4C is now hardware validated through P4-E4D.


## 4. Current Project Scope

The original v0.2 one-hop foundation remains frozen.

The following higher-level capabilities have subsequently been implemented
and validated through the E3 test series:

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
```

The E3.3.7 reliability subsystem is implemented with static transaction storage,
ACK correlation, timeout/retry processing, result reporting and a
transport/routing-independent submit callback.

E3A and E3B validated its integration boundaries, while E3C hardware validation
validated the complete reliability transaction over the three-node topology.

Phase 4 subsequently consolidated this validated behavior into the reusable
DATA/ACK data plane, reliability maintenance, dispatcher local-dispatch
boundary, production receive path and production runtime wiring.

The following remain future work:

```text
Route failure / repair integration
RERR integration
Route rediscovery during reliability
Multi-path / larger-topology validation
Fragmentation
Security
OTA
```

E3.3.6 remains the historical hardware baseline for duplicate DATA suppression
and cached-ACK recovery; E3C validates those behaviors together with the
generic E3.3.7 reliability transaction over the three-node topology.


## 5. Current Milestones

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
P4-E5A  Next-hop failure detection/propagation  VALIDATED / FROZEN
```

The E3.3.7 reliability API and Phase 3 E3A/E3B/E3C integration boundaries are
validated and frozen. The validated behavior has subsequently been consolidated through P4-E1, P4-E2,
P4-E3, P4-E4A, P4-E4B and P4-E4C, with P4-E4D providing the production-runtime
hardware validation.


## 6. Development / Freeze Rule

ENP development follows:

```text
Design
  ↓
Specification
  ↓
API
  ↓
Implementation
  ↓
Clean build
  ↓
Test
  ↓
Hardware validation
  ↓
Documentation
  ↓
Freeze
```

A feature is not considered complete merely because its headers, enums,
or placeholders exist.

---

## 7. Documentation Structure

The `docs/` directory is organized by subject:

```text
docs/
├── 00_project/       Current roadmap and implementation status
├── 01_architecture/  Core architecture, API and freeze rules
├── 02_protocol/      Wire protocol and duplicate suppression
├── 03_routing/       Routing architecture and routing protocol
├── 04_reliability/   Reliability specification and contracts
├── 05_validation/    E3.3.7 implementation and hardware validation records
├── 99_historical/    Historical reviews and snapshots
└── INDEX.md          Documentation map
```

Historical documents are intentionally preserved and are not used as the
current project-status source. Current status is maintained in
`docs/00_project/E3.3.7_IMPLEMENTATION_STATUS.md` and
`docs/00_project/ROADMAP.md`.
