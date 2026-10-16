import argparse
import sys
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument("--port", default="COM4")
parser.add_argument("--seconds", type=int, default=15)
parser.add_argument("--reset", action="store_true")
args = parser.parse_args()
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

with serial.Serial(args.port, 115200, timeout=0.25) as port:
    if args.reset:
        port.dtr = False
        port.rts = True
        time.sleep(0.15)
        port.rts = False
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(line, flush=True)
