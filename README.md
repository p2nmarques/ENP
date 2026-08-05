# ENP - ESP Network Protocol

> A lightweight, modular and transport-independent networking protocol for ESP32 devices.

![Status](https://img.shields.io/badge/status-v0.2.0--draft-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.x-red)
![License](https://img.shields.io/badge/license-GPLv3.0-green)

---

## Overview

ENP (**ESP Network Protocol**) is an open-source networking protocol designed specifically for ESP32 devices.

Unlike ESP-NOW, which provides only a low-level wireless transport, ENP provides a complete networking layer including:

- Automatic node discovery
- Reliable packet delivery
- Device identification
- Routing
- Mesh networking
- Network management
- Diagnostics
- Statistics

ENP is designed to be lightweight enough for embedded systems while remaining modular and extensible.

The first transport implementation uses **ESP-NOW**, but the protocol is intentionally transport independent and may later support:

- ESP-NOW
- Wi-Fi
- Ethernet
- BLE
- LoRa
- Serial links

---

# Project Goals

The objectives of ENP are:

- Lightweight
- Modular
- Transport independent
- Reliable
- Deterministic
- Easy to debug
- Mesh capable
- ESP-IDF native

ENP is **not** intended to replace TCP/IP or MQTT.

Instead, ENP provides an efficient networking layer specifically optimized for ESP32 embedded systems.

---

# Architecture

```
+------------------------------------------------------+
|                  Applications                        |
|------------------------------------------------------|
| Gateway | Sensor | Relay | Monitor | OTA | CLI       |
+------------------------------------------------------+
|                  ENP Services                        |
|------------------------------------------------------|
| Discovery | Routing | Reliability | Management       |
+------------------------------------------------------+
|                    ENP Core                          |
|------------------------------------------------------|
| Protocol | CRC | Statistics | Timers | Utilities     |
+------------------------------------------------------+
|                    Transport                         |
|------------------------------------------------------|
| ESP-NOW                                              |
+------------------------------------------------------+
|                     ESP-IDF                          |
+------------------------------------------------------+
```

Applications never communicate directly with ESP-NOW.

All communication flows through the ENP protocol layer.

---

# Current Status

Current development branch:

```
v0.2.0
```

Implemented:

- ESP-IDF 6.x support
- Wi-Fi station
- ESP-NOW transport
- Gateway application
- Sensor application
- CRC packet validation
- ACK packets
- Statistics module
- Modular architecture

Currently in development:

- Automatic Gateway Discovery
- Dynamic peer registration
- Protocol versioning

---

# Roadmap

## v0.2

- Automatic Discovery
- Dynamic Peer Registration
- Protocol Specification
- Generic Packet Header

---

## v0.3

- Neighbor Discovery
- Heartbeats
- RSSI Monitoring
- Node Information

---

## v0.4

- Routing
- Multi-hop Forwarding
- Duplicate Suppression
- TTL

---

## v0.5

- Reliable Delivery
- Retransmissions
- Fragmentation
- Reassembly

---

## v1.0

- ESP-NOW Mesh
- OTA Updates
- Encryption
- CLI
- Diagnostics
- Power Management

---

## Repository Structure

```text
ENP/
│
├── docs/
│   ├── ENP_PROTOCOL_v0.2.md
│   └── ROADMAP.md
│
├── main/
│   ├── application/
│   │   ├── gateway.c
│   │   ├── gateway.h
│   │   ├── sensor.c
│   │   └── sensor.h
│   │
│   ├── network/
│   │   ├── wifi.c
│   │   ├── wifi.h
│   │   ├── espnow.c
│   │   └── espnow.h
│   │
│   ├── core/
│   │   ├── protocol/
│   │   ├── stats/
│   │   └── utils/
│   │
│   ├── main.c
│   └── CMakeLists.txt
│
├── Kconfig.projbuild
├── CMakeLists.txt
├── LICENSE
└── README.md
```

---

# Packet Flow

```
Sensor

↓

ENP Packet

↓

ESP-NOW

↓

Gateway

↓

ACK
```

Future versions may include routing through relay nodes.

```
Sensor

↓

Relay

↓

Relay

↓

Gateway
```

---

# Documentation

The protocol specification is available in:

```
docs/
```

including:

- ENP Protocol
- Discovery
- Routing
- Packet Format
- Architecture

---

# Building

Requirements:

- ESP-IDF 6.x
- ESP32

Clone the repository:

```bash
git clone https://github.com/pmarques-fullstack/ENP.git
```

Configure:

```bash
idf.py menuconfig
```

Build:

```bash
idf.py build
```

Flash:

```bash
idf.py flash monitor
```

---

# Design Philosophy

ENP follows a layered architecture.

```
Application

↓

ENP Services

↓

ENP Core

↓

Transport

↓

Hardware
```

This allows applications to remain independent of the underlying transport technology.

---

# License

This project is released under the GPL-3.0 license.

---

# Author

**Pedro Marques**

Creator of ENP – ESP Network Protocol.

---

# Vision

ENP aims to become an open, lightweight networking framework for ESP32 devices that provides a clean abstraction above ESP-NOW while remaining flexible enough to support future transports and advanced mesh networking capabilities.

> **Build networks, not just links.**
