# E3.3.7 — ENP Reliability Layer
## Draft Specification, API and Data Structures

**Project:** ENP  
**Version:** v0.2-r5  
**Target:** ESP-IDF 6.0.2  
**Milestone:** E3.3.7  
**Status:** Approved draft / Design Freeze candidate

---

## 1. Purpose

E3.3.7 defines the ENP reliability mechanism responsible for providing controlled reliable delivery for DATA packets that require an acknowledgement.

The reliability layer provides:

- ACK scheduling;
- ACK correlation;
- timeout handling;
- retransmission;
- retry accounting;
- delivery success/failure reporting.

E3.3.7 does not replace the existing routing, transport, wire-format, or duplicate-suppression mechanisms.

The E3.3.6 hardware test remains the empirical baseline for retransmission, duplicate DATA suppression and ACK recovery.

---

## 2. Architectural Position

The reliability layer is positioned between the application and the ENP routing layer:

```text
Application
     |
     v
ENP Reliability Layer
     |
     +-- ACK_REQUIRED
     +-- transaction tracking
     +-- ACK correlation
     +-- timeout
     +-- retry accounting
     +-- retransmission
     +-- delivery result
     |
     v
ENP Routing Layer
     |
     v
ENP Transport / ESP-NOW
```

Routing answers **where** a packet should go.

Reliability answers **whether the end-to-end DATA transaction was acknowledged** and what action is required when an acknowledgement is not received.

---

## 3. Reliability Transaction

A reliable DATA transmission creates one reliability transaction owned by the originating node.

A transaction progresses through:

```text
CREATED
   |
   v
WAITING_FOR_ACK
   |          ACK       TIMEOUT
   |          |
   v          v
DELIVERED   RETRYING
              |
              v
       WAITING_FOR_ACK
              |
              v
            FAILED
```

Intermediate relay nodes do not own the end-to-end transaction.

---

## 4. ACK_REQUIRED

Reliable DATA is identified by the existing DATA flags mechanism.

Conceptually:

```text
ACK_REQUIRED = 1
```

creates a reliability transaction.

```text
ACK_REQUIRED = 0
```

uses best-effort DATA semantics and does not create a reliability transaction.

E3.3.7 must reuse the existing DATA wire-format flags rather than introducing a new DATA header field.

---

## 5. ACK Correlation

An ACK must unambiguously identify the DATA transaction that it acknowledges.

The correlation identity is:

```text
DATA:
    origin
    destination
    data sequence
    application sequence

ACK:
    origin = DATA destination
    destination = DATA origin
    data sequence = DATA sequence
    application sequence = DATA application sequence
```

The ACK packet's own packet sequence is not the DATA transaction identifier.

The fundamental correlation condition is:

```text
ACK.data_sequence == DATA.sequence
```

Additional origin, destination and application-sequence checks must also succeed.

---

## 6. Timeout

The originating node starts the ACK timeout after the DATA has been successfully submitted to the ENP transport/routing path.

Initial implementation parameter:

```c
#define ENP_RELIABILITY_ACK_TIMEOUT_MS 1000U
```

The timeout is configurable at compile time.

The 1000 ms value is the initial E3.3.7 implementation value; it is not intended to become an immutable protocol constant.

---

## 7. Retransmission

When the ACK timeout expires and retries remain, the originating node retransmits the same DATA transaction.

The DATA transaction identity must remain unchanged:

```text
attempt 0: data_seq = N
attempt 1: data_seq = N
attempt 2: data_seq = N
attempt 3: data_seq = N
```

The retransmitted DATA must therefore be recognized as a duplicate by the existing duplicate-suppression mechanism.

The application payload must not be delivered more than once.

---

## 8. Retry Accounting

The initial DATA transmission is not a retry.

```text
initial transmission:
    retry_count = 0

first retransmission:
    retry_count = 1

second retransmission:
    retry_count = 2

third retransmission:
    retry_count = 3
```

Initial implementation parameter:

```c
#define ENP_RELIABILITY_MAX_RETRIES 3U
```

When:

```text
retry_count < ENP_RELIABILITY_MAX_RETRIES
```

another retransmission is permitted.

When the retry limit is exhausted, the transaction enters `FAILED`.

---

## 9. Maximum Reliability Transactions

The implementation uses statically allocated transaction storage.

Initial implementation parameter:

```c
#define ENP_RELIABILITY_MAX_TRANSACTIONS 8U
```

No dynamic allocation is required for reliability transactions.

If all transaction slots are occupied, creation of a new reliability transaction must fail deterministically and report a resource error to the caller.

---

## 10. Duplicate DATA

A retransmitted DATA packet must never produce a second application delivery.

```text
DATA seq=N
    |
    +--> first reception --> application delivery

DATA seq=N again
    |
    +--> duplicate --> no second application delivery
```

