**In short:** the device prepares a report at regular intervals. It contains position, speed, ignition state and the current vehicle data. The format was determined by what the fleet system expects. Not by guessing, but by reading the server code.

## Fields in the report

| Field | What | When unknown |
|---|---|---|
| `imei` | Device identity | always present |
| `dateTime` | The moment the report was produced | The report is not sent |
| `latitude`, `longitude` | Latitude, longitude | `0` |
| `speed` | Speed (km/h) | `0` |
| `direction` | Heading (degrees) | `0` |
| `ignitionState` | Is the ignition on | always present |
| `satelliteCount` | Number of satellites in view | `0` |
| `gsmSignalQuality` | Network signal strength | `-1` |
| `fuel` | Fuel level | `-1` |
| `positionSource` | Was the position measured or estimated (`gnss` / `dr` / `none`) | `none` |
| `positionAccuracy` | The position's error radius (meters) | `-1` |
| `wifi` | Nearby networks (MAC + signal strength), for indoor positioning | field omitted entirely |
| `can` | Raw vehicle data list | empty list |

Fields like idle duration, running duration and vehicle status are **computed by the server**; the device does not send them.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Special values for "unknown"</div><div style="line-height:1.6;color:inherit">In the server code, <code>-1</code> for fuel and <code>0</code> for position mean "this information is absent". Moreover the server already skips points whose position is <code>0</code> when drawing routes. So the <code>0,0</code> sent without a satellite fix does not create a wrong point on the map; it is silently filtered. The device uses these values deliberately.</div></div>

## Example report

```
{"imei":"351234567890123",
 "dateTime":"2026-08-14T13:22:32.390Z",
 "latitude":41.0123456,"longitude":29.0123456,
 "speed":54.3,"ignitionState":true,"direction":123.4,
 "gsmSignalQuality":-1,"satelliteCount":0,"fuel":-1.0,
 "positionSource":"gnss","positionAccuracy":5,
 "can":["0x18FEE500,01,02,03,04,05,06,07,08",
        "0x0CF00400,FF,7D,80,7D,7D,00,00,FF"]}
```

## Memory holds a compact binary form, not readable text

Reports are written to the buffer in a compact binary form, not as the readable text above. The conversion to text happens *at send time*. Two reasons:

1. **The timestamp.** At the moment a report is produced, the device may not know the time yet. Writing the text right away would make the stamp either wrong or the report garbage. Details in the next section.

2. **Space.** The compact form is at most 933 bytes, the text form about 2,900. The same memory holds three times more reports: roughly 30 hours of coverage loss instead of 10.

## Report frequency

| State | Frequency |
|---|---|
| Ignition on (vehicle running) | 1 minute |
| Ignition off (parked) | 10 minutes |
| Ignition change | **immediate** |

An ignition change does not wait, because the fleet system's shift start/end report relies on exactly this moment. Reporting it 10 minutes late would make that report outright wrong.
