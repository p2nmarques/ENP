# ENP API Guidelines

**ESP Network Protocol**

Version: 0.2.0

Status: Draft

---

# Purpose

This document defines the API design principles and coding conventions used throughout the ENP project.

Its goals are to:

- Keep the public API small and consistent.
- Encourage modular development.
- Minimize coupling between components.
- Make ENP easy to extend and maintain.
- Preserve backward compatibility whenever possible.

These guidelines apply to all future ENP modules.

---

# Design Philosophy

ENP follows a layered architecture.

```
Application
        │
        ▼
ENP Services
        │
        ▼
ENP Core
        │
        ▼
Transport
        │
        ▼
ESP-IDF
```

Each layer communicates only with the layer immediately below it.

---

# API Levels

ENP distinguishes three API levels.

## Public API

Stable.

Applications are expected to use these headers.

```
enp.h

enp_context.h

enp_transport.h

enp_protocol.h

enp_config.h

enp_types.h

enp_version.h
```

Changes to these files should remain backward compatible whenever possible.

---

## Experimental API

These headers are public while ENP is under active development.

```
enp_node.h

enp_neighbor.h

enp_network.h
```

These APIs may change before ENP 1.0.

---

## Internal API

Internal headers must never be used directly by applications.

Examples include:

```
dispatcher/

discovery/

routing/

management/

heartbeat/

stats/

utils/
```

These modules may change without notice.

---

# Public Entry Point

Applications should include only:

```c
#include "core/enp.h"
```

Internal ENP modules should include only the headers they require.

Example:

```c
#include "enp_context.h"
#include "enp_transport.h"
```

Avoid including `enp.h` inside ENP implementation files.

---

# Header Layout

All public headers shall follow the same structure.

```c
/**
 * @file
 * @brief
 */

#ifndef ENP_XXX_H
#define ENP_XXX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */

/* Macros */

/* Types */

/* Public API */

#ifdef __cplusplus
}
#endif

#endif
```

This layout shall remain consistent across the project.

---

# Naming Conventions

## Functions

Public functions shall begin with:

```
enp_
```

Example:

```c
enp_context_init()

enp_transport_send()

enp_packet_verify()

enp_dispatcher_register()
```

---

## Types

Structures and typedefs shall end with `_t`.

Examples:

```c
enp_context_t

enp_packet_t

enp_node_t

enp_transport_t
```

---

## Enumerations

Enumeration types shall end with `_t`.

Examples:

```c
enp_role_t

enp_packet_type_t

enp_result_t
```

---

## Macros

Macros shall be uppercase.

Examples:

```c
ENP_MAX_NEIGHBORS

ENP_PROTOCOL_VERSION

ENP_PACKET_MAGIC
```

---

## Static Functions

Private functions shall be declared `static`.

Example:

```c
static esp_err_t packet_verify(...)
```

---

# Module Responsibilities

Each module owns exactly one responsibility.

Examples:

```
protocol/
    Packet encoding
    Packet decoding
    CRC

transport/
    ESP-NOW
    Wi-Fi

dispatcher/
    Packet dispatching

discovery/
    Automatic discovery

routing/
    Multi-hop routing

management/
    Management packets

stats/
    Statistics
```

Avoid combining unrelated responsibilities in a single module.

---

# Include Policy

Only include the headers required.

Prefer:

```c
#include "enp_context.h"
#include "enp_packet.h"
```

Avoid:

```c
#include "enp.h"
```

inside ENP implementation files.

---

# Dependency Rules

Dependencies shall always point downward.

```
Application

↓

Services

↓

Core

↓

Transport

↓

ESP-IDF
```

Lower layers must never depend on higher layers.

---

# Global Variables

Avoid global variables.

Applications should create exactly one:

```c
enp_context_t
```

All ENP modules receive a pointer to this context.

---

# Memory Management

ENP follows these principles:

- No hidden dynamic allocation.
- Prefer stack allocation.
- Static allocation is preferred over heap allocation.
- Any dynamic allocation must be clearly documented.

---

# Error Handling

All public functions shall return `esp_err_t`.

Never ignore return values.

Use:

```c
ESP_ERROR_CHECK(...)
```

where appropriate.

---

# Const Correctness

Parameters shall be marked `const` whenever possible.

Example:

```c
esp_err_t enp_send(
        const uint8_t *mac,
        const void *data,
        size_t length);
```

---

# Documentation

All public APIs shall be documented using Doxygen.

Example:

```c
/**
 * @brief Initialize the ENP context.
 *
 * @param context Context to initialize.
 *
 * @return
 *      - ESP_OK
 *      - ESP_ERR_INVALID_ARG
 */
```

---

# Versioning

Public APIs should evolve conservatively.

Breaking changes should occur only in a new major protocol version.

---

# File Organization

Each directory owns a namespace.

Example:

```
protocol/

    enp_packet.*

    enp_crc16.*
```

```
transport/

    enp_transport_espnow.*

    enp_transport_wifi.*
```

```
routing/

    enp_routing.*
```

This naming convention should remain consistent throughout the project.

---

# ENP Context

Applications own exactly one context.

```
enp_context_t
```

Services operate on a context but never own it.

The context represents the runtime state of one ENP instance.

---

# Object Ownership

```
enp_context_t
        │
        ├── enp_network_t
        │       │
        │       ├── Local Node
        │       └── Neighbor Table
        │
        └── Active Transport
```

Ownership should always follow this hierarchy.

---

# Transport Independence

The ENP Core must remain transport independent.

Applications and protocol services should not depend directly on ESP-NOW.

All communication with the underlying transport must occur through the transport abstraction layer.

---

# Future Compatibility

The public API should be designed with future protocol services in mind, including:

- Discovery
- Heartbeat
- Routing
- Reliability
- OTA
- Security
- Mesh Networking

New features should extend existing interfaces whenever practical instead of introducing incompatible APIs.

---

# Guiding Principles

When designing new APIs, ask:

- Does this belong in the correct layer?
- Can this responsibility be isolated?
- Is this API transport independent?
- Will this still make sense in ENP 1.0?
- Can this be tested independently?
- Does this introduce unnecessary coupling?

If the answer to any of these questions is "no", reconsider the design before implementation.

---

# Summary

ENP is designed around a simple principle:

> **Expose capabilities, hide implementation.**

A small, consistent, and well-documented API is easier to understand, easier to test, and easier to maintain than a large API exposing internal implementation details.

These guidelines should be followed by all contributors to ensure ENP remains modular, scalable, and maintainable as it evolves.

---

# Architecture First

ENP prioritizes architectural consistency over rapid feature development.

Before implementing a new feature:

1. Update the protocol specification if required.
2. Update the object model if required.
3. Review the public API.
4. Only then implement the feature.

Following this process helps ensure that ENP remains coherent as it evolves.

---

Notes:

enp_types.h shall contain only primitive protocol types and enumerations. It shall never depend on ESP-IDF, FreeRTOS, or any ENP module.

---

# Rule ENP-001

Every ENP application should include only enp.h. Public ENP headers (enp_packet.h, enp_context.h, etc.) may be included individually by advanced users, but enp.h is the canonical entry point for the library.

---