# Báo cáo hiện trạng dự án ESP32-S3 Audio Device

**Cập nhật:** 30/09/2026  
**Framework/target:** ESP-IDF 6.0.1, ESP32-S3  
**Mục đích:** Ghi lại chức năng hiện tại, kiến trúc mã nguồn, thao tác build và các giới hạn cần biết khi tiếp tục phát triển.

## 1. Tóm tắt

Firmware tích hợp phát âm thanh qua I2S, thu microphone bằng nút BOOT, lưu và đọc WAV trên thẻ SD, đồng thời cung cấp giao diện quản lý file qua Wi-Fi. Khi khởi động, thiết bị mở các tác vụ SD, audio và microphone, khởi tạo Wi-Fi Station và HTTP server. Nếu đã cấu hình Wi-Fi, địa chỉ IP được in ra serial monitor.

Ở trạng thái nền, audio player phát lặp `data/audio.wav` được nhúng vào firmware. Khi nhận yêu cầu, player phát file WAV 16-bit PCM từ SD một lượt hoặc dừng phát. Giữ BOOT (GPIO0) để thu tối đa 6 giây; nhả nút để ghi bản thu thành `/sdcard/recorded.wav` và yêu cầu phát lại.

## 2. Cấu trúc hiện tại

| Đường dẫn | Vai trò |
|---|---|
| `main/app_main.c` | Khởi chạy các module ứng dụng |
| `main/microphone.c`, `main/microphone.h` | Thu âm I2S khi giữ nút BOOT, ghi WAV xuống SD |
| `components/audio_player/` | I2S TX, giải mã PCM WAV nhúng và WAV trên SD, điều khiển play/stop/status |
| `components/sd_card/` | Mount SDMMC/FAT, phát hiện thẻ nóng và khóa truy cập filesystem |
| `components/web_server/` | Wi-Fi Station, trang HTML nhúng và API quản lý file/audio |
| `data/audio.wav`, `make_audio.py` | WAV mẫu nhúng và script tạo giai điệu mẫu |
| `partitions.csv` | Partition table đang được chọn |
| `pytest_audio_device.py` | Smoke test xác nhận các dịch vụ khởi chạy |
| `README.md` | Hướng dẫn build, cấu hình và sử dụng |

Ví dụ loopback I2S cũ cùng README/test đa target cũ đã được bỏ; chúng mô tả một luồng chạy không còn được gọi từ `app_main()`. Audio player được tách thành component để loại bỏ phụ thuộc vòng giữa `main` và web server. Dependency `i2s_examples_common` vẫn được dùng để lấy định nghĩa chân I2S ví dụ.

## 3. Luồng khởi động và hoạt động

1. `sd_card_init()` tạo monitor task. Task dò thẻ mỗi khoảng 1.5 giây, mount tại `/sdcard`, kiểm tra trạng thái và unmount khi phát hiện tháo thẻ.
2. `audio_player_start()` tạo I2S TX mono 16-bit và tác vụ phát. Khi rảnh, WAV nhúng phát lặp; yêu cầu từ SD có độ ưu tiên cao hơn.
3. `microphone_init()` tạo task cấu hình nút GPIO0 và I2S RX.
4. `web_server_start()` khởi tạo NVS, kết nối Wi-Fi Station nếu đã cấu hình SSID và mở HTTP server.
5. Các tác vụ web/audio/microphone cùng dùng API khóa SD để monitor không unmount filesystem giữa một lượt đọc/ghi.

## 4. Tính năng chi tiết

### Audio player

- WAV nhúng được đưa vào firmware bằng `EMBED_FILES`; đổi file WAV cần build lại.
- File WAV trên SD được đọc theo chunk, giới hạn PCM 16-bit, 1 hoặc 2 kênh và sample rate 8–48 kHz; stereo được trộn xuống mono.
- Parser kiểm tra kích thước chunk và thông tin format cơ bản trước khi phát.
- Mutex bảo vệ yêu cầu phát/dừng và đường dẫn hiện tại; yêu cầu mới tới trong lúc phát file được giữ lại để phát sau file hiện tại.

### Microphone

- I2S controller 1, mono, 16-bit, 16 kHz; chân BCLK GPIO6, WS GPIO7, DIN GPIO9.
- Nút BOOT GPIO0 active-low: nhấn để thu, nhả để dừng. Giới hạn buffer là 6 giây.
- WAV được ghi theo header và dữ liệu trực tiếp xuống SD, không tạo mảng WAV 192 KB trên stack task.
- Sau khi ghi thành công, gọi audio player để phát lại.

### SD card

- SDMMC 1-bit, CLK GPIO39, CMD GPIO38, D0 GPIO40; mount FAT tại `/sdcard`.
- Monitor thử mount lại sau khi chưa thấy thẻ và kiểm tra trạng thái thẻ đang mount.
- Filesystem được bảo vệ bằng recursive mutex khi monitor unmount, web server thao tác file, audio player stream WAV hoặc microphone ghi bản thu.
- Mount lỗi không còn kích hoạt format tự động (`format_if_mount_failed=false`).
- Kiểm tra read/write dùng file tạm ẩn `.sd_card_check.tmp` và xóa sau khi kiểm tra để tránh ghi đè file người dùng.

