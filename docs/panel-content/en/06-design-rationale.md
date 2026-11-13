**In short:** the software is split into four layers. Services doing the actual work on top, hardware drivers below. There is one more layer in between, and it is the most distinctive part of the project: *pure modules that know nothing about hardware*. They can be tested on a development computer, which means they can be verified without having the device in hand.

## The layers

<pre style="background:#0B1626;color:#E4F0EE;border-radius:12px;padding:16px 20px;overflow-x:auto;font-size:13px;line-height:1.5;font-family:Menlo,Consolas,monospace">+--------------------------------------------------------------+
|  SERVICES     report production, delivery, network, position, |
|               clock, updates, buffer, command interface       |
+--------------------------------------------------------------+
|  PURE MODULES data formatter, satellite sentence parser,      |
|  (tested on a update notice parser, modem response parser     |
|   computer)                                                   |
+--------------------------------------------------------------+
|  DRIVERS      CAN, satellite receiver UART, modem/WiFi,       |
|               persistent memory access                        |
+--------------------------------------------------------------+
|  CORE         settings store, central configuration           |
+--------------------------------------------------------------+</pre>

The family's first product, Wear, had an **application layer** (watch face, settings menu, gallery and so on). Drive has none, because there is no user. Its place was taken by the pure module layer.

## The path of the data

Data from three sources becomes a single report, is written to persistent memory, then sent:

<pre style="background:#0B1626;color:#E4F0EE;border-radius:12px;padding:16px 20px;overflow-x:auto;font-size:13px;line-height:1.5;font-family:Menlo,Consolas,monospace">  CAN bus            satellite rx        ignition wire
      |                   |                   |
      v                   v                   v
 96-signal          fresh fix only      report timer
 table              (older than 10 s    (ignition on: 1 min
                     is not used)        parked: 10 min)
      |                   |                   |
      +---------+---------+---------+---------+
                          |
                          v
              +-------------------------+
              |  REPORT                 |  compact binary
              |  at most 933 bytes      |  form. Converted to
              |                         |  readable text LATER
              +-------------------------+
                          |
                          v
              +-------------------------+
              |  PERSISTENT MEMORY      |  holds 624 reports
              |  (buffer)               |  survives power
              +-------------------------+  loss
                          |
                          v
              +-------------------------+
              |  CONVERT TO TEXT        |  the timestamp is
              |                         |  computed here
              +-------------------------+
                          |
                          v
                     send over the
                        NETWORK
                          |
                          v
                    ==== SERVER ====
                          |
              +-----------+-----------+
              |                       |
              v                       v
      decode vehicle data,     "received" ack
      write to database          comes back
                                      |
                                      v
                            report deleted from memory
                            (only NOW)</pre>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Two rules of the flow</div><div style="line-height:1.6;color:inherit"><strong>1. Every report goes through the buffer</strong>, even when the network is up. Reason: a path used only during failures is a path that turns out broken when you need it. When every report takes the same path, that path is exercised constantly.<br><br><strong>2. A report is not deleted until the server says "received".</strong> If we deleted right after sending, a connection dropping at exactly that moment would leave the report both unsent and deleted.</div></div>

## Division of work

The device's processor has two cores. The work is spread across them:

| Task | Core | Priority |
|---|---|---|
| CAN listening | 1 | highest |
| Network connectivity | 0 | high |
| Report production, delivery, position | 0 | medium |
| Command interface | 0 | medium |
| Updates, status LED | 0 | lowest |

CAN listening sits on its own core at the highest priority, because thousands of messages pass on the bus per second. Even if the modem or delivery work stalls for 20 seconds, no vehicle data is missed.

## Memory partitions

The device's 8 MB persistent memory is divided into parts:

```
settings     20 KB    IMEI, server address, SIM settings
software A   2.8 MB   running firmware
software B   2.8 MB   updates are written here
buffer       2.4 MB   unsent reports (624 reports)
```

**Having two software partitions is a safety choice.** An update is written to the idle partition; if it succeeds, the device switches to it. An update that dies halfway never touches the running software, so the device does not brick.

The buffer lives outside the software partitions. In other words, **an update never deletes waiting reports.**
