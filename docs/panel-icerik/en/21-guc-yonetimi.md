**In short:** when the ignition goes off, the device can finish its pending work and enter **deep sleep**. Consumption drops by orders of magnitude; even if the vehicle parks for weeks, the battery does not die because of the device. The feature ships disabled and is enabled on field devices with `set power sleep 1`.

![Power management state cycle](GORSEL: en/guc-dongusu.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>The power cycle: grace period, deep sleep, and the two wake paths.</em></div>

## Why does it exist?

The device is powered from the vehicle's battery, and running at full power with the ignition off slowly drains it. On a long park (airport, service, holiday) the field name for this is "the tracker killed my car", and there is no worse reputation for a tracking product.

## What happens when the ignition goes off?

1. The device does not sleep immediately: it waits **5 minutes**. That is enough to send the ignition report and the buffered backlog, and to install a pending update. It also prevents pointless sleep/wake cycles on short stops (a shop, traffic).

2. When the wait is over and no work remains, an `offline` notice is left with the server and the device enters deep sleep.

3. Two things wake it: **the ignition turning on, immediately**; or **the timer** when the parked report is due. On a timer wake the device produces a parked report, finishes its work within a short window and sleeps again.

## What delays sleep and what allows it

| Situation | Decision |
|---|---|
| Ignition on, or it just went off | stays awake |
| An update is downloading or waiting to install (network up) | stays awake |
| The buffer is draining (network up) | stays awake, up to a ceiling |
| No work left | sleeps |
| No network (even with a full buffer) | sleeps; the records wait in persistent memory |
| Buffer will not drain, ceiling reached | sleeps |

The decision logic lives in its own pure module and is verified with tests on a computer, because mistakes are costly in both directions: staying awake needlessly kills the battery, sleeping too early misses reports and updates.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The clock is not lost in sleep</div><div style="line-height:1.6;color:inherit">The device's timekeeping circuit (RTC) keeps running through deep sleep. A waking device does not have to learn the time again; the parked report's timestamp can be produced the moment it wakes.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Two known limits</div><div style="line-height:1.6;color:inherit"><strong>1.</strong> Unstamped records die after sleep: if the time was never learned, the buffered records could not survive any reboot anyway (see the timestamp section); sleep does not create this situation, it makes it certain. Refusing to sleep until the time arrives would kill the battery of a vehicle forgotten in a garage.<br><br><strong>2.</strong> The USB console goes dark in sleep. On a bench development device the feature should therefore stay off; a device that dozed off looks "broken".</div></div>
