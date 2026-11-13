A 15 minute roadmap for someone holding the device for the first time. Each step says what you should see at the end; details and troubleshooting are in the next section.

1. **Prepare.** You need three things: a computer, a USB cable and the ESP32-S3 board. Plug the board into the computer over USB.

2. **Install the build tool** (done once): type `pip install platformio` in a terminal.

3. **Download and flash the code:**

```
git clone https://github.com/corezzle/NarRTOS-Drive
cd NarRTOS-Drive
pio run -e esp32s3_drive -t upload
```

The first time takes a few minutes; at the end you should see **SUCCESS**.

4. **Talk to the device:** type `pio device monitor`. The device's boot messages scroll by, ending with a line inviting you to type `help` (in Turkish).

5. **Try it:** type `help` for the command list, then `info` for version and status. If these two work, the installation is done.

6. **Configure and produce the first report:** follow the "First settings" steps in the next section.
