| Milestone | Scope | Status |
|---|---|---|
| 1 | Project skeleton, memory partitions | done |
| 2 | Settings store, command interface | done |
| 3 | Vehicle data collection | done |
| 4 | Buffer, network, report delivery | done |
| 5 | Report format, clock | done |
| 6 | Position, status LED | done |
| 7 | Over-the-air updates (rollback protected) | done |
| 8 | Modem | code done, awaiting hardware |
| 9 | Indoor position (dead reckoning) | code done, awaiting hardware |
| 10 | WiFi scanning (indoor positioning) | code done, not tried in a vehicle |
| 11 | Power management (deep sleep on ignition off) | code done, not tried in a vehicle |

## Upcoming work

1. **Encrypted connections.** Report delivery and updates are currently unencrypted. Worse for updates: an unsigned channel lets an interceptor install their own firmware.

2. **Hardware verification round.** The vehicle bus, satellite receiver, modem and gyroscope paths all merely compile; none has been tried with real hardware.

## Out of our hands

- Agreement with the server side: address, topic names, authentication

- Adding the report's `positionSource`, `positionAccuracy` and `wifi` fields to the fleet document

- A separate update address for this product

- Passing the message priority finding to the fleet team
