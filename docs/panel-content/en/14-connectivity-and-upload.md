**In short:** the device can reach the internet by two different paths: a cellular modem in the field, WiFi in the workshop. The upper layer does not know which one is in use, so the same software runs in both settings.

## Two paths, one interface

| Path | Where |
|---|---|
| Cellular modem | In the field, in the vehicle. The real path |
| WiFi | In the workshop, during development |

The choice is made **by a setting**, not by rebuilding the software. The same firmware runs on the bench and in the vehicle; no need to produce two variants.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Retry intervals grow over time</div><div style="line-height:1.6;color:inherit">If the connection cannot be established, the device retries after 5 seconds; then 10, then 20, up to a maximum of 5 minutes. A fixed interval would be wrong in a vehicle: tunnels and rural areas can mean hours without network, and retrying every second wastes both power and data.<br><br>This logic sits above the connection paths, not inside them. WiFi and the modem share the same behavior and it is never written twice.</div></div>

## Remote commands

Besides sending reports, the device also listens on a channel dedicated to it. Commands sent there are processed as if typed over USB, and the reply comes back.

For a screenless vehicle device this can be the only way to intervene in the field: the vehicle is in another city, the device is underneath it, and nobody can reach it.

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The server-side contract is not final yet</div><div style="line-height:1.6;color:inherit">Which address the data goes to, with which channel names and what kind of authentication, has not been settled with the fleet team. The device currently runs on its own assumptions. All assumptions were collected <strong>in a single file</strong>; once the contract is settled only that file will change, not the service code. Details in the Server side section.</div></div>
