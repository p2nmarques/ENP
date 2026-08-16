# ENP API Guidelines

**Version:** 0.2.0  
**Status:** Active

---

# 1. Purpose

These guidelines define the API and layering rules for ENP.

The goal is to keep the protocol modular, transport-independent, deterministic, and maintainable.

---

# 2. Layering

```text
Application
    ↓
Services
    ↓
ENP Core
    ↓
Transport abstraction
    ↓
Link implementation
    ↓
ESP-IDF
```

Dependencies must point downward.

A service must not directly include or call ESP-NOW APIs.

---

# 3. Public and internal APIs

The canonical public entry point is:

```c
#include "core/enp.h"
```

Advanced users may include individual public ENP headers when appropriate.

Core/public contracts include:

```text
enp.h
enp_types.h
enp_address.h
enp_node.h
enp_network.h
enp_context.h
enp_transport.h
enp_protocol.h
enp_packet.h
enp_crc16.h
enp_config.h
```

Internal/service infrastructure includes:

```text
dispatcher/
service/
link/
core/enp_duplicate.*
```

The distinction is architectural: a module being visible in the source tree does not automatically make its implementation details part of the public contract.

---

# 4. Naming

Functions:

```text
enp_*
```

Types:

```text
enp_*_t
```

Macros:

```text
ENP_*
```

Private functions and variables are `static`.

---

# 5. `enp_types.h`

`enp_types.h` contains only fundamental protocol types and enums.

It must not depend on:

- ESP-IDF
- FreeRTOS
- transport implementations
- services

Current fundamental types include:

```text
enp_node_id_t
enp_network_id_t
enp_sequence_t
enp_capability_t
enp_role_t
```

---

# 6. Error handling

Public functions return `esp_err_t`.

Return values must not be silently ignored.

`ESP_ERROR_CHECK()` is appropriate at application initialization boundaries where failure is fatal.

Library/core code should normally return the error to its caller.

---

# 7. Ownership

The application owns the runtime context:

```c
enp_context_t
```

The context contains the active transport pointer but does not own the transport implementation object.

Service descriptors are owned by their modules and must remain valid while registered.

---

# 8. Memory

Prefer deterministic/static allocation.

The ESP-NOW receive path uses:

```text
StaticQueue_t
StaticTask_t
static buffers
static task stack
```

The ESP-NOW callback must not perform blocking work.

---

# 9. Transport independence

Protocol services operate on:

```c
enp_transport_address_t
```

rather than ESP-NOW MAC addresses.

ESP-NOW-specific conversion belongs in:

```text
link/enp_transport_espnow.*
```

---

# 10. Service contract

A service descriptor contains:

```text
name
packet_type
init
process
```

The process callback receives:

```c
enp_context_t *
const enp_packet_t *
const enp_transport_address_t *
```

The dispatcher validates the packet before invoking the service.

---

# 11. Packet handling

Services should use the packet API:

```text
enp_packet_init()
enp_packet_header()
enp_packet_payload()
enp_packet_payload_const()
enp_packet_seal()
enp_packet_verify()
enp_packet_length()
```

Services should not duplicate packet serialization or CRC logic.

---

# 12. Time

Services should obtain ENP time through:

```c
enp_context_time_ms()
```

They should not introduce platform-specific timer dependencies when an ENP context abstraction is sufficient.

---

# 13. Documentation rule

Before implementing a new feature:

1. Define its responsibility.
2. Update the protocol specification if the wire format changes.
3. Define/update the API.
4. Implement it.
5. Build.
6. Test.
7. Hardware-validate where necessary.
8. Freeze the result before building dependent functionality.

---

# 14. No obsolete compatibility APIs

Once a v0.2 API has been frozen, do not reintroduce obsolete APIs solely to make old application code compile.

Examples of legacy APIs that should not return to the v0.2 core include old packet-finalization or direct ESP-NOW peer APIs.

Migration should move old applications onto the new architecture instead.



# 15. Duplicate suppression

`enp_duplicate` is a core runtime module owned by the dispatcher.

The public module contract is:

```c
enp_duplicate_cache_init()
enp_duplicate_cache_clear()
enp_duplicate_check_and_record()
enp_duplicate_count()
```

Services must not manipulate the duplicate cache directly.

The dispatcher performs duplicate detection after packet validation and
before service dispatch.

Duplicate identity is:

```text
source Network ID + source Node ID + sequence
```

The current frozen v0.2 cache policy is:

```text
32 entries
10000 ms lifetime
static allocation
```

The transport source address is intentionally excluded from duplicate
identity.

Sequence-number ordering for routing is a separate concern and is not
defined by this duplicate-suppression contract.
