**In short:** no screen, no buttons, no sound. With a cable there is the console, with connectivity there are remote commands. But the installer is under the vehicle, and while the device has not joined the network yet, neither is available. The only local indicator is this LED.

![LED patterns and their meanings](GORSEL: en/led-desenleri.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>LED patterns: the more light, the worse. A healthy device gives a single blink every 3 seconds.</em></div>

The pattern language rests on a single rule: **the more light, the worse.**

| Pattern | Meaning |
|---|---|
| Solid on | The device cannot do its job (its memory has a problem) |
| Fast blinking | Missing settings |
| One long flash / 2 s | Running but no network |
| Two short flashes / 2 s | Network up, cannot reach the server |
| Very short blink / 3 s | Everything is fine |
| Completely dark | No power, the device is faulty, or it is in deep sleep (normal while parked if sleep is enabled) |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">It blinks even when healthy</div><div style="line-height:1.6;color:inherit">This makes "completely dark" a fault signal on its own, at no extra cost. Designs where healthy means "solid on" lose this distinction: a dark LED and a dead device look the same.</div></div>

**"Network up but cannot reach the server" is its own pattern**, because it points the installer at the server address instead of the antenna.

Several problems can exist at once; the deepest one is shown. A device with missing settings is also offline, but showing "no network" would send the installer hunting for antenna problems. The real issue is the setting they never entered.
