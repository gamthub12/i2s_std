"""Exercise ESP32-S3 speaker and SD endpoints without using the microphone.

Run with the ESP-IDF Python environment, for example:
    python tools/m0_validate.py http://192.168.100.167 --serial COM4

The script creates two uniquely named WAV files on the card and deletes only
those files. Hearing the speaker and card removal still require a person.
"""

import argparse
import hashlib
import io
import json
import math
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import wave


def wav_bytes(rate: int, channels: int, seconds: int = 5) -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        frames = bytearray()
        for i in range(rate * seconds):
            sample = int(10000 * math.sin(2 * math.pi * 440 * i / rate))
            frames.extend(sample.to_bytes(2, "little", signed=True) * channels)
        wav.writeframes(frames)
    return output.getvalue()


class Device:
    def __init__(self, base: str):
        self.base = base.rstrip("/")

    def request(self, method: str, path: str, body: bytes | None = None) -> bytes:
        request = urllib.request.Request(
            self.base + path,
            data=body,
            method=method,
            headers={"Content-Type": "application/octet-stream"} if body is not None else {},
        )
        with urllib.request.urlopen(request, timeout=15) as response:
            return response.read()

    def json(self, path: str) -> dict | list:
        return json.loads(self.request("GET", path))

    @staticmethod
    def file_path(endpoint: str, name: str) -> str:
        return f"/{endpoint}?filename={urllib.parse.quote(name)}"


class SerialLog:
    def __init__(self, port: str | None):
        self.port = port
        self.lines: list[str] = []
        self.stop = threading.Event()
        self.thread: threading.Thread | None = None
        self.conn = None

    def __enter__(self):
        if self.port:
            import serial

            self.conn = serial.Serial(self.port, 115200, timeout=0.2)
            self.conn.dtr = False
            self.conn.rts = False
            self.thread = threading.Thread(target=self._read, daemon=True)
            self.thread.start()
        return self

    def __exit__(self, *_args):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=1)
        if self.conn:
            self.conn.close()

    def _read(self):
        while not self.stop.is_set():
            line = self.conn.readline().decode("utf-8", "replace").strip()
            if line:
                self.lines.append(line)

    def playback_starts(self, name: str) -> int:
        return sum(f"=== Playing SD file: /sdcard/{name} ===" in line for line in self.lines)

    def panic_context(self) -> list[str]:
        for index, line in enumerate(self.lines):
            if "Guru Meditation" in line:
                return self.lines[index : index + 18]
        return []