### Wi-Fi và HTTP server

- Kết nối Station theo `CONFIG_WIFI_SSID`/`CONFIG_WIFI_PASSWORD`, cấu hình trong `idf.py menuconfig` dưới **Audio device configuration**.
- Trang giao diện tiếng Việt được nhúng trực tiếp trong `web_server.c`; không cần `index.html` trên SD.
- API gồm `GET /`, `GET /list`, `POST /upload?filename=...`, `GET /download?filename=...`, `POST /delete?filename=...`, `POST /play?filename=...`, `POST /stop` và `GET /status`.
- Tên file được giải mã URL và chỉ chấp nhận một path component; dấu `/`, `\`, `:`, ký tự điều khiển và tên `.`/`..` bị từ chối. Tên file trả về JSON được escape.
- HTTP server không có xác thực người dùng. Bất kỳ máy nào truy cập được server đều có thể tải/xóa file và điều khiển playback; chỉ chạy trong mạng đáng tin cậy.

## 5. Phần cứng và cấu hình

| Hạng mục | Giá trị hiện tại |
|---|---|
| Target | ESP32-S3 |
| Loa I2S TX | `EXAMPLE_I2S_*_IO1` từ `i2s_examples_common`; cần đối chiếu với board thực tế |
| Microphone I2S RX | Controller 1; BCLK GPIO6, WS GPIO7, DIN GPIO9 |
| SD card | SDMMC 1-bit; CLK39, CMD38, D0 40 |
| Nút thu âm | BOOT GPIO0, active-low |
| Thu âm | 16 kHz, mono, PCM 16-bit, tối đa 6 giây |
| WAV playback | PCM 16-bit, 1/2 kênh, 8–48 kHz |

Partition table hiện hành là `partitions.csv`: NVS/OTA/PHY và app factory. Partition SPIFFS không được code dùng nên đã được bỏ; file người dùng lưu trên SD. Bảng phân vùng trùng trong `partition_table/` cũng đã bị xóa để tránh nhầm cấu hình.

## 6. Build và sử dụng

Trong môi trường ESP-IDF 6.0.1:

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Thay `COMx` bằng cổng serial. Nhập SSID/mật khẩu trong menuconfig; `sdkconfig` bị `.gitignore` bỏ qua để không đưa cấu hình riêng vào Git. Mở serial monitor để lấy IP, rồi truy cập `http://<IP>/` từ thiết bị cùng mạng. Không có SSID thì firmware vẫn khởi tạo HTTP server nhưng ESP32 không kết nối Wi-Fi.

Tạo lại giai điệu WAV mẫu:

```powershell
python make_audio.py
idf.py build
```

## 7. Các cải tiến đã thực hiện trong đợt này

- Đổi tên module `app_main`, `audio_player`, `microphone`; tách audio player thành component riêng.
- Xóa task/hàm I2S mẫu không được gọi, test loopback cũ, Kconfig lựa chọn duplex/SPIFFS audio không còn hiệu lực, header `audio_data.h` không được dùng, partition SPIFFS dư thừa và bảng phân vùng trùng.
- Cập nhật README theo ứng dụng hiện tại và thêm smoke test chỉ target ESP32-S3.
- Đưa thông tin Wi-Fi ra Kconfig, xóa credential hardcode khỏi source.
- Chặn path traversal, escape JSON và kiểm tra lỗi ghi upload cơ bản.
- Không tự format SD khi mount lỗi; thêm khóa filesystem cho các thao tác dài; dùng file tạm khi kiểm tra thẻ.
- Loại bỏ buffer WAV lớn khỏi stack microphone task.
- Kiểm tra WAV PCM, số kênh, sample rate, alignment và giới hạn chunk; giữ yêu cầu phát mới trong khi file đang phát.

## 8. Giới hạn và việc cần xác nhận

1. Cần xác nhận wiring thật của chân speaker vì chúng đến từ `i2s_examples_common`, không được khai báo trực tiếp trong repo.
2. HTTP server chưa có đăng nhập, TLS hoặc bảo vệ truy cập; không đưa lên mạng công cộng.
3. Khóa SD được giữ trong suốt một số thao tác mạng như upload/download. Một client chậm có thể trì hoãn monitor SD cho tới khi request kết thúc.
4. Smoke test mới chỉ kiểm tra log khởi động; chất lượng audio, nút BOOT, tháo thẻ khi đang hoạt động và luồng web cần thử trên phần cứng.
5. Chưa chạy build hoặc test sau thay đổi trong lần cập nhật này; cần cấu hình ESP-IDF/board để xác nhận biên dịch và vận hành.

## 9. Smoke test

Khi đã cài môi trường pytest-embedded của ESP-IDF:

```powershell
pytest pytest_audio_device.py
```

Test đợi log của SD monitor, audio player, microphone và HTTP server. Test không giả lập được thẻ SD, microphone, speaker hay thao tác nút.
