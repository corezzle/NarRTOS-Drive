**In short:** losing network in a vehicle is not an exception, it is *normal*. Tunnels, underground car parks, rural roads, handovers between two cell towers. During these periods the device writes reports to its persistent memory and sends them in order once the connection returns.

![Buffer behavior during a coverage outage](GORSEL: en/tampon-zaman-cizgisi.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>During an outage, reports are written to the persistent buffer and sent in order when the connection returns.</em></div>

The family's first product, the watch, had no such mechanism: without WiFi, the report was silently skipped. On a wearable a missed measurement is just a missing data point. In vehicle tracking it means **a missing route**. The vehicle disappears from the fleet screen and the speed, shift and zone reports all misbehave.

## How many reports fit?

The 2.4 MB partition takes 624 reports. For a vehicle producing one report a minute that is **about 10 hours**. When parked (one per 10 minutes) far longer.

## Why not a file system?

Instead of a computer-style file system, the memory was divided directly into fixed-size slots. Reasons: a file system costs time and memory at mount, carries corruption risk, and frequently writing small pieces is exactly its worst case. Our need, in contrast, is very simple: fixed slots that fill in order and drain in order. File names, folders, resizing, none of it would be used.

## What if power is cut?

Persistent memory has a property: if power dies before a write completes, half-written data remains. To prevent a half report from being mistaken for a valid one, the order was reversed: **the report content first, and the "this slot is occupied" marker last**.

So if power dies mid-write, the marker never appears and the slot counts as empty. A half report is never sent.

## When memory fills, which report is dropped?

**The oldest.** This is the exact opposite of the vehicle-data table's decision (there, newcomers are dropped and existing entries kept) and it is not a contradiction: there, the competitors were *different signals*, all needed. Here, they are *different times of the same information*.

The person watching the fleet screen wants the vehicle's current location. Losing a point from 10 hours ago is better than losing the current one. Rejecting the new report would make the device unable to produce live data at all.

## Deletion only after acknowledgment

A report is not deleted right after sending; the server's "received" acknowledgment is awaited. With single-step deletion, a connection dropping at exactly that moment would leave the report both unsent and deleted.

## How long does the memory last?

Persistent memory cannot be written forever; each slot survives about 100,000 erases. The math: one report a minute = 1,440 reports a day, 624 slots = 2.3 full cycles a day. 100,000 ÷ 2.3 ≈ **43,000 days**, more than a century. Negligible next to the device's lifetime.
