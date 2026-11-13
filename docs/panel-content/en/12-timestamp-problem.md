**In short:** the device has no battery-backed clock chip. It loses the time when power is cut and needs the internet or satellites to learn it again. Yet most reports are produced exactly when there is no connectivity. This is the most carefully thought-through topic in this document.

![Timestamp decision flow](GORSEL: en/timestamp.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>The stamp decision at send time: all four outcomes are deliberate.</em></div>

## How the problem arises

Picture a truck in an underground car park. The driver turns the ignition on, the device starts, but it sees no satellites and gets no signal. In other words, **it does not know what time it is.** For 20 minutes it collects data and writes it to memory. Then the vehicle reaches open air, the network returns, the time is learned.

What timestamp should that 20 minutes of data carry?

## The solution: computing backwards later

The device has two different clocks:

- **Wall clock**: the real date and time. Learning it requires connectivity, and it can jump forwards or backwards when learned.

- **Time since boot**: always known, never jumps, but carries no date information.

The device writes "I was produced at second N since boot" next to every report. When the time is learned later, this calculation is made:

```
It is now 07:20 and we are at second 1200 since boot.
This report was produced at second 60 since boot.
The difference is 1140 seconds = 19 minutes.
So the report was produced at 07:01.
```

This way, reports accumulated in the car park are sent **with the timestamps of when they were produced**, not when they were sent.

## The time can be learned from three sources

| Source | What it needs |
|---|---|
| Internet time server | A working connection |
| Satellite receiver | A satellite fix. No network needed |
| Cellular modem | Network registration |

The satellite source is especially valuable: correct stamps can be produced even where the network never reaches.

## The unsolvable case and how it is handled

There is one loose end: if the device reboots, the "time since boot" counter resets. A report written before the reboot, without a stamp, has a counter that is meaningless in the new session. Computing backwards would produce a random date.

For this reason a random identity is generated at every boot and written into the reports. If the identity does not match, the report is **dropped**; waiting would be pointless too, because that report can never be stamped and would block everything queued behind it forever.

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">If the time is unknown, no report is sent</div><div style="line-height:1.6;color:inherit">It waits in the buffer. A wrongly stamped report cannot be weeded out once it enters the fleet database: it corrupts the route, the speed report and the shift report all at once. Waiting is far cheaper, and that is what the buffer exists for anyway.</div></div>
