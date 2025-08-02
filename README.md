
# Infrared Remote Decoder with MicroPython on RP2040-Zero (Tiny 2040)

This project implements an IR remote decoder using a TSOP4838 IR receiver and a Pimoroni Tiny 2040 (RP2040) microcontroller running MicroPython v1.25.0 (2025-04-15). It captures IR signals encoded in the NEC protocol, decodes them into hexadecimal codes, and activates a specific GPIO pin for 400 ms depending on the button pressed.

---

## Objective

- Capture and decode IR signals using the NEC protocol.
- Map each button to a unique 32-bit hexadecimal code.
- Trigger a corresponding digital output (GPIO high) for 400 milliseconds.

---

## Hardware

| Component              | Description                         |
|------------------------|-------------------------------------|
| Microcontroller        | Pimoroni Tiny 2040 (RP2040)         |
| IR Receiver            | TSOP4838 (demodulated at 38 kHz)    |
| IR Remote              | Generic 6-button remote (AliExpress)|
| Firmware               | MicroPython v1.25.0 (2025-04-15)    |

---

## Wiring

### IR Receiver (TSOP4838)

| TSOP4838 Pin | RP2040 GPIO | Purpose        |
|--------------|-------------|----------------|
| VCC          | 3V3         | Power supply   |
| GND          | GND         | Ground         |
| OUT          | GP26        | IR signal input|

### Output Pins (digital high for 400 ms)

| Button | RP2040 GPIO |
|--------|-------------|
| POWER  | GP0         |
| UP     | GP1         |
| DOWN   | GP2         |
| LEFT   | GP3         |
| RIGHT  | GP4         |
| MENU   | GP5         |

---

## Signal Decoding Logic

1. The TSOP4838 IR receiver outputs a digital low signal when it detects a 38 kHz modulated IR carrier.
2. Pulses are measured in microseconds using `time.ticks_us()` to reconstruct the original signal.
3. NEC protocol messages start with:
   - 9 ms HIGH (carrier ON)
   - 4.5 ms LOW (carrier OFF)
4. Each bit is encoded as:
   - Logical 0: 560 µs ON + 560 µs OFF
   - Logical 1: 560 µs ON + 1690 µs OFF
5. A full message consists of 32 bits → 4 bytes.
6. The 4-byte message is converted to hexadecimal and matched to known codes.

---

## Initial Signal Observation and Protocol Identification

To identify the protocol used by the remotes, we first captured raw IR pulse timings using MicroPython and the following simplified snippet:

```python
from machine import Pin
import time

ir = Pin(26, Pin.IN)
while True:
    pulses = []
    for _ in range(100):
        t0 = time.ticks_us()
        while ir.value() == 1:
            pass
        while ir.value() == 0:
            pass
        duration = time.ticks_diff(time.ticks_us(), t0)
        pulses.append(duration)
    print(pulses)
    time.sleep(1)
```

Using this method:

- The **AliExpress 6-button remote** consistently produced sequences starting with:
  ```
  [9100, 4500, 560, 560, 560, 1690, 560, 560, ...]
  ```
  This matched the typical NEC pattern (`9ms` + `4.5ms` header), followed by bit-encoded pulses.

- In contrast, a **FireTV remote** emitted shorter repeat signals or sequences starting with `~4500 µs`, indicating a different behavior, possibly NEC repeat codes or a non-NEC protocol.

Based on the initial match to NEC timing, we proceeded to implement NEC decoding and hexadecimal code extraction to uniquely identify each button.

---

## Known Button Hex Codes

| Button | Hex Code            |
|--------|---------------------|
| POWER  | 0x61 0xD6 0x48 0xB7 |
| UP     | 0x61 0xD6 0xD8 0x27 |
| DOWN   | 0x61 0xD6 0x58 0xA7 |
| LEFT   | 0x61 0xD6 0x20 0xDF |
| RIGHT  | 0x61 0xD6 0x60 0x9F |
| MENU   | 0x61 0xD6 0xA0 0x5F |

These codes are matched at runtime and mapped to specific output GPIO pins. Each pin remains HIGH for 400 milliseconds after a button press is detected.

---

## Runtime Behavior

- Waits for a valid NEC signal on `GP26`.
- Measures pulse timings and decodes them into a 32-bit binary code.
- If the decoded code matches a predefined entry, a specific GPIO pin is activated for 400 ms.
- Only complete signals (with a valid NEC header and 32 bits) are accepted.

---

## Files

- `main.py`: Main MicroPython script implementing decoding, mapping, and output logic.
- `README.md`: Technical documentation of hardware setup, signal structure, and usage.

---

## Future Improvements

- Implement a **learning mode**, which allows the system to temporarily capture and overwrite the six predefined IR codes via a special button combination. This will enable dynamic reassignment of remote buttons to specific GPIO outputs without changing the firmware.

---

## Connection Diagram (Textual)

```
  IR Remote
      ↓
  [TSOP4838] —— GP26 (IR IN)
      |
     VCC → 3.3V
     GND → GND

  [RP2040-Zero]
      GPIO0 → POWER
      GPIO1 → UP
      GPIO2 → DOWN
      GPIO3 → LEFT
      GPIO4 → RIGHT
      GPIO5 → MENU
```

---

## Author

LUIS VILLAVICENCIO  
Scientific programmer with expertise in Python, electronics, and embedded systems  
Date: August 2025