The existing duplicate-suppression mechanism remains responsible for duplicate DATA detection.

---

## 11. ACK Recovery

When a duplicate DATA is received after the original ACK has been lost, the appropriate previously generated ACK must be recoverable and forwarded.

The required behaviour is:

```text
duplicate DATA
      |
      +--> suppress application delivery
      |
      +--> recover/forward appropriate ACK
```

This behaviour was demonstrated by E3.3.6 and is now part of the reliability design.

---

## 12. Duplicate ACK

A duplicate ACK must not complete the same reliability transaction more than once.

```text
first valid ACK
    |
    +--> transaction = DELIVERED

duplicate ACK
    |
    +--> ignore for completion purposes
```

The existing ACK duplicate-suppression mechanism remains responsible for duplicate ACK forwarding suppression.

---

## 13. Delivery Success

A reliability transaction enters:

```text
ENP_RELIABILITY_STATE_DELIVERED
```

only after a valid, correlated ACK is received.

Local ESP-NOW transmission success alone is not end-to-end delivery confirmation.

---

## 14. Delivery Failure

When the transaction exhausts its retry budget without receiving a valid correlated ACK:

```text
ENP_RELIABILITY_STATE_FAILED
```

is reported to the application.

The reliability layer owns the final delivery result.

---

## 15. Route Failure

A temporary route problem should not automatically be converted into an immediate reliability failure.

If routing can recover, the reliability transaction remains eligible for retransmission.

If the routing subsystem explicitly reports an unrecoverable failure, the reliability transaction may transition to `FAILED`.

Routing remains responsible for route state; reliability remains responsible for delivery state.

---

# 16. API

The E3.3.7 API is intentionally small.

### Initialization

```c
bool enp_reliability_init(void);
```

Initializes the statically allocated reliability state.

Expected behaviour:

- clears all transaction slots;
- initializes internal synchronization;
- prepares the reliability task/event mechanism;
- returns `true` on successful initialization.

---

### Start

```c
bool enp_reliability_start(void);
```

Starts reliability processing.

The implementation may use a statically allocated FreeRTOS task.

---

### Send

```c
bool enp_reliability_send(
    const enp_data_packet_t *packet,
    uint32_t now_ms,
    enp_reliability_handle_t *handle);
```

Creates a reliability transaction and submits the DATA packet for transmission.

The caller receives a transaction handle.

The packet must have `ACK_REQUIRED` semantics.

---

### Process ACK

```c
bool enp_reliability_process_ack(
    const enp_ack_packet_t *ack,
    uint32_t now_ms);
```

Processes an incoming ACK.

The function:

1. validates the ACK;
2. identifies the corresponding transaction;
3. validates origin/destination;
4. validates DATA sequence;
5. validates application sequence;
6. transitions the transaction to `DELIVERED`.

A duplicate ACK must not produce a second completion event.

---

### Tick

```c
void enp_reliability_tick(uint32_t now_ms);
```

Processes timeout conditions.

For each active transaction:

```text
now_ms >= deadline_ms
```

causes either:

```text
retry
```

or:

```text
FAILED
```

depending on the retry count.

The implementation should preferably invoke this from one statically allocated reliability task rather than creating one timer per transaction.

---

### Get State

```c
bool enp_reliability_get_state(
    enp_reliability_handle_t handle,
    enp_reliability_state_t *state);
```

Returns the current transaction state.

---

### Cancel

```c
bool enp_reliability_cancel(
    enp_reliability_handle_t handle);
```

Cancels an active transaction.

Cancellation is distinct from delivery failure.

---

# 17. Public Types

## Transaction state

```c
typedef enum {
    ENP_RELIABILITY_STATE_INVALID = 0,
    ENP_RELIABILITY_STATE_CREATED,
    ENP_RELIABILITY_STATE_WAITING_FOR_ACK,
    ENP_RELIABILITY_STATE_RETRYING,
    ENP_RELIABILITY_STATE_DELIVERED,
    ENP_RELIABILITY_STATE_FAILED,
    ENP_RELIABILITY_STATE_CANCELLED
} enp_reliability_state_t;
```

---

## Transaction handle

```c
typedef uint16_t enp_reliability_handle_t;
```

The handle identifies a transaction slot and must not expose internal storage directly.

---

## Result

```c
typedef enum {
    ENP_RELIABILITY_RESULT_NONE = 0,
    ENP_RELIABILITY_RESULT_PENDING,
    ENP_RELIABILITY_RESULT_DELIVERED,
    ENP_RELIABILITY_RESULT_FAILED,
    ENP_RELIABILITY_RESULT_CANCELLED,
    ENP_RELIABILITY_RESULT_NO_RESOURCES
} enp_reliability_result_t;
```

---

