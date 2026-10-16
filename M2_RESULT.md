# M2 — Firebase Emulator và ứng dụng Android thử nghiệm

Cập nhật 2026-10-08: Firebase thật `alarm-12463` dùng gói Spark với Authentication Email/Password và Realtime Database. Người dùng đã ghép Android và xác nhận Phát ngay/đặt lịch tới ESP. Đã chuyển firmware Spark từ thăm dò REST sang SSE, gửi trạng thái khi thay đổi hoặc mỗi 60 giây; APK mới dùng ngưỡng Offline 150 giây. Firmware và APK đã build, firmware đã nạp COM4. Chi tiết và kết quả đo: [M2_FIREBASE_CLOUD.md](M2_FIREBASE_CLOUD.md). Các mục Emulator bên dưới là kết quả của giai đoạn thử trước.

## Phạm vi đã làm

- Backend cục bộ tại `m2/firebase`: Authentication, Realtime Database và Cloud Functions Emulator. Dùng project ID `demo-alarm`; không cần Firebase project thật.
- Ghép ESP32 bằng Device ID, bí mật riêng lưu trong NVS và mã 6 chữ số. Backend cấp Custom Token cho từng thiết bị. Rules phân quyền tài khoản sở hữu và thiết bị.
- Firmware ESP32 gửi lịch M1 đang lưu trong NVS lên emulator khi đăng ký lần đầu, đọc lịch từ Database, áp dụng vào NVS, báo `appliedRevision`, gửi trạng thái và xử lý lệnh phát/dừng/báo lại. ID lệnh được lưu trước khi phát để tránh phát lặp sau reset. Khi mất mạng, lịch đã lưu vẫn chạy tại chỗ sau khi đồng hồ đã đồng bộ.
- App Kotlin/Compose tại `m2/android`: đăng ký/đăng nhập một tài khoản thử, ghép loa, thêm/sửa/xóa/bật/tắt báo thức, chọn tên WAV sẵn có trên SD, chọn âm phát ngay, phát/dừng/báo lại và xem phiên bản đồng bộ. Giới hạn 20 báo thức, thời lượng reo 5–30 giây.
- Thu âm bằng micro điện thoại và tải WAV lên SD thuộc M3. M2 chỉ chọn tên file WAV đã có trên SD hoặc âm nhúng mặc định.

## Cách chạy thử

1. Lần đầu tiên, chạy `powershell -NoProfile -ExecutionPolicy Bypass -File .\m2\firebase\setup.ps1` để cài Firebase CLI, JRE và Database Emulator. Sau đó chạy `powershell -NoProfile -ExecutionPolicy Bypass -File .\m2\firebase\start-emulator.ps1` từ thư mục gốc dự án. Giữ cửa sổ này mở. Các cổng là Auth `9099`, Database `9000`, Functions `5001` trên máy tính.
2. Firmware M2 đã được build với host emulator `192.168.100.114` trong `sdkconfig`. Nếu IP máy tính đổi, chỉnh `CONFIG_M2_FIREBASE_HOST` rồi build và nạp lại. Điện thoại, ESP32 và máy tính phải ở cùng LAN; cho phép các cổng emulator qua tường lửa Windows nếu cần.
3. Sau khi nạp firmware, mở `http://<IP-ESP>/api/pairing` để lấy Device ID và mã ghép. Mã này chỉ dành cho mạng thử nghiệm tin cậy.
4. Mở `m2/android` bằng Android Studio; cần Android SDK 35, Build Tools 35.0.0 và JDK 17+. Với Android Emulator, host mặc định `10.0.2.2`. Với điện thoại thật, build với `-PemulatorHost=192.168.100.114` (thay bằng IP máy chạy Firebase Emulator). Tạo tài khoản thử, nhập Device ID/mã ghép, rồi thao tác báo thức.
5. Kiểm thử backend tự động bằng `powershell -NoProfile -ExecutionPolicy Bypass -File .\m2\firebase\run-tests.ps1` khi đã dừng tiến trình emulator đang chạy trên các cổng trên.

## Kiểm tra đã chạy

- `node --test m2/firebase/functions/test/protocol.test.js`: 2/2 đạt.
- `run-tests.ps1`: đạt luồng tạo tài khoản, đăng ký/ghép thiết bị, cấp và đổi Custom Token, cập nhật lịch, chặn truy cập trái quyền, gửi trạng thái, lệnh, đổi bí mật thiết bị và thu hồi thiết bị.
- ESP-IDF 6.0.1: build firmware thành công cả khi M2 tắt lẫn khi M2 bật. Bản M2 có `build/esp32_audio_device.bin`.
- Board ESP32-S3 N16R8 trên COM4: đã nạp bản M2, SD mount và đọc được file, lịch M1 gồm một báo thức được chuyển vào emulator tự động với `alarmCount=1`, `manualSound=audio01.wav`. Sau khi ghép tài khoản thử, board gửi `appliedRevision=1`, `sdMounted=true`; lệnh `playNow` và `stop` đều có ACK, `playing=false` sau khi dừng. Người dùng xác nhận loa phát âm ngắn rồi dừng đúng.
- Emulator đã được khởi động lại sau bài thử, nên board hiện **chưa ghép tài khoản** và lịch M1 đã được seed sẵn. Có thể ghép từ app mà không xóa báo thức đang có.
- APK Android M2 đã build thành công với Android SDK Platform 35, Build Tools 35.0.0, Gradle 8.11.1 và Firebase BoM 33.16.0. Tệp: `m2/android/app/build/outputs/apk/debug/app-debug.apk` (bản debug cấu hình host emulator `192.168.100.114`).
- Sửa lỗi Android Auth báo "Please set a valid API key" khi tạo/đăng nhập tài khoản thử: thay chuỗi `fake-api-key` bằng API key giả đúng định dạng cho bản emulator và build lại APK. Cần cài đè APK mới trên điện thoại để kiểm tra thao tác thực tế.

## Giới hạn của bản emulator

- Firebase Emulator dùng HTTP trong LAN và dữ liệu thử mặc định mất khi tắt tiến trình. Đây là cấu hình thử nghiệm, không dùng trên Internet.
- Firmware M2 lấy lịch/lệnh bằng REST mỗi 2 giây. Mục tiêu độ trễ lệnh và cơ chế streaming trong plan cần đo trên board trước khi chốt M2.
- `appliedRevision` xác nhận lịch đã lưu NVS. Tên WAV được kiểm tra cú pháp, nhưng file WAV riêng trên SD chưa được chuyển từ điện thoại xuống loa trong M2.
- APK đã build nhưng chưa thử giao diện trực tiếp trên điện thoại vì ADB chưa thấy thiết bị kết nối. Cài APK trên điện thoại Android cùng LAN với máy chạy emulator rồi kiểm tra ghép thiết bị, cập nhật báo thức và nút phát/dừng.
