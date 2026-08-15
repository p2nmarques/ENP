# ENP Documentation Review Matrix — Post E3.3.6

**Project:** ENP v0.2-r5  
**Target:** ESP-IDF 6.0.2  
**Review point:** After E3.3.6, before E3.3.7 implementation/freeze

## Status legend

- 🟢 **Aligned** — document accurately represents the current project state.
- 🟡 **Needs synchronization** — useful document, but current-state claims are stale or incomplete.
- 🔵 **Historical** — intentionally records a previous baseline and should be preserved.
- 🟠 **Proposed** — current design/specification awaiting implementation/validation.
- 🔴 **Contradictory** — current claims conflict materially with validated implementation.

## Matrix

| Document | Status before review | Finding | Action |
|---|---|---|---|
| `README.md` | 🟡 | Described ENP as a one-hop-only frozen baseline and marked routing/multi-hop/retransmission as not implemented. This no longer represents the post-E3.3.6 project state. | **Updated** |
| `docs/ROADMAP.md` | 🔴 | Routing, multi-hop and retransmission remained in the future-work boundary despite E3 validation. | **Updated** |
| `docs/CORE_FREEZE.md` | 🟡 | Correct historical v0.2 foundation freeze, but its boundary could be misread as the current whole-project state. | **Updated with post-freeze clarification** |
| `docs/DOCUMENTATION_FREEZE_REVIEW.md` | 🔵 | Explicitly records the ENP-0.2-m3 documentation freeze review. Its old boundaries are historically correct. | **Preserved; not rewritten** |
| `docs/DUPLICATE_INTEGRATION.md` | 🟢 | Duplicate-suppression integration remains consistent with the validated architecture. | **No change** |
| `docs/ENP_PROTOCOL_v0.2.md` | 🟡 | Reliability and routing sections described the original v0.2 runtime and did not reflect later E3 validation. | **Updated** |
| `docs/ARCHITECTURE.md` | 🔴 | Current topology and architecture stopped at the two-node one-hop foundation. | **Updated** |
| `docs/ENP_ROUTING_ARCHITECTURE_v0.2.md` | 🟡 | Architecture remains useful and consistent, but status was still framed only as an implementation-review baseline. | **Status clarified** |
| `docs/ENP_ROUTING_PROTOCOL_v0.2.md` | 🟡 | Protocol contract remained marked as an implementation gate although routing behavior is now exercised by E3. | **Status clarified; contract not declared frozen** |
| `docs/ENP-v0.2-Routing-Integration-Architecture-Specification.md` | 🟡 | Integration baseline was still labeled only proposed despite subsequent implementation/integration testing. | **Status clarified** |
| `docs/API_GUIDELINES.md` | 🟢 | Layering/API principles remain compatible with the proposed reliability architecture. | **No change** |
| `docs/ENP_Reliability_Layer_Specification_E3.3.7.md` | 🟠 | Approved draft; appropriately not implemented yet. | **Status retained as PROPOSED** |
| `docs/PROJECT_STATE_REVIEW_E3.3.6.md` | 🟢 | New current-state checkpoint created by this review. | **Added** |

## Key conclusions

### 1. The original v0.2 freeze is still valid

The frozen foundation is not being invalidated.

### 2. Higher-level E3 work must be documented separately

The routing and E3.3 behavior were developed on top of the frozen foundation.

### 3. E3.3.6 is validated behavior, not a general reliability subsystem

This distinction is now explicit in the synchronized documentation.

### 4. E3.3.7 remains proposed

The reliability architecture, API and data structures are approved as a draft,
but implementation and hardware validation are still required.

## Documentation lifecycle

The repository should follow:

```text
Historical freeze
      ↓
Current-state review
      ↓
Specification
      ↓
Implementation
      ↓
Validation
      ↓
Documentation update
      ↓
Freeze
```

This matrix is the review record for the E3.3.6 → E3.3.7 transition.
