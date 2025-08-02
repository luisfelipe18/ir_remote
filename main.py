from machine import Pin
import time

ir_pin = Pin(26, Pin.IN)

# PINOUT
outputs = {
    "POWER": Pin(0, Pin.OUT),
    "UP": Pin(1, Pin.OUT),
    "DOWN": Pin(2, Pin.OUT),
    "LEFT": Pin(3, Pin.OUT),
    "RIGHT": Pin(4, Pin.OUT),
    "MENU": Pin(5, Pin.OUT),
}

# READED CODES AFTER TESTING
button_codes = {
    (0x61, 0xD6, 0x48, 0xB7): "POWER",
    (0x61, 0xD6, 0xD8, 0x27): "UP",
    (0x61, 0xD6, 0x58, 0xA7): "DOWN",
    (0x61, 0xD6, 0x20, 0xDF): "LEFT",
    (0x61, 0xD6, 0x60, 0x9F): "RIGHT",
    (0x61, 0xD6, 0xA0, 0x5F): "MENU",
}


def read_pulses(timeout_ms=100):
    pulses = []
    start_time = time.ticks_ms()

    # WAIT FOR LOW SIGNAL
    while ir_pin.value() == 1:
        if time.ticks_diff(time.ticks_ms(), start_time) > timeout_ms:
            return []

    # PULSE CAPTURE (100 CHANGES AT MOST)
    for _ in range(100):
        t0 = time.ticks_us()
        level = ir_pin.value()
        while ir_pin.value() == level:
            if time.ticks_diff(time.ticks_us(), t0) > 10000:
                return pulses
        duration = time.ticks_diff(time.ticks_us(), t0)
        pulses.append(duration)

    return pulses

def decode_nec(pulses):
    if len(pulses) < 66:
        return None

    # ARE WE USING NEC PROTOCOL?: 9ms pulse + 4.5ms space
    if not (8500 < pulses[0] < 9500 and 4000 < pulses[1] < 5000):
        return None

    bits = []
    for i in range(2, 66, 2):  # Ignores header
        low = pulses[i+1]
        if 400 < low < 700:
            bits.append(0)
        elif 1500 < low < 1800:
            bits.append(1)
        else:
            bits.append('?')

    if '?' in bits:
        return None

    hex_code = []
    for i in range(0, 32, 8):
        byte = 0
        for b in bits[i:i+8]:
            byte = (byte << 1) | b
        hex_code.append(byte)

    return hex_code

# main loop
print("Waiting for IR signal IR...")
while True:
    pulses = read_pulses()
    if not pulses:
        continue
    
    code = decode_nec(pulses)
    if code:
        key = tuple(code)
        if key in button_codes:
            name = button_codes[key]
            print("Botón:", name)
            # Sentt HIGH to the right PIN
            pin = outputs[name]
            pin.on()
            time.sleep(0.4)
            pin.off()

