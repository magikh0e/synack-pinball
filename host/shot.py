"""Grab a screenshot from the pinball board over serial.

    python host/shot.py out.png [--port COM4] [--pre "b" --wait 20]

Sends 'p' to the firmware, which streams the panel as RGB888 rows at 921600
baud. --pre sends serial commands first (e.g. "b" to start the bot) and
--wait sleeps before the capture so the game can get going. --reset reboots
the board first.
"""
import argparse
import time

import serial
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--port", default="COM4")
ap.add_argument("--pre", default="")
ap.add_argument("--wait", type=float, default=0)
ap.add_argument("--reset", action="store_true")
a = ap.parse_args()

s = serial.Serial(a.port, 115200, timeout=1)
if a.reset:
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)
    time.sleep(2.5)
for c in a.pre:
    s.write(c.encode())
    time.sleep(0.3)
time.sleep(a.wait)
s.reset_input_buffer()
s.write(b"p")
w = h = None
deadline = time.time() + 10
while time.time() < deadline:
    line = s.readline()
    if b"SCREEN" in line:
        _, _, w, h = line.decode().split()
        w, h = int(w), int(h)
        break
if not w:
    raise SystemExit("no SCREEN marker")
s.baudrate = 921600
s.timeout = 15
need = w * h * 3
data = s.read(need)
s.baudrate = 115200
if len(data) != need:
    raise SystemExit(f"short read: {len(data)} of {need}")
Image.frombytes("RGB", (w, h), data).save(a.out)
print("saved", a.out)
