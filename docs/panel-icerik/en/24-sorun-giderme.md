## The device does not connect

| Symptom | Cause / Fix |
|---|---|
| `net status` → "unconfigured" (ayarsız) | SIM or WiFi settings were never entered. The network hardware is deliberately kept off |
| Network up but `mqtt status` → not connected | Server address/port wrong, or the server is down. The LED gives double flashes |
| I changed the server address, no effect | It now takes effect immediately. Older versions required a reboot |
| The modem gets stuck | `net modem` says at which step: no SIM, searching for network, settings rejected |

## Reports do not go out

| Symptom | Cause / Fix |
|---|---|
| `sf peek` → "no clock yet" | Expected behavior. Sent automatically once the time arrives |
| `sf peek` → "cannot be resolved" | An unstamped report from a previous boot. Dropped when its turn comes |
| The buffer keeps filling | Check connectivity and server reachability |
| The "deleted" counter grows | The buffer is full, oldest reports are dropping. No connectivity for a long time |

## No position

| Symptom | Cause / Fix |
|---|---|
| Position is zero in the report | No fix, or the fix is older than 10 seconds. Check the satellite count with `gnss status` |
| Corrupted line count grows | Noisy wiring, or the receiver speed is set wrong |
| Satellites visible but no fix | The antenna cannot see the sky; normal indoors |

## Vehicle data does not decode

| Symptom | Cause / Fix |
|---|---|
| No signals at all on the server | The identifier format must be exactly 8 digits. Check with `can dump` |
| Some signals decode, others do not | Message priority bits. They do not match the server's definition file. Report to the fleet team |
| `can stats` → 0 incoming messages | The transceiver is not wired, or CAN is compiled out |
| The "dropped" counter grows | The vehicle broadcasts more than 96 signals; the table should be enlarged |

## Two traps for developers

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Verify after adding a build setting</div><div style="line-height:1.6;color:inherit">The build system's defaults file is ignored for newly added settings <strong>if a generated settings file already exists</strong>. The code looks right, the setting never takes effect. The fix: delete the generated files, let them regenerate, and verify the setting actually went through.</div></div>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The first build may fail for nothing</div><div style="line-height:1.6;color:inherit">The <em>first</em> run after changing a build setting can fail in the configuration stage and pass on the second. Not a blocker; run the command again.</div></div>
