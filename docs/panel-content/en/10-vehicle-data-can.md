**In short:** the vehicle's own computers exchange thousands of messages per second. The device listens to them but does *not interpret* them. It forwards them raw to the server. The server decodes them with a 5.7 MB definition file: "this message is engine RPM, that one is fuel level".

## Why doesn't the device decode?

Three reasons:

- **The definition file does not fit on the device.** 5.7 MB, 47 thousand lines. The device's total memory is 8 MB.

- **Definitions change.** When a new vehicle model or a new signal is added, the file on the server is updated. If it lived on the device, every vehicle in the field would need a visit.

- **Raw data loses nothing.** If the device filtered, a signal needed later could never be recovered.

The format the device sends: the message identifier followed by the data bytes.

```
"0x18FEE500,01,02,03,04,05,06,07,08"
```

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The identifier must be written with exactly 8 digits</div><div style="line-height:1.6;color:inherit">The server finds the definition entry by cutting specific characters of this text. If the digit count differs, it <strong>matches the wrong signal and raises no error at all</strong>. The server mistakes wrong data for right. Short identifiers are therefore zero-padded: <code>0x00000123</code>, not <code>0x123</code>.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">The message's priority bits must be preserved</div><div style="line-height:1.6;color:inherit">The first digits of a CAN identifier express the message's urgency. The server-side matching is sensitive to this too: the same signal sent with a different priority finds no entry in the definition file and is lost silently. The device therefore forwards the identifier <strong>completely unmodified</strong>.</div></div>

## Why keep each signal's latest value instead of everything?

On the vehicle bus every signal repeats regularly, some a hundred times per second. If the device queued up every incoming message, memory would fill within seconds and the report would carry a random slice of the bus.

What the server wants is each signal's **current value**, not its history. So the device keeps only the *latest* message per signal identifier. The table takes 96 identifiers; a typical vehicle broadcasts between 30 and 80 distinct signals, a comfortable margin.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">When the table fills, new signals are dropped, existing ones kept</div><div style="line-height:1.6;color:inherit">The other way around, a rare but important signal (a fault code, say) would be evicted for the sake of a constantly repeating one. Dropped signals are counted and visible with <code>can stats</code>. A growing count means the table should be enlarged.</div></div>
