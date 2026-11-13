All technical terms used in the document, grouped by topic.

## Vehicle data

| Term | Meaning |
|---|---|
| **CAN** | The standard data bus in vehicles. Two wires; engine, brakes and dashboard all communicate on it. |
| **Frame** | A single message on the CAN bus: an identifier and up to 8 bytes of data. |
| **TWAI** | The name of the CAN controller in the device's processor. The controller exists, the electrical interface does not. |
| **Transceiver** | The chip that converts the processor's signal to the CAN bus's electrical form. The bus is unusable without it. |
| **J1939** | The CAN conventions used in heavy commercial vehicles. |
| **PGN** | The number of a message group in J1939. Engine RPM, speed, fuel and so on. |
| **DBC** | The file defining which bit of which message is which signal. In this project it is 5.7 MB and lives on the server. |
| **Priority bits** | The leading part of a CAN identifier; they express the message's urgency. The server-side matching is sensitive to them. |
| **Ignition** | The vehicle's running state. It determines report frequency and update timing. |

## Position

| Term | Meaning |
|---|---|
| **GNSS** | The umbrella name for satellite positioning systems. GPS is one of them; GLONASS, Galileo and BeiDou are others. |
| **NMEA** | The text format position receivers use. Every line ends with a checksum. |
| **RMC / GGA** | Two NMEA line types. RMC carries date+time, GGA the satellite count. |
| **Fix** | The receiver having seen enough satellites to compute a position. |
| **Stale fix** | A valid but old position. Wrong in a moving vehicle; older than 10 seconds is not used. |
| **Dead reckoning** | Advancing the position from the last known point while satellites are gone: distance from the vehicle's speed, turns from the gyroscope. It is an estimate; its error grows with distance and is reported together with its margin. |
| **Gyroscope** | A sensor measuring turn rate. It says how much the vehicle turned; it cannot produce a position on its own. |
| **Accelerometer** | A sensor measuring acceleration. Not used for position: its small bias grows too fast over time to survive a tunnel passage. |
| **MAC address** | Every network device's unique factory identity. Networks in the report's WiFi list are told apart by it. |
| **Signal strength (RSSI)** | How strongly a network is heard. A rough "how close" hint in position resolution. |

## Connectivity

| Term | Meaning |
|---|---|
| **IMEI** | The modem's unique 15-digit factory number. The fleet system tells devices apart by it. |
| **APN** | The entry name of the carrier's data network. If wrong, the modem registers but no data flows. |
| **AT command** | The text-based command language of modems. |
| **Coverage** | The area the cellular network reaches. |
| **PPP** | A method of carrying internet over a serial line. It makes the modem the device's network interface. |

## Messaging

| Term | Meaning |
|---|---|
| **MQTT** | A topic-based, lightweight messaging protocol. Common on IoT devices. |
| **Topic** | The address a message is sent to. Like `narrtos/drive/<imei>/telemetry`. |
| **QoS 1** | "At least once" delivery. The sender keeps the message until acknowledged; a message can arrive twice. |
| **Acknowledgment (PUBACK)** | The server's "I received the message" notice. The device does not delete a report before it. |
| **Retained message** | The server keeping the last message on a topic. A new subscriber immediately sees the latest state. |
| **Will (LWT)** | The message the server publishes on the device's behalf when the connection drops unexpectedly. |
| **Broker** | The server that takes messages from publishers and distributes them to subscribers. |

## Device and memory

| Term | Meaning |
|---|---|
| **Firmware** | The software inside the device. |
| **Buffer** | The area where unsent reports accumulate. Here it is in persistent memory, 624 reports of capacity. |
| **Persistent memory (flash)** | Memory that keeps its content without power. Its write rules have a quirk: changing data requires erasing the sector first. |
| **Settings store (NVS)** | The partition holding settings like the IMEI and the server address. |
| **A/B partitions** | Two software partitions. Updates are written to the idle one; a half-finished update cannot brick the device. |
| **Over-the-air update (OTA)** | The device updating its software without a cable. |
| **Rollback** | The device returning to the previous version when new software fails to declare itself healthy. |
| **GPIO** | The processor's numbered, externally exposed pins. |
| **UART** | The serial unit that lets two devices exchange text over wires. |
| **Deep sleep** | The standby state where the processor is almost entirely off and consumption drops by orders of magnitude. Waking means rebooting; persistent memory, settings and the clock survive. |
| **RTC memory** | The small memory region that stays powered in deep sleep. Small values that must survive between sleeps, like the sleep counter, live here. |
| **Watchdog** | The counter that notices a stuck task and reboots the device. Enabled because nobody can intervene in a vehicle. |
| **Wall clock / monotonic clock** | The first is the real date-time (learning it needs connectivity), the second is time since boot (always known but carries no date). |
