**In short:** the device is useless on its own. Two services must face it: one that receives reports and writes them to the database, and one that announces software updates. This section describes what the server team needs to do.

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Everything below is the device's current assumption</div><div style="line-height:1.6;color:inherit">It is not what was agreed with the fleet team. Once addresses, channel names and authentication are settled, <strong>a single file</strong> changes on the device. The items that need settling are listed at the end of the section.</div></div>

## Message channels

The device uses a messaging protocol called MQTT. Messages are sent to addresses called "topics". The topic name starts with `narrtos/drive`, followed by the device's IMEI:

| Topic | Direction | Content |
|---|---|---|
| `narrtos/drive/<imei>/telemetry` | device → server | Report |
| `narrtos/drive/<imei>/status` | device → server | `online` / `offline` |
| `narrtos/drive/<imei>/cmd` | server → device | Command |
| `narrtos/drive/<imei>/cmd/reply` | device → server | Command reply |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">How are "the device went silent" and "the vehicle stopped" told apart?</div><div style="line-height:1.6;color:inherit">While connecting, the device leaves a will with the server: "if my connection drops unexpectedly, publish <code>offline</code> on my behalf." When the messaging server notices the drop, it publishes this itself.<br><br>So the fleet screen can see the difference between "the vehicle parked and stopped reporting" and "the device is unreachable" without building extra timeout logic.<br><br>A device with power management enabled sleeps <strong>deliberately</strong> while parked: it publishes <code>offline</code> before sleeping, wakes briefly at the parked report period to send its report, then sleeps again. A parked device appearing mostly offline is therefore not a fault.</div></div>

## What the report-receiving service must do

1. Subscribe to `narrtos/drive/+/telemetry` with **acknowledged delivery** (QoS 1). The device does not delete a report until acknowledged; a receiver that never acknowledges fills the device's memory.

2. Validate the incoming report: the IMEI inside must match the IMEI in the topic name.

3. Decode the raw vehicle data with the definition file. The device sends it unprocessed.

4. Convert the timestamp from text to a date type, because database queries compare dates.

5. Write the report to the relevant vehicle collection.

6. Compute the derived information: vehicle status, idle duration, running duration. The device does not send them.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">How is the <code>wifi</code> list in a report resolved?</div><div style="line-height:1.6;color:inherit">Reports produced indoors may carry a <code>wifi</code> field: a MAC + signal list of nearby networks. The watch product's server software already contains a ready service that converts this list into a position (geolocation.service.js): Ichnaea/beaconDB compatible, switches to the Google Geolocation API via the <code>GEOLOCATE_URL</code> and <code>GEOLOCATE_KEY</code> settings, and caches results. The resolved position should be used as <em>confirmation</em> of the estimated position, not as a measured one.</div></div>

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The same report can arrive twice</div><div style="line-height:1.6;color:inherit">If the acknowledgment does not arrive in time, the device <strong>keeps the report and sends it again</strong>. Repeating was chosen over losing. The receiving service must deduplicate on the device identity + timestamp pair. Otherwise routes grow duplicate points.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Reports can arrive late</div><div style="line-height:1.6;color:inherit">As the buffer drains after a network outage, reports produced hours earlier arrive back to back. The timestamp in a report is <strong>the moment of production, not the moment of arrival</strong>. The receiving service must not assume "arrived now = happened now"; ordering must follow the stamp.</div></div>

## The update-announcing service

The device asks every 6 hours:

```
GET <address>?device_id=<imei>&current=<version>&product=drive
```

Expected response:

```
{
  "version": "0.2.0",
  "version_code": 200,
  "url": "https://ota.ornek.com/drive/0.2.0.bin",
  "size": 924235,
  "notes": "CAN table fix",
  "forced": false
}
```

| Field | Required | Behavior |
|---|---|---|
| `version_code` | yes | Installed if **greater** than the device's; equal or smaller is not installed |
| `url` | yes | Address of the firmware file |
| `version`, `size`, `notes` | no | Informational |
| `forced` | no | If `true`, the parked window is not awaited |

A response missing a required field is rejected, because a half notice must not start an installation.

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The update address must be separate from the other product's</div><div style="line-height:1.6;color:inherit">If the same address is shared, the watch downloads vehicle firmware. The device sends <code>product=drive</code> to be distinguishable, but <strong>the reliable separation must happen on the server</strong>: a separate address, or a mandatory filter on this parameter.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Use the "urgent" flag carefully</div><div style="line-height:1.6;color:inherit"><code>forced: true</code> can reboot the vehicle <strong>mid-drive</strong> and tracking is cut for about 30 seconds. It should be used only for genuinely critical patches.</div></div>

## What needs settling with the fleet team

1. **Server address and port.** Will an encrypted connection be used? If so, how will the certificate be provided?

2. **Authentication.** Username/password, certificate, or token? The fields exist on the device but are empty.

3. **Topic names.** The names above are the device's assumption.

4. Will IMEI be accepted as the **device identity**?

5. **Batch delivery.** While the buffer drains, reports go one by one. If the server accepted lists, it would be far more efficient.

6. **The update address** and where the firmware files will be hosted.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">A finding that must reach the fleet team</div><div style="line-height:1.6;color:inherit">The vehicle-data matching on the server is <strong>sensitive to the message's priority bits</strong>. On a vehicle broadcasting the same signal with a different priority, the data is not decoded, and this happens <em>silently</em>. The server raises no error. The device forwards the identifier completely unmodified, so the problem is not on the device; but since it will surface in the field as "the signal is missing", priority-independent matching should be considered on the definition-file side.</div></div>
