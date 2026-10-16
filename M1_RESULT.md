# Kết quả M1 — báo thức cục bộ

## Đã triển khai

- Tối đa 20 báo thức trên ESP32-S3 N16R8, lưu NVS cùng `revision`, phiên bản schema và CRC. Mỗi báo thức có ID, giờ/phút, ngày lặp, bật/tắt, tên, tệp WAV, âm lượng, thời gian reo và số phút báo lại.
- Đồng bộ giờ qua NTP. Bộ lịch hoạt động theo giờ Việt Nam UTC+7 sau khi thiết bị có giờ hợp lệ; lịch được xử lý trong task riêng, không phụ thuộc vào web server. Các báo thức trùng phút chỉ tạo một lượt phát, chọn ID nhỏ nhất và ghi nhận tất cả đã kích hoạt. Báo thức một lần tự tắt sau khi reo.
- Một đầu mối điều khiển audio nhận lệnh báo thức, phát ngay, phát tệp từ web, dừng và báo lại. File SD không có hoặc không đọc được sẽ chuyển sang âm nhúng cho báo thức và phát ngay. Loa im lặng sau khởi động.
- Nút BOOT/GPIO0 bấm một lần để phát ngay, lần tiếp theo để dừng; dùng lọc dội 60 ms. Không dùng micro trong M1.
- Giao diện web local tại `/alarms`, có danh sách, biểu mẫu tạo/sửa/xóa, chọn WAV trên SD, chọn âm phát ngay, trạng thái đồng hồ và lần reo tiếp theo.

## Kiểm tra trên thiết bị thật

- Build ESP-IDF 6.0.1 thành công; firmware được nạp vào phân vùng app `0x20000`, giữ nguyên NVS và SD.
- Web `/alarms` và `/api/alarms` phản hồi; giờ đã đồng bộ NTP.
- Tạo đủ 20 báo thức; lịch thứ 21 bị từ chối HTTP 409. Sửa và xóa lịch thành công.
- Phát ngay và dừng qua HTTP: `/status` đổi `playing=true → false`.
- Báo thức thử lúc **21:52 ngày 01/10/2026 (UTC+7)** đã reo với audio đang chạy, tự dừng sau 6 giây và tự tắt vì là báo thức một lần.
- Một lịch thử vẫn còn sau reset qua COM4, xác nhận dữ liệu NVS được tải lại.
- Bộ theo dõi nút GPIO0 ghi `playing=false → true → false` qua hai lần bấm; người dùng xác nhận nghe phát rồi dừng đúng.
- Trên firmware cuối, tệp WAV thử bị lỗi trên SD được chọn làm âm phát ngay; player chuyển sang âm nhúng và `/status` báo đúng. Tệp thử đã xóa, âm phát ngay trước thử được khôi phục.

Báo cáo tự động: [m1_result.json](m1_result.json), [m1_reboot.json](m1_reboot.json). Script kiểm tra: [tools/m1_validate.py](tools/m1_validate.py); theo dõi nút: [tools/m1_watch_button.py](tools/m1_watch_button.py); âm dự phòng: [tools/m1_check_fallback.py](tools/m1_check_fallback.py). Các lịch thử đã được xóa.

## Giới hạn hiện tại

- Chưa kiểm tra thực địa trường hợp Wi-Fi mất lâu sau đồng bộ giờ. Scheduler không gọi mạng khi xét lịch, nên lịch đã lưu vẫn có thể chạy theo đồng hồ hệ thống; cần kiểm tra riêng trước khi coi đây là tiêu chí nghiệm thu hoàn tất.
- Sau khi mất điện và khởi động lại khi không có mạng, board hiện không có RTC pin dự phòng để xác định giờ thật. Báo thức chờ tới khi NTP đồng bộ.
- Ampli MAX98357 và loa 8 Ω 3 W vẫn là phần cứng thử. Cấu hình công suất lớn cho bản thực tế cần kiểm tra riêng.
