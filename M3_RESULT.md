# Android 0.3 — giao diện và thu âm làm chuông trong LAN

Cập nhật: 16/10/2026. Giữ Firebase Spark/Emulator và API file hiện có của ESP32; không sửa firmware trong đợt này.

## Chức năng

- Giao diện Material 3 xanh dương, màn hình đăng nhập/đăng ký riêng. Sau đăng nhập là ba tab Tổng quan, Báo thức và Âm thanh. Đăng xuất trong biểu tượng tài khoản ở góc phải.
- Tự khôi phục phiên đăng nhập, loa đã chọn và IP theo tài khoản. Hủy listener và tác vụ khi đổi tài khoản; không hiển thị lịch hoặc bản thu của tài khoản trước.
- Tổng quan có lịch tiếp theo theo giờ Việt Nam, Online/Offline, SD, trạng thái đồng bộ và các nút Phát ngay/Dừng/Báo lại.
- Báo thức có bộ chọn giờ, ngày lặp, âm báo, âm lượng, thời lượng và báo lại. Tạo/sửa/xóa/bật/tắt giữ giới hạn 20 lịch; giao dịch Firebase kiểm tra lại giới hạn và ID trên dữ liệu mới nhất.
- Xin quyền micro khi bắt đầu thu. Thu PCM 16-bit, mono, 16 kHz trực tiếp ra WAV; tự dừng ở 30 giây và dừng khi ứng dụng ra nền. Có nghe thử, đặt tên, lưu bền vững tối đa 20 bản thu/tài khoản trên điện thoại và xóa bản thu local.
- Gửi WAV qua Wi-Fi tới SD bằng API `/upload` hiện có. Kiểm tra Device ID, SD, định dạng WAV và số lượng tệp trước khi gửi. Sau gửi, tải lại tệp và so kích thước/SHA-256; chỉ bản thu đã xác minh mới được chọn làm chuông.
- Từ bản thu đã gửi có thể chọn làm âm Phát ngay hoặc tạo một báo thức với âm đó. Danh sách chọn âm cũng có các WAV sẵn có trên SD và âm mặc định nhúng.

## Cách sử dụng

1. Đăng nhập, chọn loa tại Tổng quan → biểu tượng điều chỉnh → Mở loa. Khi ghép mới, bật Ghép loa mới và nhập mã hiện tại.
2. Trong Âm thanh, nhập IP loa ở phần Kết nối thẻ nhớ rồi nhấn Kết nối và tải danh sách. Điện thoại và loa phải cùng LAN, IP phải trỏ đúng Device ID.
3. Bắt đầu thu âm → cấp quyền micro → Dừng thu âm, hoặc chờ tự dừng ở 30 giây.
4. Nghe thử, đặt tên và Lưu bản thu. Gửi lên loa, chờ xác minh hoàn tất.
5. Chọn Dùng phát ngay, hoặc Tạo báo thức. Khi lưu lịch, theo dõi trạng thái đã đồng bộ trên Tổng quan.

Xóa bản thu local không xóa tệp đã gửi trên loa. Đổi máy điện thoại không tự chuyển thư viện thu âm local. Các tệp trên SD vẫn có thể chọn lại từ danh sách WAV.

## Kiểm tra đã thực hiện

- `:app:assembleDebug`, `:app:testDebugUnitTest`, `:app:lintDebug`: thành công. 4 unit test xác nhận WAV PCM, giới hạn 30 giây, từ chối tệp bị cắt/sửa header và PCM lệch khung. Lint không còn lỗi chặn build; còn các khuyến nghị phiên bản thư viện/KTX. 
- Android Emulator API 35: đăng xuất đưa về màn hình đăng nhập riêng; đăng nhập và khôi phục loa. Thư viện/bản thu giữ sau cập nhật APK và khởi động lại app.
- Thu tự dừng 30 giây: 480.000 mẫu, mono 16 kHz, PCM 16-bit, 960.044 byte gồm header. Đổi tên, nghe thử và lưu được.
- App gửi bản thu tới ESP32 `b8f862facf84`, SD thật. Tệp điện thoại và tệp đọc lại từ SD giống từng byte, SHA-256 `cf61979996f3f7f4b683f70f46a905ff5193b71e5a0709bf80dfcf07e75e5af3`.
- Tạo báo thức tạm `M3 test` bằng app; ESP nhận lịch ID 1020 với trường `sound` trỏ tới bản thu. Phát ngay được xác nhận `/status` đang đọc đúng `/sdcard/rec_8b2777a6-fff3-4f2a-895f-7764d5c8432e.wav`; dừng được.
- Đã kiểm tra cuộn thư viện có cùng bản thu ở điện thoại và loa sau khi sửa khóa hiển thị trùng. Đã sửa instance Firebase khi mở lại Activity và vị trí nút Lưu khi bàn phím mở.
- Đã xóa lịch và bản thu thử, khôi phục âm Phát ngay. Nội dung báo thức gốc và tệp SD của người dùng được giữ nguyên.

Kết quả máy và WAV đối chiếu nằm trong `m2/.tools/m3-*`, được Git bỏ qua. Bài thử dùng micro ảo; chưa xác nhận chất lượng giọng nói, độ ồn hoặc tương thích micro trên điện thoại thật. Chưa nghiệm thu toàn bộ báo thức/offline hoặc xử lý mất nguồn khi đang truyền WAV.

## APK và môi trường

- `m2/artifacts/alarm-speaker-v0.3-emulator.apk`: Firebase Emulator host `10.0.2.2` cho máy ảo.
- `m2/artifacts/alarm-speaker-v0.3-phone-lan.apk`: Firebase Emulator host `192.168.100.123` cho điện thoại cùng Wi-Fi với máy tính hiện tại.
- Chạy backend/máy ảo bằng `powershell -NoProfile -ExecutionPolicy Bypass -File .\m2\.tools\start-test.ps1`.
- Build lại bằng JDK/SDK cục bộ: `powershell -NoProfile -ExecutionPolicy Bypass -File .\m2\.tools\build-android.ps1` (thêm `-Phone` để build bản điện thoại LAN).
- Khi build Firebase thật, khôi phục `m2/android/app/google-services.json` và dùng `-PuseFirebaseEmulator=false` như M2. Tải WAV vẫn qua LAN.

Đây là M3 qua LAN, chưa có Cloud Storage, tải WAV từ xa, đồng bộ bản thu giữa nhiều điện thoại hoặc URL tải có hạn dùng. HTTP file API của firmware hiện tại chưa có xác thực; dùng trên mạng nội bộ tin cậy như phạm vi README. Chưa triển khai Storage/Functions thật hoặc thay đổi gói Firebase.
