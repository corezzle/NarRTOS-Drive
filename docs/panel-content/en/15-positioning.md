**In short:** the satellite receiver reports position once a second as lines of text. The device reads these lines and extracts latitude, longitude, speed, heading and satellite count. The receiver is also the second time source.

## What does the receiver send?

Position receivers use a standard text format (NMEA). Each line carries one kind of information; the device cares about two:

| Line | Contents |
|---|---|
| **RMC** | Position, speed, heading, *date and time*, validity |
| **GGA** | Position, fix quality, *satellite count* |

Both are needed: RMC has no satellite count, GGA has no date (only time of day, so no timestamp can be built from it).

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">A corrupted line is discarded entirely</div><div style="line-height:1.6;color:inherit">Every line ends with a checksum. A single byte corrupted on the wire can turn <code>4807.038</code> into <code>4907.038</code>, a <strong>111 kilometer jump</strong>. On the fleet side that both triggers a speed alarm and ruins the route, and the server has no chance of weeding it out. From a line whose checksum does not match, <em>nothing</em> is taken.</div></div>

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">A stale position is worse than no position</div><div style="line-height:1.6;color:inherit">When the receiver loses satellites in a tunnel, holding on to the last known position looks tempting but is wrong: in a moving vehicle a 10-minute-old point goes out as "current position" and the server does not question it. The result is a vehicle standing still on the map while actually driving.<br><br>For this reason <strong>a fix older than 10 seconds is invalid</strong> and the report carries "no position".</div></div>

## Empty does not mean zero

Before a fix exists, the receiver leaves the fields empty. Reading empty as zero would place the vehicle in the Gulf of Guinea (latitude 0, longitude 0), and the server would take it for a valid position. The device keeps "this field is absent" and "this field is zero" strictly apart.

## Indoor position: continuing by estimation when satellites drop

While satellites are visible, position comes directly from them. When they drop in a tunnel or an enclosed car park, the device starts from the last known satellite point and **continues the position by estimation**: distance traveled comes from the vehicle's own speed data, turns come from the gyroscope. The method is called "dead reckoning" and it is what commercial trackers use for tunnels. When satellites return the estimate is abandoned, and the next outage starts fresh from that new point; accumulated error is wiped every time.

![Dead reckoning in a tunnel: growing error radius and reseeding](GORSEL: en/tunnel-dead-reckoning.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>A tunnel passage: position is advanced with speed + gyroscope, the error radius grows as a ring, and resets when satellites return.</em></div>

| Source | What it provides | Status |
|---|---|---|
| The vehicle's speed data (CAN) | Distance traveled | Implemented. A single signal is decoded: wheel speed |
| Gyroscope (ICM-42688-P) | Amount of turning | Driver ready, sensor not in hand yet |
| WiFi network scan | Coarse position confirmation | Implemented (separate section below) |

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Why an accelerometer alone is not enough</div><div style="line-height:1.6;color:inherit">Deriving position from an accelerometer requires integrating acceleration twice, and the sensor's small bias grows <strong>with the square of time</strong> in that process. Within half a minute the error reaches tens of meters; within a few minutes it is unusable.<br><br>Distance is therefore taken from <strong>the vehicle's own speed measurement</strong>: its error grows linearly with distance and stays around a few percent. The gyroscope is used only for changes of direction.</div></div>

An estimate is not a measurement, and the device does not hide that:

- **The error margin is tracked and reported.** At every estimation step the error radius is grown and written into the report.

- **A badly drifted estimate is not sent.** If the error radius exceeds 500 meters or the duration exceeds 10 minutes, the report carries "no position". The "stale position is worse than none" rule applied to estimation.

- **Without speed data, estimation never starts.** A gyroscope alone cannot produce position.

- **Gyroscope bias is measured while the vehicle stands still** and subtracted from readings. Uncorrected bias makes the heading drift even in a parked vehicle, and the estimate heads the wrong way the moment the tunnel starts. With no gyroscope at all, heading is assumed constant and the error margin grows faster.

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Estimates are distinguishable from measurements</div><div style="line-height:1.6;color:inherit">Two fields were added to the report: <code>positionSource</code> (<code>"gnss"</code> for satellites, <code>"dr"</code> for estimation, <code>"none"</code> when absent) and the error radius in meters, <code>positionAccuracy</code>. These fields do not exist in the fleet document yet; <strong>agreement with the server side is required</strong>. If the server does not record the distinction, estimated positions get processed like satellite ones and route accuracy silently degrades.</div></div>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">A limited exception to "the device does not decode vehicle data"</div><div style="line-height:1.6;color:inherit">For distance, the device decodes a single signal from the vehicle bus itself (wheel speed). The raw data still goes to the server unmodified; the exception is limited to this one signal.</div></div>

The state is watched with the `pos status` command: position source, error radius, distance covered by estimation, gyroscope bias and whether speed data is arriving. The math of this section was verified with tests on a computer; with no gyroscope or CAN transceiver in hand yet, it has **not been verified in a vehicle**.

## Extra evidence indoors: the WiFi list

The tunnel's answer is the estimation above; in other enclosed places that see no satellites (car parks, malls, inside buildings) there is one more piece of evidence. As long as position is not coming from satellites, the device scans nearby WiFi networks every 30 seconds and adds the identities (MAC) and signal strengths of the strongest 8 to the report. **The device does not resolve the position**: same pattern as the vehicle data, the raw list goes to the server and the position is resolved there against a database of known network locations.

![Indoor positioning with the WiFi list](GORSEL: en/wifi-positioning.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Indoors, the WiFi list is added to the report; the server resolves the position and the device needs no internet.</em></div>

- **The device needs no internet.** The list waits in the buffer together with the report; when a report produced in a networkless enclosed space is sent later, its position is resolved retroactively.

- **Ready-made infrastructure inside the family.** The watch product finds its position by the same method, and the resolver service on the server side is already written (beaconDB by default, switches to Google with a single setting). It does not need rewriting for Drive.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">A stale list never enters the report</div><div style="line-height:1.6;color:inherit">If more than 2 minutes passed since the last scan, the list is not attached. With the vehicle on the move, sending the networks of an enclosed space left two minutes ago as "I am here now" would mislead the server. One more application of the stale-fix rule.</div></div>

The limit is stated openly too: there is no WiFi in tunnels, the list contributes nothing there. Coarse confirmation from cell tower information will be considered when the modem hardware arrives. The scan can also be run by hand with `wifi scan`; without a module, `wifi inject` feeds the list so the record path can be exercised end to end.

## The two-digit year problem

The receiver sends the year as two digits: `26`. Is that 2026 or 1926? We did not build an assumption like "greater than 80 means 19xx", because a faulty receiver can produce a date roughly 19.6 years in the past and such an assumption would **convert that fault into a plausible date and hide it**.

Instead the parser stays honest (adds 2000) and the sanity check happens where the clock is set: every date before 2020 is rejected, and rejections are counted.
