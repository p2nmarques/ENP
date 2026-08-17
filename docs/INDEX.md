# ENP Documentation Index

This directory is organized by subject. The project targets **ENP v0.2-r5 / ESP-IDF 6.0.2**.

## 00 — Project
- `00_project/ROADMAP.md` — current roadmap and milestones.
- `00_project/E3.3.7_IMPLEMENTATION_STATUS.md` — current implementation and validation state.

## 01 — Architecture
- `01_architecture/ARCHITECTURE.md`
- `01_architecture/API_GUIDELINES.md`
- `01_architecture/CORE_FREEZE.md`
- `01_architecture/ENP-v0.2-Routing-Integration-Architecture-Specification.md`

## 02 — Protocol
- `02_protocol/ENP_PROTOCOL_v0.2.md`
- `02_protocol/DUPLICATE_INTEGRATION.md`

## 03 — Routing
- `03_routing/ENP_ROUTING_ARCHITECTURE_v0.2.md`
- `03_routing/ENP_ROUTING_PROTOCOL_v0.2.md`

## 04 — Reliability
- `04_reliability/ENP_Reliability_Layer_Specification_E3.3.7.md`

## 05 — Validation
Contains the E3.3.7 implementation audits, hardware validation records, and Phase 4 consolidation tests.

- `E3.3.7_PHASE4_E1_DATA_PLANE.md` — reusable receive data-plane consolidation.
- `E3.3.7_PHASE4_E2_RELIABILITY_MAINTENANCE_IDF6.0.2.md` — maintenance-driven reliability tick.
- `E3.3.7_PHASE4_E3_E3C_CONSOLIDATION_IDF6.0.2.md` — three-node E3C consolidation into reusable ENP infrastructure.

Current validated sequence:

```text
E1  Routing Data Path                         PASS / FROZEN
E2A Context + Neighbor                        PASS / FROZEN
E2B Real ESP-NOW                              PASS / FROZEN
E3A Reliability -> Routing                    PASS / FROZEN
E3B Reliability -> Routing -> ESP-NOW         PASS / FROZEN
E3C Three-node Reliability                    PASS / FROZEN
```

## 99 — Historical
Historical reviews and snapshots are preserved here and should not be interpreted as current project status.

## Documentation rule
After each validated development stage, update the current-status documents and the relevant validation record. Historical review documents are preserved rather than rewritten.
