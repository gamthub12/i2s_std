# ESP32-S3 Audio Device

Android **0.3** adds a blue Material 3 interface, separate sign-in/sign-out, and phone recording → verified WAV upload to the speaker's SD card over the local network. See [M3_RESULT.md](M3_RESULT.md) for usage, APKs, validation and the LAN-only audio-transfer scope.

ESP-IDF firmware for an ESP32-S3 board with I2S audio playback, microphone recording, SD card storage and a Wi-Fi file manager.

## What it does

- Plays the embedded `data/audio.wav` when an alarm or manual play command requests it. The speaker is quiet after boot.
- Plays 16-bit PCM WAV files from the SD card on request, and accepts stop/play requests through a small HTTP API.
- BOOT/GPIO0 toggles manual playback and stop after boot. Microphone recording is disabled in M1 because its old BOOT button handler conflicts with this button.
- Monitors SD card insertion/removal and mounts its FAT filesystem at `/sdcard`. Filesystem access is serialized with the monitor so it does not unmount during an active file operation.
- Serves an embedded Vietnamese file manager at `/`, with list, upload, download, delete, play, stop and playback-status functions.
- Serves a local alarm interface at `/alarms`: create, edit and delete up to 20 alarms, choose a WAV on SD or embedded sound, select repeat days, volume, ring duration and snooze, and choose a sound for **Play now**.
- Stores the alarm schedule in NVS with schema version, revision and CRC. Syncs time from NTP using `pool.ntp.org`; alarms run locally in `Asia/Ho_Chi_Minh` time after the clock has synchronized.

## Hardware configuration

The checked-in configuration targets **ESP32-S3**. Confirm the wiring against the actual board before powering it:

| Function | Pins/configuration |
|---|---|
| SD card, SDMMC 1-bit | CLK GPIO39, CMD GPIO38, D0 GPIO40 |
| BOOT button | GPIO0, active low; short press toggles Play now/Stop after boot |
| I2S microphone RX | Controller 1; BCLK GPIO6, WS GPIO7, DIN GPIO9 |
| I2S speaker TX | `EXAMPLE_I2S_*_IO1` from the ESP-IDF `i2s_examples_common` component |

Microphone capture, when enabled, is mono, 16-bit, 16 kHz, up to 6 seconds. Playback accepts PCM WAV with 16-bit samples, one or two channels, and sample rates from 8 to 48 kHz. The device has one mono speaker; stereo WAV files are mixed down to mono.

The SD driver uses internal pull-ups and recommends external pull-ups on CMD and D0. User files are stored on the removable SD card; the unused SPIFFS partition has been removed from the partition table.

## Configure Wi-Fi

Set the network name and password in the project configuration:

```powershell
idf.py menuconfig
```

Open **Audio device configuration**. The SSID and password are stored in local `sdkconfig`, which is ignored by Git. Do not commit or share that file. If no SSID is configured, the HTTP server starts but the device cannot join a Wi-Fi network. The serial log prints the assigned IP after a successful connection.

The HTTP server has no login or access control. Use it only on a trusted network; anyone who can reach it can upload or delete SD files and control playback.

GPIO0 is also a boot strapping pin. Do not hold BOOT while resetting or powering up unless you intend to enter flashing mode. The current M1 firmware uses GPIO0 for Play now/Stop; do not enable the old microphone task on the same pin.

## Build and flash

Use ESP-IDF **6.0.1** with the I2S example common component available through the configured ESP-IDF installation.

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the board's serial port. After boot, watch the monitor for the Wi-Fi IP, then open `http://<device-ip>/` from a phone or computer on the same network. A card must be mounted for file operations and for saving microphone recordings. Mount failure no longer formats the card automatically.

To regenerate the embedded melody, run:

```powershell
python make_audio.py
idf.py build
```

The WAV is embedded in the firmware, so rebuilding is required after changing it.

## HTTP endpoints

File endpoints take a URL-encoded `filename` query parameter. The server accepts only a single filename component and rejects path separators and traversal names.

| Method and path | Purpose |
|---|---|
| `GET /` | Embedded file manager page |
| `GET /alarms` | Local alarm interface |
| `GET /api/alarms` | Alarm list, revision, clock and ringing state |
| `POST /api/alarms/save` | Create or update one alarm (form body) |
| `POST /api/alarms/delete` | Delete an alarm by ID (form body) |
| `POST /api/manual-sound` | Select WAV basename for Play now; empty value uses embedded audio |
| `POST /api/play-now` | Play the selected sound now |
| `POST /api/snooze` | Snooze an active alarm when configured |
| `GET /list` | List SD files as JSON |
| `POST /upload?filename=name.wav` | Upload raw request body to SD |
| `GET /download?filename=name.wav` | Download an SD file |
| `POST /delete?filename=name.wav` | Delete an SD file |
| `POST /play?filename=name.wav` | Request WAV playback |
| `POST /stop` | Stop current playback |
| `GET /status` | Read playback state as JSON |
| `GET /health` | Read SD mount state and internal/PSRAM heap diagnostics |

## Source layout

```text
main/                         app entry point and microphone capture
components/audio_player/      I2S playback and WAV decoding
components/alarm_manager/     NVS alarm schedule, local scheduler and GPIO0 button
components/sd_card/           SDMMC mount, hot-plug monitor and filesystem lock
components/web_server/        Wi-Fi, NTP, embedded UI and HTTP endpoints
data/audio.wav                embedded sample audio
pytest_audio_device.py        serial-log smoke test for the ESP32-S3 app
```

The old upstream I2S loopback task and its target matrix test were removed because `app_main()` no longer runs that example. `i2s_examples_common` remains as a dependency for the board's I2S pin definitions.

## Smoke test

With ESP-IDF's pytest-embedded environment configured:

```powershell
idf.py build
pytest pytest_audio_device.py
```

The smoke test checks that the SD monitor, audio player and HTTP server start. The microphone task is not started in the default M0 configuration. It does not exercise physical audio quality or card removal; those require hardware checks.

For the M0 hardware check, run `python tools/m0_validate.py http://<device-ip> --serial COMx --cycles 50 --report m0_full.json` with the ESP-IDF Python environment. It uploads uniquely named test WAVs, verifies SD reads with SHA-256, plays through the single speaker, tests stop/restart and a malformed WAV, then removes its test files. See [M0_RESULT.md](M0_RESULT.md) for the latest device result. Run `python tools/m0_watch_sd.py http://<device-ip> --filename hello.txt` before a physical SD remove/reinsert test if the mount-state transition needs to be captured.

For M1, open `http://<device-ip>/alarms` on the same local network. Upload WAV files from `/` first, then select them in the alarm form. The device must synchronize NTP time at least once after power-up; `/alarms` shows whether its clock is ready. Alarm times use Vietnam time (UTC+7). Without an RTC with backup power, the device cannot ring on schedule after a cold boot without any time source. Run `python tools/m1_validate.py http://<device-ip> --wait-for-ring --report m1_result.json` to create and clean up temporary alarms while checking scheduling. See [M1_RESULT.md](M1_RESULT.md).
