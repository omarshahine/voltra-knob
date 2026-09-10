#!/usr/bin/env python3
"""Print serial output for N seconds, then exit. Run with: uv run --with pyserial tools/serial_tail.py --seconds 10"""
import argparse, glob, sys, time
import serial

p = argparse.ArgumentParser()
p.add_argument("--port", default=None)
p.add_argument("--seconds", type=float, default=10)
p.add_argument("--baud", type=int, default=115200)
a = p.parse_args()
port = a.port or sorted(glob.glob("/dev/cu.usbmodem*"))[0]
deadline = time.time() + a.seconds
with serial.Serial(port, a.baud, timeout=0.5) as s:
    while time.time() < deadline:
        line = s.readline()
        if line:
            sys.stdout.write(line.decode("utf-8", "replace"))
            sys.stdout.flush()
