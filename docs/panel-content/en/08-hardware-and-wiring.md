<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The pin map is temporary</div><div style="line-height:1.6;color:inherit">Drive's own circuit board has not been manufactured yet. The pin numbers below were chosen from non-conflicting pins on the development board. When the real board arrives it will be corrected <strong>in a single file</strong>; the rest of the code will not change.</div></div>

![Vehicle wiring diagram: power, ignition, CAN, antennas](GORSEL: en/vehicle-wiring.png)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Vehicle wiring diagram. The pin map is temporary; final wiring will be settled with Drive's own board.</em></div>

## Connection points

"GPIO" is the name for the processor's externally exposed pins; each one is numbered.

| Function | GPIO | Direction | Note |
|---|---|---|---|
| CAN data out | 4 | output | Goes to the transceiver chip (wired even though the device never writes to the bus) |
| CAN data in | 5 | input | Comes from the transceiver |
| CAN sleep line | none | none | Not used |
| Satellite receiver in | 8 | input | Position data arrives here |
| Satellite receiver out | 9 | output | Not used. No commands are sent to the receiver |
| Modem out | 17 | output | To the modem's input pin |
| Modem in | 18 | input | From the modem's output pin |
| Modem power key | 15 | output | 1.2 second pulse to power the modem on |
| Ignition input | 10 | input | From the vehicle's ignition line, through a voltage divider |
| Status LED | 48 | output | If not wired, the service never starts |

## Serial line assignment

The processor has three serial units; each is dedicated to one job:

| Unit | Use | Speed |
|---|---|---|
| UART0 | Computer connection and commands | 115200 |
| UART1 | Satellite receiver | 9600 (configurable) |
| UART2 | Modem | 115200 |

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">A transceiver chip is mandatory for CAN</div><div style="line-height:1.6;color:inherit">The processor contains a CAN controller but <strong>no electrical interface</strong>. Wiring its pins directly to the vehicle's CAN cable is neither functional nor safe. It damages both the bus and the processor. A <em>transceiver</em> (such as SN65HVD230 or TJA1050) must sit in between, with 120 ohm termination resistors at the ends of the bus.</div></div>

## Ignition input

Vehicle ignition is 12 volts, the processor input is 3.3 volts. A voltage divider and protection circuitry in between are mandatory.

Ignition information is used in three places: **report frequency** (once a minute when on, every 10 minutes when parked), **immediate reports** (the moment ignition changes), and **update timing** (only while parked). If this wire is not connected, the device thinks the vehicle is permanently parked.

## Power supply

The device is intended to be wired to the vehicle's always-on 12 volt line; ignition state is read as a separate input. Protection against reverse polarity, overvoltage and cranking transients is the board design's job.

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Power consumption has not been measured</div><div style="line-height:1.6;color:inherit">With no hardware in hand, consumption has never been measured. Power management exists in the software (deep sleep when the ignition is off, see the Power management section) but it has <strong>not been verified on the bench</strong> yet.</div></div>
