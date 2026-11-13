**In short:** the device is driven by text commands. The same commands can be sent from two places: from a computer over the USB cable, or remotely when connectivity is up.

![pos status and power status output on the serial console](GORSEL: en/konsol-teshis.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Field diagnostics: pos status shows the switch to estimation in a tunnel, power status shows the sleep decision. Device output is in Turkish.</em></div>

## Settings commands

| Command | What it does |
|---|---|
| `set imei <number>` | Device identity |
| `set mqtt host\|port\|user\|pass` | Server settings |
| `set wifi ssid\|pass` | Workshop WiFi (names with spaces allowed) |
| `set net backend` | Use the modem or WiFi |
| `set apn <name>` | The carrier's data network name |
| `set power sleep <0\|1>` | Deep sleep when ignition goes off (should be 1 in the field) |
| `set ota url <address>` | Update address (empty = updates disabled) |
| `set can bitrate <speed>` | Vehicle bus speed |
| `set gnss baud <speed>` | Satellite receiver speed |
| `get config` | Show all settings |
| `config clear` | Return to factory settings |

## Status commands

| Command | What it shows |
|---|---|
| `info` | Version, uptime, memory, network, buffer state |
| `net status` | Connectivity state |
| `net modem` | Which step the modem is at, signal strength, IMEI |
| `mqtt status` | Server connection and counters |
| `time status` | The clock and where it was learned from |
| `gnss status` | Fix, satellite count, counters |
| `pos status` | Position source (satellite/estimate), error radius, gyroscope |
| `wifi scan` · `wifi status` | Scan nearby networks / scan counters |
| `sf stats` | Buffer occupancy and remaining capacity |
| `can stats` · `can dump` | Vehicle data table |
| `telemetry status` | Report frequency, ignition state |
| `ota status` | Version and update state |
| `power status` | Sleep decision, wake cause, sleep count |
| `led status` | What the LED is showing |
| `selftest` | Hardware checklist — probes every peripheral in turn (see Test and verification) |

## Hardware-free test commands

The CAN transceiver, satellite receiver and modem are not in hand. These commands **bypass the driver layer entirely** and feed data straight into the services. Hand-fed data is indistinguishable from real data inside the system.

```
can inject 0x18FEE500,01,02,03,04,05,06,07,08   # vehicle data
gnss inject $GPRMC,...*XX                       # position
telemetry ignition 1        # turn ignition on, a report is produced immediately
telemetry now               # produce a report right now
sf peek                     # show the outgoing report
net offline 1               # simulate network loss
sf stats                    # is the buffer filling?
net offline 0               # network back, the buffer drains
```
