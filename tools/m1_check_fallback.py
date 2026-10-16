"""Check manual sound fallback when an SD WAV filename exists but is invalid."""
import json
import argparse
import time
import urllib.parse
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument("base")
args = parser.parse_args()
BASE = args.base.rstrip("/")
NAME = "m1_invalid_fallback.wav"

def request(path, body=None):
    with urllib.request.urlopen(BASE + path, data=body, timeout=8) as response:
        return response.read()

def form(path, **fields):
    return request(path, urllib.parse.urlencode(fields).encode())

old = json.loads(request("/api/alarms"))["manualSound"]
try:
    request("/upload?filename=" + NAME, b"this is not a WAV")
    form("/api/manual-sound", sound=NAME)
    form("/api/play-now")
    time.sleep(1)
    state = json.loads(request("/status"))
    assert state["playing"] and state["file"] == "(embedded audio)", state
    print("Invalid SD WAV fell back to embedded audio", flush=True)
finally:
    form("/stop")
    form("/api/manual-sound", sound=old)
    form("/delete?filename=" + NAME)
