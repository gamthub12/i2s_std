"""Observe two GPIO0 button presses through the real playback status."""
import argparse
import json
import time
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument("base")
parser.add_argument("--timeout", type=int, default=90)
args = parser.parse_args()
base = args.base.rstrip("/")

def status():
    with urllib.request.urlopen(base + "/status", timeout=4) as response:
        return json.load(response)

initial = status()["playing"]
print("READY: initial playing =", initial, flush=True)
transitions = []
last = initial
deadline = time.monotonic() + args.timeout
while time.monotonic() < deadline and len(transitions) < 2:
    try:
        current = status()["playing"]
    except Exception:
        time.sleep(0.2)
        continue
    if current != last:
        transitions.append(current)
        print("Transition:", "playing" if current else "stopped", flush=True)
        last = current
    time.sleep(0.2)
print("RESULT:", json.dumps({"initial": initial, "transitions": transitions}), flush=True)
if transitions != [True, False]:
    raise SystemExit(1)
