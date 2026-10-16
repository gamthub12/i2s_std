"""Watch a physical SD remove/reinsert cycle through the ESP32 M0 API."""

import argparse
import hashlib
import json
import sys
import time
import urllib.parse
import urllib.request


def read(base: str, path: str) -> bytes:
    with urllib.request.urlopen(base + path, timeout=3) as response:
        return response.read()


def health(base: str) -> dict:
    return json.loads(read(base, "/health"))


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base_url")
    parser.add_argument("--filename", default="hello.txt")
    parser.add_argument("--seconds", type=int, default=180)
    parser.add_argument("--report", default="m0_sd_reinsert.json")
    args = parser.parse_args()

    base = args.base_url.rstrip("/")
    path = "/download?filename=" + urllib.parse.quote(args.filename)
    before = health(base)
    if not before["sdMounted"]:
        raise AssertionError("SD was not mounted at the start")
    original = read(base, path)
    original_hash = hashlib.sha256(original).hexdigest()
    print(f"SD mounted; baseline {args.filename}: {len(original)} bytes, SHA-256 {original_hash}", flush=True)

    seen_removed = False
    removal_time = None
    transitions = []
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        try:
            mounted = health(base)["sdMounted"]
        except OSError as error:
            print(f"Health request unavailable: {error}", flush=True)
            time.sleep(0.5)
            continue
        if not mounted and not seen_removed:
            seen_removed = True
            removal_time = time.monotonic()
            transitions.append("removed")
            print("SD removed: /health sdMounted=false", flush=True)
        elif mounted and seen_removed:
            transitions.append("reinserted")
            print("SD reinserted: /health sdMounted=true", flush=True)
            break
        time.sleep(0.5)

    report = {
        "file": args.filename,
        "bytes": len(original),
        "sha256Before": original_hash,
        "transitions": transitions,
    }
    if transitions == ["removed", "reinserted"]:
        report["unmountedSeconds"] = round(time.monotonic() - removal_time, 2)
        after = read(base, path)
        report["sha256After"] = hashlib.sha256(after).hexdigest()
        report["readMatches"] = after == original
        print(f"Read after reinsert: {'MATCH' if after == original else 'MISMATCH'}", flush=True)
    else:
        report["readMatches"] = False
        print("Timed out before observing both SD states", flush=True)

    with open(args.report, "w", encoding="utf-8") as file:
        json.dump(report, file, indent=2)
        file.write("\n")
    if not report["readMatches"]:
        raise AssertionError("SD remove/reinsert validation did not complete")


if __name__ == "__main__":
    main()
