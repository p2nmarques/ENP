# ENP v0.2 Duplicate Suppression Integration

## Status

The isolated `enp_duplicate.c` module has been validated on ESP32.

The dispatcher integration is implemented in this milestone but is
**not yet hardware-validated**.

## Receive path

```text
ESP-NOW
   ↓
Transport receive callback
   ↓
Dispatcher
   ↓
ENP packet validation
   ↓
Duplicate cache
   │
   ├── DUPLICATE → consume / ESP_OK
   │
   └── NEW
        ↓
      Service
```

## Ordering

Duplicate suppression occurs:

1. after complete packet validation;
2. before service dispatch.

This means invalid packets cannot poison the duplicate cache.

A valid packet is recorded before the service callback is invoked.

If the service later returns an error, a retransmission of the same
packet remains a duplicate for the cache lifetime.

## Identity

Duplicate identity is:

```text
(source Network ID,
 source Node ID,
 sequence number)
```

The transport source address is not part of the identity.

This is required for future multi-hop forwarding, where the same
originated packet may arrive from different transport peers.

## Cache

```text
Capacity: 32 entries
Lifetime: 10000 ms
Allocation: static
Synchronization: static FreeRTOS mutex
```

## Duplicate handling

A duplicate is intentionally consumed by the dispatcher and returns
`ESP_OK` to the transport receive path.

It is logged at `DEBUG` level to avoid flooding the normal serial
console.

## Hardware validation to perform

1. Build the integrated firmware with ESP-IDF 6.0.2.
2. Run Gateway and Sensor normally.
3. Confirm ordinary Discovery still works.
4. Replay an identical valid Discovery frame within 10 seconds.
5. Confirm the duplicate is logged by the dispatcher and does not
   generate a second Discovery-service processing event.
6. Wait beyond 10 seconds and replay the same identity.
7. Confirm it is accepted as a new packet.

The implementation must not be frozen as hardware-validated until
these tests pass.
