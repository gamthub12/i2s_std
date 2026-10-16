"""Live M1 HTTP check. Creates only temporary alarms and removes them afterwards."""
import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone


def call(base, path, data=None):
    payload = None if data is None else urllib.parse.urlencode(data).encode()
    with urllib.request.urlopen(base + path, data=payload, timeout=8) as response:
        raw = response.read()
    return json.loads(raw) if raw.startswith(b"{") or raw.startswith(b"[") else raw.decode()


def alarm(base, **overrides):
    defaults = dict(id=0, name="M1 temporary", enabled=0, hour=23, minute=59,
                    days=0, sound="", volume=50, ringSeconds=5, snoozeMinutes=1)
    defaults.update(overrides)
    return call(base, "/api/alarms/save", defaults)["id"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base")
    parser.add_argument("--wait-for-ring", action="store_true")
    parser.add_argument("--report", default="m1_result.json")
    parser.add_argument("--reboot-port")
    args = parser.parse_args()
    base = args.base.rstrip("/")
    report = {"base": base, "events": []}
    created = []
    old_manual = call(base, "/api/alarms")["manualSound"]
    try:
        page = call(base, "/alarms")
        assert "Loa báo thức" in page
        report["events"].append("alarms page loaded")
        for index in range(20):
            created.append(alarm(base, name=f"M1 temporary {index}"))
        assert len(call(base, "/api/alarms")["alarms"]) >= 20
        try:
            alarm(base, name="M1 extra")
        except urllib.error.HTTPError as error:
            assert error.code == 409, error.code
        else:
            raise AssertionError("21st alarm was accepted")
        report["events"].append("20 alarms accepted; 21st rejected")

        test_id = created[0]
        alarm(base, id=test_id, name="M1 edited", enabled=0, hour=8)
        rows = call(base, "/api/alarms")["alarms"]
        assert any(row["id"] == test_id and row["name"] == "M1 edited" for row in rows)
        report["events"].append("alarm edit persisted in API")

        for item_id in created[1:]:
            call(base, "/api/alarms/delete", {"id": item_id})
        created = created[:1]
        call(base, "/api/manual-sound", {"sound": ""})
        call(base, "/api/play-now", {})
        time.sleep(0.4)
        assert call(base, "/status")["playing"]
        call(base, "/stop", {})
        assert not call(base, "/status")["playing"]
        report["events"].append("manual play and stop returned correct status")

        if args.wait_for_ring:
            state = call(base, "/api/alarms")
            assert state["clockValid"], "Device clock is not synchronized"
            local = datetime.fromtimestamp(state["now"], timezone(timedelta(hours=7)))
            target = local.replace(second=0, microsecond=0) + timedelta(minutes=1)
            if (target - local).total_seconds() < 20:
                target += timedelta(minutes=1)
            alarm(base, id=test_id, name="M1 timed test", enabled=1,
                  hour=target.hour, minute=target.minute, ringSeconds=6)
            report["target"] = target.isoformat()
            print("Waiting for alarm at", target.isoformat(), flush=True)
            deadline = time.monotonic() + 145
            saw_ring = False
            while time.monotonic() < deadline:
                status = call(base, "/api/alarms")
                playback = call(base, "/status")
                if status["ringing"] and status["activeId"] == test_id and playback["playing"]:
                    saw_ring = True
                    report["events"].append("scheduled alarm rang with audio active")
                    break
                time.sleep(0.5)
            assert saw_ring, "Alarm did not ring"
            time.sleep(8)
            status = call(base, "/api/alarms")
            playback = call(base, "/status")
            assert not status["ringing"] and not playback["playing"]
            assert not next(row for row in status["alarms"] if row["id"] == test_id)["enabled"]
            report["events"].append("one-time alarm stopped and disabled itself")

        if args.reboot_port:
            subprocess.run([sys.executable, "-m", "esptool", "--port", args.reboot_port,
                            "run"], check=True, capture_output=True, text=True)
            deadline = time.monotonic() + 35
            while True:
                try:
                    state = call(base, "/api/alarms")
                    break
                except (urllib.error.URLError, TimeoutError):
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(1)
            assert any(row["id"] == created[0] and not row["enabled"]
                       for row in state["alarms"])
            report["events"].append("NVS alarm survived reboot")
    finally:
        call(base, "/stop", {})
        for item_id in created:
            try:
                call(base, "/api/alarms/delete", {"id": item_id})
            except urllib.error.HTTPError:
                pass
        call(base, "/api/manual-sound", {"sound": old_manual})
        with open(args.report, "w", encoding="utf-8") as file:
            json.dump(report, file, ensure_ascii=False, indent=2)
        print(json.dumps(report, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
