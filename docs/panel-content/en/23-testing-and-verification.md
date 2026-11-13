**In short:** most of the device's hardware is not in hand. The bulk of the software can be verified regardless, because the critical parts were written to know nothing about hardware. They are run and tested on a development computer.

## Automated tests

```
tools/host-test/run.sh
```

The code under test is **the very code** flashed to the device, not a copied imitation.

| What is tested | Checks | What it proves |
|---|---|---|
| Vehicle data table | 22 | Signal merging, table overflow |
| Buffer | 67 | Power loss, memory rules |
| Report format | 47 | Field names, timestamp math, WiFi tail |
| Position parser | 41 | Coordinate conversion, corrupted lines |
| Update notice | 28 | Version comparison |
| Modem responses | 37 | Command flow, timezone conversion |
| Vehicle speed signal | 12 | Decoding wheel speed from CAN |
| Dead reckoning | 20 | Position advancement, uncertainty growth, limits |
| Sleep decision | 16 | Both directions of the sleep/wake policy |
| **Total** | **290** |  |

The tests imitate persistent memory and **enforce the real memory's rules**: an invalid write attempt counts as a violation. Power loss can be imitated too: power "dies" mid-write and what remains of the data is checked afterwards.

## On-device self-test (`selftest` command)

The host tests verify the software's **logic**; the `selftest` command probes the **real hardware** on the device itself. Typing `selftest` (or `test`) on the serial console runs a checklist, each line returning PASS / FAIL / WARN / SKIP.

| Row | What it probes |
|---|---|
| Board info | Chip, MAC, version, free memory |
| Memory | Flash / PSRAM / free heap |
| Store & forward | `buffer` partition, slot count, corrupt records |
| IMU · CAN · GNSS · Modem | The matching peripheral (SKIP when no hardware/build) |
| Wi-Fi scan | Nearby network count and best signal |
| Ignition · Status LED | Ignition input; briefly lights the LED |

The test runs **on command, not at boot** — it never affects operation in the vehicle. It uses only read-only status queries and existing safe actions (Wi-Fi scan, LED test); no running service is stopped. Peripherals without hardware yet show **SKIP** — the framework is ready and runs automatically once a board arrives.

## Verified / not verified on a real device

| Path | Status |
|---|---|
| USB configuration, settings persistence | verified |
| WiFi, clock, report delivery and acknowledgment | verified |
| Buffer and timestamp math | verified |
| Remote commands | verified |
| Vehicle bus (CAN) | no transceiver |
| Satellite receiver | no module |
| Modem | no modem |
| Update rollback | no server |
| Gyroscope (dead reckoning) | no sensor; the math was tested on a computer |
| WiFi scanning | verifiable on the bench once a board is attached, pending |
| Deep sleep and wake | bench verification pending |

## End-to-end verification

A report actually sent by the device was run through the fleet system's own decoder: **30 vehicle signals** decoded correctly (including engine RPM, engine hours and vehicle speed), the timestamp converted to a valid date, and the "unknown" values were read exactly as the server expects.
