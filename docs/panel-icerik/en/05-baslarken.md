**In short:** this section is the explained version of Quick start. Someone who has never set up an embedded device can walk through it step by step: what you are installing and why, what you should see at each step, and what to do when something goes wrong.

![Initial setup commands on the serial console](GORSEL: en/konsol-kurulum.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Initial setup over the serial console: identity, server and workshop network are configured and a test report is produced. Device output is in Turkish.</em></div>

First, fix one thing in your mind: the device has **no screen**. The only way to talk to it is to connect over USB and type text commands. You will not tap like setting up a phone; you will type short commands, and the device will answer each one in text.

## Step 1: Prepare the computer

Two installs, both done once:

1. **PlatformIO**: the tool that builds embedded software and flashes it to the board. In a terminal:

```
pip install platformio
```

If your computer has no Python, install it from python.org first; PlatformIO downloads everything else itself.

2. **USB-serial driver**: some boards need a small driver to be recognized (depending on the bridge chip on the board, e.g. CH34x). If the computer does not see the board when plugged in, install this driver and replug the cable.

## Step 2: Download the code, build it, flash the board

```
# 1. Download the code
git clone https://github.com/corezzle/NarRTOS-Drive
cd NarRTOS-Drive

# 2. With the board plugged in, build and flash
pio run -e esp32s3_drive -t upload
```

**What you will see:** the first build takes a few minutes (compilers are downloaded) and hundreds of lines scroll by. It ends with a green **SUCCESS** and upload percentages. Later builds take seconds.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">If it says the board was not found</div><div style="line-height:1.6;color:inherit">Three suspects, try in order: the cable (some USB cables are charge-only and carry no data), the driver (the USB-serial driver from Step 1) and the port (plug the board into a different USB socket). If the board is visible but flashing does not start, hold the BOOT button on the board and try again.</div></div>

## Step 3: First conversation with the device

```
pio device monitor
```

This connects to the device's console. On every boot the board prints what it is doing line by line, ending with a line inviting you to type `help`.

Now type `help` and press Enter: the full command list appears. Type `info`: version, uptime and memory appear. **If these two work, you can talk to the device.**

## Step 4: First settings

The device ships "empty": it does not know who it is, which server to report to, or which network to use. You tell it, one command at a time. Enter the following with your own values; the device answers each with `OK`:

```
set imei 351234567890123        # device identity (not needed if a modem is attached)
set apn internet                # your SIM carrier's data network name
set mqtt host broker.ornek.com  # the server reports will go to
set mqtt port 1883
set wifi ssid Atolye Agi        # workshop WiFi name, for testing
set wifi pass sifre123          # WiFi password
get config                      # check what you wrote
```

| Setting | What it is for | Where to find it |
|---|---|---|
| `imei` | The device's identity in the fleet system | With a modem attached the device reads it itself |
| `apn` | Your SIM's data network entry name | From the SIM carrier (often "internet") |
| `mqtt host / port` | The server reports go to | From the server team |
| `wifi ssid / pass` | Workshop test connection while there is no modem | Your workshop's WiFi details |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Passwords are never printed</div><div style="line-height:1.6;color:inherit">In the <code>get config</code> output, passwords appear as <code>&lt;ayarlı&gt;</code> ("set") instead of their value. The reason: console output ends up in logs, screenshots and support tickets.</div></div>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">An unconfigured device does not run for nothing</div><div style="line-height:1.6;color:inherit">If no SIM or WiFi details were entered, the device <strong>never powers up</strong> the network hardware, so a device waiting on a bench does not waste power. <code>net status</code> reports this state as unconfigured ("ayarsız").</div></div>

## Step 5: See it working

Type these three commands in order and expect the result next to each:

| Command | What you should see | If you do not |
|---|---|---|
| `net status` | online ("çevrimiçi") | Check WiFi/SIM settings with `get config` |
| `telemetry now` | OK: report produced | Read the error line in the output; it says what is missing |
| `sf peek` | The outgoing report's content (JSON) | Seeing "no clock yet" is normal: it is sent automatically once the time is learned |

If all three pass, the chain works end to end: data is collected, a report is produced, and it is ready to send. From here the device runs on its own: one report a minute with the ignition on, one per 10 minutes while parked.

## What does the device do at boot?

If you are curious: when powered, the device starts its parts in a fixed order and prints each step to the console. The order is not arbitrary: settings first (everything reads them), the command interface right after (the only way into a misconfigured device), then the collectors, the buffer, network, clock, position, the report producer, updates and the status LED.

## Developer notes

The same source code builds for two boards:

| Environment | Board | Notes |
|---|---|---|
| `esp32s3_drive` | ESP32-S3 + 8 MB flash | The real target. No screen, so no extra RAM chip (PSRAM) |
| `esp32s3_drive_lcd128` | Corezzle Touch-LCD-1.28 | Development/test board: 16 MB flash. Its screen stays black |

The CAN, satellite and modem hardware is not in hand yet; the related code can be left out of the build with feature switches. The important part: **the services were written hardware-independent**, so they can be fed with commands and tested even without hardware.

| Switch | Default | When off |
|---|---|---|
| `ENABLE_CAN` | off | Vehicle data is fed by hand with `can inject` |
| `ENABLE_GNSS` | off | Position is fed by hand with `gnss inject` |
| `ENABLE_WIFI` | on | not applicable |
| `ENABLE_MQTT` | on | Reports pile up in the buffer, nothing is sent |
| `ENABLE_MODEM` | off | The modem cannot be selected |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Enabling an unwritten part stops the build</div><div style="line-height:1.6;color:inherit">So that someone who flips a switch by mistake does not receive a silently broken device, the relevant file contains a check that stops the compilation.</div></div>
