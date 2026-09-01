# IG-F.7.8 — Multi-Hop Application Delivery Validation

**Status:** PASS — end-to-end hardware validated  
**Date:** 2026-09-01  
**Topology:** Gateway Node 1 → Relay Node 2 → Sensor Node 3  
**Transport:** ESP-NOW  
**Application packet type:** `ENP_PACKET_APPLICATION` / type `6`

## 1. Objective

Validate production end-to-end delivery of an ENP application packet from the
Gateway to the Sensor through an intermediate Relay.

The validation covers discovery, direct-route availability, routing, ESP-NOW
transport submission, relay forwarding, local destination recognition, dispatcher
registration, and application delivery.

## 2. Initial failure

The Relay received application packets for destination `1/3` but forwarding failed
with `ESP_ERR_NOT_FOUND`.

Read-only inspection established that Discovery maintained the neighbour table but
did not promote discovered direct neighbours into the routing table. Consequently,
Node 3 could be known as a neighbour without an ACTIVE direct route.

## 3. Discovery-to-routing integration

A minimal production integration was added at the Discovery boundary.

After successful neighbour update, the discovered direct neighbour is synchronized
to the routing table as a one-hop route:

- destination = discovered neighbour;
- next hop = discovered neighbour;
- metric = 1 hop;
- state = ACTIVE.

Subsequent discovery refreshes update the existing route. This removes the
previous missing-route condition without changing forwarding semantics.

## 4. ESP-NOW transmission observation

A post-`esp_now_send()` diagnostic was added to observe the immediate ESP-NOW API
result independently of higher-level submission acceptance.

Validation confirmed `esp_now_send() = ESP_OK` for both:

- Gateway → Relay application transmission;
- Relay → Sensor forwarded application transmission.

## 5. Local application dispatch

The Sensor originally received the packet but local dispatch failed because no
service was registered for packet type `6`.

A minimal IG-F.7.8 application service was registered for
`ENP_PACKET_APPLICATION`. The service only consumes and logs locally delivered
application packets; it does not alter routing, discovery, maintenance, or
transport behaviour.

## 6. Final validated path

```text
Gateway Node 1
    |
    | application packet, type 6
    | ESP-NOW send = ESP_OK
    v
Relay Node 2
    |
    | receive
    | route lookup destination 1/3
    | next-hop resolution 1/3
    | forward
    | ESP-NOW send = ESP_OK
    v
Sensor Node 3
    |
    | receive
    | destination is local
    | dispatcher
    v
IG-F.7.8 application service
```

## 7. Result

All IG-F.7.8 validation boundaries passed:

- Discovery: PASS
- Direct-route synchronization: PASS
- Gateway route selection: PASS
- Gateway ESP-NOW transmission: PASS
- Relay reception: PASS
- Relay route lookup: PASS
- Relay forwarding: PASS
- Relay ESP-NOW transmission: PASS
- Sensor reception: PASS
- Local destination recognition: PASS
- Dispatcher registration: PASS
- Application consumption: PASS

**IG-F.7.8 is hardware validated end-to-end.**

## 8. Scope

This validation record documents the IG-F.7.8 integration increment. Existing
frozen milestones remain frozen. IG-F.7.8 adds validated production integration
at the Discovery → Routing and local application-dispatch boundaries; it does not
replace or redefine the existing reliability or route-repair contracts.
