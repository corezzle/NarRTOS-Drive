**In short:** the device can update its own software. The mechanism is the same as the family's first product, but the *policy* is completely different, and for a single reason: **nobody can physically reach this device.** On the watch a broken update annoys the user, and the user plugs it into a computer. The vehicle device is under a vehicle, screenless, perhaps in another city.

![A/B partitions and rollback flow](GORSEL: en/ota-rollback.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>A/B partitions and rollback: an update that cannot prove itself falls back to the previous version.</em></div>

## A broken update rolls itself back

The update is written to the idle software partition and the device switches to it. If the new software **does not declare itself "healthy"**, the device returns to the previous version at the next boot.

So what does "healthy" mean? Either of two conditions is enough, whichever happens first:

1. **A report was sent and acknowledged by the server.** The whole chain works; the strongest possible proof

2. **The software ran for 5 minutes without crashing**

Why both? With only the first, a device updated while the server was down could never declare itself healthy, would fall back at the first power cut, and would enter an **endless update loop**. A server failure is not a software defect. With only the second, software that boots but achieves nothing would count as healthy.

## When is an update installed?

**While the vehicle is parked.** Rebooting mid-drive blinds tracking for about 30 seconds and the vehicle disappears from the fleet screen.

On the watch the equivalent was "at night, while the user sleeps". A vehicle has a better signal: the ignition.

The server can mark an update as "urgent"; in that case the parked window is not awaited. A path reserved for a critical security patch.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">What if the ignition turns on during installation?</div><div style="line-height:1.6;color:inherit">No problem. Because the update is written to the idle partition, a half-finished write is never activated; the device keeps running the old software.</div></div>

## Version comparison

The device compares the version number from the server with its own. The comparison is done **numerically**, not as text, because in text comparison `"0.10.0"` comes out smaller than `"0.2.0"`, which means updating in the wrong direction.

If the server accidentally announces an older version, nothing is installed; vehicles in the field are never downgraded.
