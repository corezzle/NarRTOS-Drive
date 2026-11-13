All of these are defined in a single configuration file and can be changed at build time.

## Data limits

| Limit | Value | When exceeded |
|---|---|---|
| Vehicle signals tracked at once | 96 | New signals are dropped and counted |
| Vehicle messages per report | 64 | The 64 most recent go |
| Buffer capacity | 624 reports (~10 hours) | The oldest report is deleted and counted |
| Report size (compact) | at most 933 bytes (WiFi list included) | no limit |
| Report size (text) | ~2,900 bytes | fits the 3,456 byte area (measured) |
| Reports in flight at once | 1 | Order is preserved; a full buffer drains slowly |
| Acknowledgment timeout | 15 seconds | The report is kept and retried |
| Command line length | 256 bytes (USB) / 192 (remote) | The line is discarded |

## Durations

| What | Duration |
|---|---|
| Report frequency (running / parked) | 1 minute / 10 minutes |
| Connection retry | 5 seconds → 5 minutes (growing) |
| Position freshness limit | 10 seconds |
| Update check | 6 hours |
| New firmware's proving period | 5 minutes |
| Stuck-task watchdog | 10 seconds |

## Memory

| Resource | Used | Total |
|---|---|---|
| Working memory (RAM) | 58 KB | 320 KB (18%) |
| Firmware size | 952 KB | 2.8 MB (33%) |

An extra RAM chip (PSRAM) is **not used**: with no screen it is unnecessary, and the buffer lives in persistent memory.
