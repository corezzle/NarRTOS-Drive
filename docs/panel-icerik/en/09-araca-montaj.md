This section is for the person installing the device in a vehicle: how to tell it works after connecting the wires.

## 1. Wiring

| Wire | Where |
|---|---|
| 12 volt (+) | The always-on supply line not cut by the ignition |
| Ground (−) | Vehicle body |
| Ignition signal | The line powered with ignition → voltage divider → GPIO 10 |
| CAN H / CAN L | The vehicle's data bus → transceiver → GPIO 4 and 5 |
| Satellite antenna | Somewhere that sees the sky (under the windshield works) |
| Cellular antenna | Away from metal surfaces |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The device does NOT write to the vehicle bus</div><div style="line-height:1.6;color:inherit">It only listens. Write capability is <strong>never set up</strong> in the software, so it is impossible even by accident. A tracking device interfering with a vehicle's data bus is unnecessary and risky.</div></div>

## 2. Initial setup (over USB)

```
pio device monitor            # connect to the device console

set apn internet              # your carrier's data network name
set mqtt host broker.ornek.com
set mqtt port 1883
set ota url https://ota.ornek.com/drive/manifest
get config                    # verify
```

No need to type the IMEI: if a modem is attached, the device reads and stores the number itself.

## 3. Check connectivity

```
net status                    # should say online ("çevrimiçi")
net modem                     # if not connected: where did it stop?
time status                   # the clock should be set
mqtt status                   # should say connected ("bağlı: evet")
```

The `net modem` output tells you where the problem is: *no SIM*, *searching for network*, *settings rejected*. Each is a separate step. "The modem won't connect" alone is useless; you need to know which step it stopped at.

## 4. Check the data flow

```
can stats                     # are messages arriving from the vehicle?
gnss status                   # is there a satellite fix?
telemetry status              # is the ignition state correct?
sf peek                       # see the outgoing report with your own eyes
```

Turn the ignition on and off and watch the ignition line change in the `telemetry status` output. If it does not change, the ignition wire or the voltage divider is faulty.

## 5. Unplug the cable, watch the LED

After USB is unplugged, the status LED is your only indicator. Expected: **a very short blink every 3 seconds**, meaning everything is fine.

| If you see | What to do |
|---|---|
| Fast blinking | Missing settings. Check IMEI and SIM settings with `get config` |
| One long flash | No network. Check the antenna and the coverage |
| Two short flashes | Network is up, cannot reach the server. Address, port or password is wrong |
| Solid on | The device's own memory has a problem. Return the device |
| Completely dark | No power, the device is faulty, or it is in deep sleep (normal while parked if sleep is enabled) |

## 6. Remote check (optional)

Once the device is on the network it can also be queried without a cable:

```
mosquitto_pub -h <server> -t 'narrtos/drive/<imei>/cmd' -m 'net status'
mosquitto_sub -h <server> -t 'narrtos/drive/<imei>/cmd/reply'
```

When the vehicle is in the field with the device underneath it and nobody can reach it, this is the only way in.