def wait_until(predicate, timeout: float, label: str):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError(f"Timed out waiting for {label}")


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base_url", help="Device URL, e.g. http://192.168.100.167")
    parser.add_argument("--serial", help="Optional serial monitor port, e.g. COM4")
    parser.add_argument("--cycles", type=int, default=50)
    parser.add_argument("--report", help="Write the machine checks as JSON")
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error("--cycles must be positive")

    device = Device(args.base_url)
    suffix = uuid.uuid4().hex[:8]
    sounds = {
        f"m0_mono_{suffix}.wav": wav_bytes(16000, 1),
        f"m0_stereo_{suffix}.wav": wav_bytes(44100, 2),
    }
    uploaded = []
    results = {"baseUrl": device.base, "cyclesRequested": args.cycles, "files": {}}

    with SerialLog(args.serial) as serial_log:
        try:
            before = device.json("/health")
            if not before["sdMounted"]:
                raise AssertionError("SD card is not mounted")
            results["healthBefore"] = before

            for name, body in sounds.items():
                device.request("POST", device.file_path("upload", name), body)
                uploaded.append(name)
                files = device.json("/list")
                if not any(item["name"] == name and item["size"] == len(body) for item in files):
                    raise AssertionError(f"Uploaded file missing or wrong size: {name}")
                downloaded = device.request("GET", device.file_path("download", name))
                expected = hashlib.sha256(body).hexdigest()
                actual = hashlib.sha256(downloaded).hexdigest()
                if expected != actual:
                    raise AssertionError(f"SHA-256 mismatch for {name}")
                results["files"][name] = {"bytes": len(body), "sha256": actual}
                print(f"SD upload/read verified: {name}, {len(body)} bytes")

            for name in sounds:
                count = serial_log.playback_starts(name)
                device.request("POST", device.file_path("play", name))
                wait_until(lambda: device.json("/status")["file"].endswith(name), 3, name)
                if args.serial:
                    wait_until(lambda: serial_log.playback_starts(name) > count, 3, "playback log")
                time.sleep(1.2)  # Time for a person to hear each test tone.
                device.request("POST", "/stop")
                wait_until(lambda: not device.json("/status")["playing"], 3, "stopped status")
                print(f"Speaker command/status verified: {name}; confirm it was audible")

            invalid_name = f"m0_invalid_{suffix}.wav"
            invalid_body = b"This is not a RIFF/WAVE file."
            device.request("POST", device.file_path("upload", invalid_name), invalid_body)
            uploaded.append(invalid_name)
            device.request("POST", device.file_path("play", invalid_name))
            expected_error = f"Not a WAV file: /sdcard/{invalid_name}"
            if args.serial:
                wait_until(
                    lambda: any(expected_error in line for line in serial_log.lines),
                    3, "invalid WAV rejection log",
                )
            wait_until(
                lambda: device.json("/status")["file"] == "(embedded audio)",
                3, "fallback to embedded audio",
            )
            device.request("POST", "/stop")
            results["invalidWavRejected"] = True
            print(f"Invalid WAV rejected without losing HTTP control: {invalid_name}")

            cycle_name = next(iter(sounds))
            for index in range(args.cycles):
                count = serial_log.playback_starts(cycle_name)
                device.request("POST", device.file_path("play", cycle_name))
                if args.serial:
                    wait_until(lambda: serial_log.playback_starts(cycle_name) > count, 3, "cycle playback log")
                else:
                    wait_until(lambda: device.json("/status")["file"].endswith(cycle_name), 3, "cycle status")
                time.sleep(0.15)
                device.request("POST", "/stop")
                wait_until(lambda: not device.json("/status")["playing"], 3, "cycle stop")
                time.sleep(0.12)
                if (index + 1) % 10 == 0:
                    print(f"Play/stop cycles: {index + 1}/{args.cycles}")

            results["cyclesCompleted"] = args.cycles
            results["healthAfter"] = device.json("/health")
            results["serialPlaybackStarts"] = {
                name: serial_log.playback_starts(name) for name in sounds
            }
            results["serialErrors"] = [
                line for line in serial_log.lines
                if ("E (" in line or "Guru Meditation" in line or "assert failed" in line)
                and expected_error not in line
            ]
            if results["serialErrors"]:
                raise AssertionError(f"Serial log contains {len(results['serialErrors'])} errors")
            print(json.dumps(results, ensure_ascii=False, indent=2))
        except Exception as error:
            results["failure"] = f"{type(error).__name__}: {error}"
            raise
        finally:
            results["serialDiagnostics"] = [
                line for line in serial_log.lines
                if any(marker in line for marker in (
                    "audio_player:", "sd_card:", "E (", "Guru Meditation",
                    "assert failed", "rst:", "Backtrace:", "Restarting",
                ))
            ][-80:]
            results["panicContext"] = serial_log.panic_context()
            if results.get("failure"):
                print("Serial diagnostics after failure:")
                print("\n".join(results["serialDiagnostics"][-25:]))
                print("\n".join(results["panicContext"]))
            try:
                device.request("POST", "/stop")
            except (OSError, urllib.error.HTTPError):
                pass
            for name in uploaded:
                try:
                    device.request("POST", device.file_path("delete", name))
                except (OSError, urllib.error.HTTPError) as error:
                    print(f"Cleanup needed for {name}: {error}")
            if args.report:
                with open(args.report, "w", encoding="utf-8") as file:
                    json.dump(results, file, ensure_ascii=False, indent=2)
                    file.write("\n")


if __name__ == "__main__":
    main()
