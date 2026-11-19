#!/usr/bin/env python3
"""Print the device log for a number of seconds: serial_log.py [seconds] [port]. Resets the board first unless --no-reset."""
import sys, time
import serial

args = [a for a in sys.argv[1:] if a != "--no-reset"]
secs = float(args[0]) if args else 30
port = args[1] if len(args) > 1 else "/dev/ttyACM0"
s = serial.Serial(port, 115200, timeout=0.2)
if "--no-reset" not in sys.argv:
    s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
end = time.time() + secs
while time.time() < end:
    try:
        b = s.read(4096)
    except serial.SerialException:
        time.sleep(0.5)
        try:
            s = serial.Serial(port, 115200, timeout=0.2)
        except serial.SerialException:
            pass
        continue
    if b:
        sys.stdout.write(b.decode("utf-8", "replace")); sys.stdout.flush()
