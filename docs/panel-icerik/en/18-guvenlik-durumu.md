**In short:** this section describes not what is protected but **what is currently unprotected**. The device is in development and the security layer has not been written yet. What must be closed before the field is listed below.

## Channels

| Channel | Encryption | Authentication | Risk |
|---|---|---|---|
| USB console | none | none | Physical access required; the device is inside a vehicle |
| Report delivery | none | none | Position and vehicle data flow in plain text |
| Remote commands | none | none | **The most serious gap.** See below |
| Software updates | none | none | Unsigned firmware download |
| On-device settings | none | not applicable | If the memory is read out, passwords are plain text |

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The remote command channel is unprotected</div><div style="line-height:1.6;color:inherit"><strong>Anyone</strong> who can connect to the messaging server can send commands to the device. The IMEI in the topic name provides addressing, not security.<br><br>A malicious actor could factory-reset the device, redirect reports to another server, or cut tracking by rebooting it continuously. Until authentication is added, this channel must only be used on a trusted network.</div></div>

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Software updates are unsigned</div><div style="line-height:1.6;color:inherit">Firmware is downloaded over an unencrypted connection with no signature check. Anyone who can get in the middle can install their own software.<br><br>The rollback mechanism does <strong>not</strong> prevent this: rollback protects against software that crashes at boot; malicious but working software declares itself healthy. What is needed is an encrypted connection <em>and</em> signature verification.</div></div>

## Protections that exist today

- **The device does not write to the vehicle bus.** Write capability is never set up, so even a compromised device cannot interfere with the vehicle. The most valuable protection on this list.

- **Passwords are never printed to the console.** Console output ends up in logs and support tickets.

- **Rollback**: against updates that crash at boot (not against malice).

- **Downgrade protection**: if the server announces an older version, it is not installed.

## What must be closed

1. **Encrypted connections**: for both report delivery and updates

2. **Server authentication**: the fields exist on the device, currently empty

3. **Firmware signing**

4. **Encrypting the settings**

5. **Command authorization**: at minimum, the ability to disable destructive commands remotely

The first two are planned as upcoming work.
