Drive is the second product of the NarRTOS family. The first is **NarRTOS Wear**: a wristwatch-like device with a touch screen. The two share the same engineering discipline but have **separate code repositories, separate version numbers, separate software**.

## What they share and what they do not

| Topic | Wear (watch) | Drive (vehicle) |
|---|---|---|
| User interface | Touch screen, 12 apps | **none.** No screen, no buttons, no sound |
| Internet connectivity | WiFi | Cellular modem (WiFi only in the workshop) |
| Position | Estimated from nearby WiFi networks (±19 m) | Satellites. The WiFi method does not work in a moving vehicle |
| When there is no network | The report is skipped | Written to memory, sent later |
| A broken update | The user plugs the device into USB to recover | **Rolls back by itself**, because nobody can reach it |
| Update timing | At night, while the user sleeps | While the vehicle is parked |
| Device identity | Network card address | IMEI. The fleet system keys on it |
| Diagnostics | Settings menu on the screen | Cable, remote messages, a single LED |
| Time source | Internet + clock chip | Internet + satellites + modem (no clock chip) |

## Shared code: not yet, and for a reason

The natural question: *"If the two products share this much discipline, why was the code not shared?"*

This was re-evaluated at the end of every milestone. The conclusion: **the code is not shared, the patterns are.**

The strongest candidate was the over-the-air update code. Wear's file is 710 lines; the genuinely shareable core is about 150 lines. The rest is product-specific: when an update is triggered, when it is installed, what happens when it is broken, how a device is identified. All different.

Putting a shared component across two separate repositories demands extra infrastructure: version tracking, coordinated changes, a separate test setup. For 150 lines of well-understood code that cost does not pay for itself.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The decision point is the third product</div><div style="line-height:1.6;color:inherit">Four products are planned in the family. <strong>Two products can carry a copy, four cannot.</strong> When the third product starts, the same code would be written a third time. That is the real trigger. And by then, two products' worth of experience will have shown which code actually stays stable.</div></div>

Meanwhile the sharing did happen, just **on the server rather than on the device**: the update notice format was kept identical to Wear's, so a single server can feed both products.
