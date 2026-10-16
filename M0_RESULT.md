# Kết quả M0 — kiểm tra một loa và thẻ SD

Trạng thái: **đạt kiểm tra chức năng một loa và SD trên board, gồm rút/cắm lại thẻ**. Không dùng micro trong bản firmware M0 (`CONFIG_AUDIO_ENABLE_MIC=n`).

## Phần đã xác nhận

- Board ESP32-S3 N16R8 trên COM4 (flash 16 MB, PSRAM 8 MB), ampli MAX98357, một loa 8 Ω 3 W; firmware M0 đã nạp vào phân vùng app, giữ nguyên NVS và thẻ SD. Ampli và loa này chỉ dùng thử; phiên bản thực tế sẽ thay bằng cấu hình công suất lớn hơn, cần chốt nguồn và phần cứng riêng.
- Thẻ SD được mount, file kiểm tra khởi động đọc đúng. Endpoint `/health` báo `sdMounted=true`.
- Hai file WAV thử, mono 16 kHz và stereo 44,1 kHz, được tải lên SD rồi tải xuống; SHA-256 trước/sau khớp. **Cả hai file đều phát qua cùng một loa mono**; file stereo được trộn xuống mono. Người dùng xác nhận nghe cả hai rõ, không rè hoặc đứt quãng.
- 50 lượt phát → dừng → phát đã hoàn thành. Log serial ghi 51 lần bắt đầu phát file mono (một lần thử đầu và 50 lần lặp), một lần file stereo, không có lỗi hay panic. Trạng thái HTTP trở về `playing=false` sau mỗi lệnh dừng.
- File `.wav` hỏng được từ chối với log `Not a WAV file`; player chuyển về âm nhúng, HTTP vẫn nhận lệnh dừng, không panic. File thử đã xóa.
- Người dùng rút/cắm lại thẻ khi loa dừng. Bộ theo dõi ghi được `/health` chuyển `sdMounted=true → false → true`; thẻ ở trạng thái tháo 15,54 giây. Sau khi cắm lại, HTTP đọc được `hello.txt` 24 byte và SHA-256 trước/sau khớp (`1e6c419c1832dd4e134161c17e9af85ca9664f221970152907598d73120d6f69`).
- `internalFree` trước/sau bài 50 lượt: 244.571 → 244.175 byte (chênh 396 byte); `internalLargest` giữ 167.936 byte; `internalMinimum` trong bài là 222.715 byte. Chưa thấy dấu hiệu bộ nhớ giảm lớn sau lượt lặp. File thử đã xóa khỏi SD.

Báo cáo máy: [m0_full.json](m0_full.json) (50 lượt), [m0_invalid.json](m0_invalid.json) (file hỏng), [m0_sd_reinsert.json](m0_sd_reinsert.json) (rút/cắm thẻ). Chạy lại: `python tools/m0_validate.py http://<device-ip> --serial COM4 --cycles 50 --report m0_full.json`.

## Lỗi tìm thấy và đã sửa trong lúc thử

1. Player nạp trước tối đa 4.096 mẫu vào DMA nhưng ở 16 kHz chỉ cấp phát bộ đệm PCM 3.200 mẫu, gây ghi quá biên và hỏng heap. Đã cấp phát theo kích thước lớn hơn giữa chunk và vùng nạp trước trong cả luồng WAV nhúng lẫn WAV từ SD.
2. Đường HTTP tải file dùng buffer 2 KB trên stack mặc định 4 KB; đã chuyển buffer sang heap và tăng stack HTTP lên 8 KB. Đây là biện pháp giảm áp lực stack trong lúc đọc FATFS.

Sau các sửa đổi, bài thử hai file và 50 lượt chạy hoàn tất không có panic.

## Thông tin phần cứng còn thiếu cho thiết kế bản công suất lớn

- Nguồn cấp hiện tại và loại/dung lượng thẻ SD chưa được cung cấp. Mã board, ampli và loa thử đã được ghi nhận.
- Cần chốt ampli, loa và nguồn công suất lớn sau M0; phép thử loa 8 Ω 3 W không chứng minh hiệu năng của cấu hình công suất lớn tương lai.

Các thông tin này không ảnh hưởng kết quả M0 trên phần cứng thử hiện tại.