# 18. Internal Transaction Data Structure

The implementation should use a fixed-size structure similar to:

```c
typedef struct {
    bool active;

    enp_reliability_handle_t handle;

    uint16_t origin;
    uint16_t destination;

    uint16_t data_sequence;
    uint16_t application_sequence;

    uint8_t retry_count;

    uint32_t deadline_ms;

    enp_reliability_state_t state;

    /*
     * Retransmission data.
     *
     * The exact representation must be selected during implementation
     * based on the existing ENP DATA ownership/lifetime model.
     */
} enp_reliability_transaction_t;
```

The exact packet-storage member is deliberately left as an implementation decision until the existing DATA buffer ownership model is inspected.

---

# 19. Static Transaction Table

The reliability implementation should contain:

```c
static enp_reliability_transaction_t
    s_transactions[ENP_RELIABILITY_MAX_TRANSACTIONS];
```

with:

```c
#define ENP_RELIABILITY_MAX_TRANSACTIONS 8U
```

No heap allocation is required for transaction management.

---

# 20. Concurrency

Reliability state must not be concurrently modified by:

- ESP-NOW receive callbacks;
- timeout processing;
- application code;
- retransmission processing.

The preferred architecture is:

```text
ESP-NOW RX callback
        |
        v
event/queue
        |
        v
Reliability Task
        |
        +--> ACK processing
        +--> timeout processing
        +--> retransmission
        +--> completion
```

The implementation should use statically allocated FreeRTOS resources, consistent with the ENP project direction.

---

# 21. E3.3.7 Self-Test Requirements

The reliability core must have deterministic self-tests for:

### Test 1 — Immediate ACK

```text
DATA -> ACK
```

Expected:

```text
retry_count = 0
state = DELIVERED
```

### Test 2 — One retry

```text
DATA -> timeout -> retry -> ACK
```

Expected:

```text
retry_count = 1
state = DELIVERED
```

### Test 3 — Retry exhaustion

```text
DATA -> timeout -> retry
     -> timeout -> retry
     -> timeout -> retry
     -> timeout -> FAILED
```

Expected:

```text
retry_count = 3
state = FAILED
```

### Test 4 — Duplicate ACK

Expected:

```text
completion count = 1
```

### Test 5 — Duplicate DATA

Expected:

```text
application delivery count = 1
```

### Test 6 — ACK recovery

Expected:

```text
first ACK lost
DATA retransmitted
duplicate DATA suppressed
ACK recovered
transaction = DELIVERED
```

E3.3.6 already provides the hardware baseline for Test 6.

---

# 22. Acceptance Criteria

E3.3.7 is complete only when:

- ACK_REQUIRED semantics are implemented;
- ACK correlation is deterministic;
- transactions are statically allocated;
- timeout handling works;
- retransmission works;
- retry accounting works;
- retry limit is enforced;
- duplicate DATA does not cause duplicate application delivery;
- duplicate DATA can recover the appropriate ACK;
- duplicate ACK does not cause duplicate completion;
- delivery success is reported;
- delivery failure is reported;
- transaction lifetime is bounded;
- concurrency is deterministic;
- E3.3.4 regression passes;
- E3.3.5 regression passes;
- E3.3.6 regression passes;
- implementation is ESP-IDF 6.0.2 compatible.

---

# 23. Frozen Parameters for Initial Implementation

```c
#define ENP_RELIABILITY_ACK_TIMEOUT_MS       1000U
#define ENP_RELIABILITY_MAX_RETRIES          3U
#define ENP_RELIABILITY_MAX_TRANSACTIONS     8U
```

These are **initial implementation parameters**, not immutable protocol constants.

They may be tuned after hardware measurements without changing the E3.3.7 architectural contract.

---

# 24. Non-Goals

E3.3.7 does not introduce:

- a new DATA header;
- a new routing protocol;
- route discovery changes;
- dynamic memory allocation;
- application-level retry logic;
- per-packet FreeRTOS timers;
- changes to ESP-NOW itself;
- changes to E3.3.4 duplicate DATA semantics;
- changes to E3.3.5 duplicate ACK semantics;
- changes to the frozen E3.3.6 protocol behaviour.

---

# 25. Design Freeze Boundary

The following are considered frozen for the E3.3.7 implementation:

```text
ACK_REQUIRED semantics
ACK correlation model
transaction ownership
state machine
timeout/retry model
retry accounting
finite retry limit
static transaction table
duplicate DATA behaviour
duplicate ACK behaviour
delivery result model
```

The following remain implementation details:

```text
exact task stack size
queue length
critical-section mechanism
packet buffer ownership
exact callback/event integration
logging format
exact CMake component dependency list
```

These implementation details must remain compatible with ESP-IDF 6.0.2 and the existing ENP architecture.
