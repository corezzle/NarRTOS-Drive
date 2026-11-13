Eight concepts needed to follow the rest of the document. The full list is in the Glossary at the end.

| Concept | What it means |
|---|---|
| **CAN bus** | The two-wire cable the computers inside a vehicle use to talk to each other. Engine, brakes, dashboard all communicate on it. The device *listens* to this conversation. |
| **Frame** | A single message on the CAN bus. It carries an identifier and up to 8 bytes of data. Thousands pass per second. |
| **Fix** | The satellite receiver having seen enough satellites to compute a position. Without a fix, no position can be produced. |
| **Coverage** | Where the cellular network reaches. "Out of coverage" means where a phone has no signal. |
| **Buffer** | The memory area where unsent reports are held temporarily. In this device it lives in persistent memory, so it survives power loss. |
| **Timestamp** | Each report's "I was produced at this time" information. Every fleet report relies on it. |
| **Fleet system** | The software at the center. It stores the data coming from the devices, shows it on a map and produces reports. |
| **Firmware** | The software inside the device. Like an operating system on a computer; it can be updated. |
