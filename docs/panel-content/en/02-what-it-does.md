Imagine a logistics company. It has a hundred trucks and constantly needs answers to these questions:

- Where is each truck right now?

- When did each driver start work, and when did they finish?

- Which vehicle is idling and burning fuel for no reason?

- Is there an engine fault?

This device is installed in every vehicle; it collects the answers and sends them to a central server. The software at the center (we call it the **fleet system**) shows the data on a map and in reports.

![End-to-end data flow: vehicle, device, server](GORSEL: en/data-flow.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>The end-to-end flow: the device collects and forwards data; decoding happens on the server.</em></div>

## Where does the device get its data?

| Source | What it learns |
|---|---|
| Satellite receiver (GNSS) | Position, speed, heading |
| The vehicle's own data bus (CAN) | Engine RPM, fuel, mileage, fault codes. The wire the vehicle's own computers talk on |
| Ignition wire | Is the vehicle running or parked |
| Cellular modem | To send the data to the center (it has a SIM card, like a phone) |

## The story of a day

Let's follow what the device does on an ordinary workday. Every technical detail in this document is part of this flow:

| Time | What happens | What the device does |
|---|---|---|
| 07:00 | Driver turns the ignition on | Reports the ignition change **immediately**, because the shift-start report depends on it |
| 07:01 | Vehicle is in an underground car park | No satellites, no network. It keeps collecting data and **writes it to its own memory** |
| 07:20 | Vehicle reaches open air | Satellites and network return. The 20 minutes of stored data is sent **with the timestamps of when it was produced** |
| 09:30 | Vehicle passes through a tunnel | Network drops, data goes to memory again; it is sent at the tunnel exit |
| 12:00 | Lunch break, ignition off | Report rate drops from once a minute to once every 10 minutes, to avoid wasting data |
| 18:00 | Vehicle in the garage, ignition off | If a software update is pending, it is installed **now**, never in the middle of a drive |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">What makes this device different?</div><div style="line-height:1.6;color:inherit">Three things: <strong>it does not lose data when there is no network</strong>, <strong>it produces correct timestamps even when it does not know the time yet</strong>, and <strong>a broken update rolls itself back</strong>. All three have the same reason: the device sits under a vehicle and nobody can reach it to intervene.</div></div>

<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px;margin:18px 0"><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Data</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">The server decodes vehicle data</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">The device forwards what it reads from the vehicle bus without processing it. Interpretation happens at the center, so adding a new signal never requires visiting the vehicles.</div></div><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Resilience</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">No data is lost without network</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">Reports produced in tunnels, underground car parks and rural areas are written to the device's memory and sent in order once the connection returns.</div></div><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Maintenance</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">Updates can roll back</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">Software that crashes on boot reverts to the previous version by itself. Nobody can reach the device under the vehicle to fix it by hand.</div></div><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Diagnostics</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">Screenless but inspectable</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">A cable, remote messages and a single LED. Three channels, all three answering the question "why is it not working".</div></div></div>
