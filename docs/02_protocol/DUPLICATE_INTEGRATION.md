# ENP v0.2 Duplicate Suppression Integration

## Status

**Frozen and hardware-validated.**

The isolated `enp_duplicate.c` module was validated on ESP32 and the
dispatcher integration was subsequently validated end-to-end on Gateway
and Sensor hardware.

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

Duplicate suppression occurs after complete packet validation and before
service dispatch.

## Ordering

1. The complete ENP packet is validated.
2. The duplicate cache is checked and the valid packet is recorded.
3. Only a new packet is passed to the registered service.
4. A duplicate is consumed by the dispatcher and is not delivered again.

Therefore an invalid packet cannot poison the duplicate cache, while a
valid packet remains suppressed even if its service later returns an error.

## Identity

Duplicate identity is:

```text
(source Network ID,
 source Node ID,
 sequence number)
```

The transport source address is deliberately **not** part of the identity.

This is important for future multi-hop forwarding, where an originated
packet may arrive through different transport peers.

## Cache

```text
Capacity: 32 entries
Lifetime: 10000 ms
Allocation: static
Ownership: dispatcher
```

The cache has been independently tested on ESP32 for:

- first packet / duplicate behavior;
- different sequence numbers;
- different sources;
- expiration;
- `uint32_t` time wrap-around;
- full-cache oldest-entry replacement;
- clear/reset behavior.

## Hardware validation

The complete dispatcher path was tested with one valid Discovery frame
sealed once and transmitted three times using the same sequence:

```text
0x7E000001
```

Expected and observed behavior:

```text
Frame #1
    NEW → Discovery service

Frame #2, 100 ms later
    DUPLICATE → DROP

Frame #3, 11000 ms later
    NEW → Discovery service
```

The Sensor log confirmed:

```text
enp_discovery: Neighbor discovered: network=1 node=1 ...
enp_dispatcher: Dropped duplicate packet: network=1 node=1 seq=2113929217
enp_discovery: Neighbor discovered: network=1 node=1 ...
```

`2113929217` is `0x7E000001`.

Normal periodic Discovery continued throughout the test, confirming that
duplicate suppression did not disrupt the existing Discovery path.

## Frozen rules

Do not change the duplicate identity, cache semantics, or dispatcher
ordering without a new design review and validation cycle.

Sequence-number ordering/comparison for future routing is **not** defined
by this document. The sequence field is currently used as a packet
identity component for duplicate suppression.
