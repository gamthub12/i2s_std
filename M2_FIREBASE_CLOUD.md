# M2: Firebase Spark — alarm-12463

Cập nhật: 2026-10-08. Chỉ dùng gói Spark theo yêu cầu của người dùng.

## Cấu hình và nguyên lý

- Project: `alarm-12463`; Android package: `vn.local.alarmspeaker`.
- Database: `https://alarm-12463-default-rtdb.asia-southeast1.firebasedatabase.app`.
- Dùng Authentication Email/Password và Realtime Database. Luồng Spark không gọi Cloud Functions, không yêu cầu Blaze.
- ESP32 có tài khoản thiết bị riêng, mật khẩu ngẫu nhiên lưu NVS. Rules kiểm tra UID thiết bị và UID chủ sở hữu; không lưu mật khẩu hay mã ghép trong Database.
- Ghép lần đầu qua Wi-Fi tin cậy: Android gửi Device ID, mã sáu số và ID token đến `POST /api/pair` trên ESP32. ESP xác minh token bằng Firebase Auth rồi ghi chủ sở hữu. Rules không cho thay chủ sở hữu đã ghép.
- Sau khi ghép, lịch, lệnh và trạng thái đi qua Firebase; không cần máy tính chạy Emulator. Điều khiển từ mạng khác được khi điện thoại và ESP đều có Internet.
- Firmware Spark giữ một kết nối SSE tại `/devices/<id>/inbox`. Chỉ đọc lịch/lệnh khi sự kiện báo thay đổi hoặc vừa kết nối lại; bỏ thăm dò mỗi hai giây. Các sự kiện xóa lệnh sau ACK không gây đọc lại hàng đợi.
- Gửi trạng thái ngay khi thay đổi phát/dừng, SD, đồng bộ giờ hoặc phiên bản lịch; khi rảnh chỉ gửi nhịp báo online mỗi 60 giây. App đánh dấu offline sau 150 giây không có trạng thái mới.
- Tái sử dụng HTTP client/kết nối HTTPS cho thao tác Database. Luồng SSE tự kết nối lại với thời gian chờ tăng từ 2 đến 60 giây; token được làm mới và stream mở lại trước hạn Firebase. Lịch đã lưu vẫn chạy tại chỗ khi mất mạng nếu đồng hồ đã đồng bộ.

## Đã chuẩn bị và kiểm tra

- Firebase CLI đã đăng nhập; Authentication Email/Password đã bật.
- Rules Spark tại `m2/firebase/database.spark.rules.json` đã triển khai bằng `firebase.spark.json`.
- `node m2/firebase/test/spark-e2e.js` đạt trên project thật: xác thực thiết bị, ghép chủ sở hữu, quyền đọc/ghi, giới hạn lịch/thời lượng, lệnh, ACK và trạng thái. Tài khoản và dữ liệu thử đã xóa.
- APK Spark build thành công: `m2/artifacts/alarm-m2-spark.apk`.
- Firmware Spark dùng `sdkconfig.cloud`, tạo từ `m2/prepare-cloud-firmware.ps1`; cấu hình Wi-Fi riêng được giữ trong file bỏ qua bởi Git.
- Bật xác minh chuỗi chứng chỉ ký chéo của ESP-IDF để kiểm tra HTTPS Google; không tắt xác thực chứng chỉ.
- Tăng stack task Firebase lên 20 KiB và HTTP server lên 16 KiB cho luồng HTTPS/ghép. Trạng thái `reported.playing` lấy từ audio player để phản ánh cả Phát ngay và báo thức.
- Tách dấu lệnh cuối theo project/môi trường để dấu lệnh Emulator không chặn lệnh Firebase push. Sắp xếp lệnh theo khóa trước khi thực thi, lưu dấu trước tác động và xóa lệnh sau ACK trên Spark.

## Kết quả trên ESP32 thật

- Đã nạp firmware Spark lên ESP32-S3 N16R8, COM4, Device ID `b8f862facf84`, IP `192.168.100.169`.
- `node m2/firebase/test/spark-board-e2e.js`: **đạt** ghép trong LAN, đọc lịch đã giữ, lệnh Firebase push, ACK và trạng thái audio player.
- Phát ngay: ACK `done` sau 3301 ms; Dừng: ACK `done` sau 3473 ms. `reported.playing` chuyển true rồi false đúng. Độ trễ này là số đo của lần thử, không phải cam kết cho mọi mạng.
- Không thấy tràn stack/reset trong bài thử sau sửa. Lịch cũ một báo thức và `audio01.wav` được giữ; SD đang mount.
- Kiểm tra cuối: `/status` có `playing=false`; RAM nội còn 125867 byte, mức thấp nhất 109715 byte, khối lớn nhất 45056 byte; PSRAM còn 8379208 byte.
- Đã xóa tài khoản chủ sở hữu tạm của bài thử đầu. Sau đó người dùng ghép app và xác nhận thao tác Phát ngay/đặt lịch từ app đã tới ESP. Lần tối ưu realtime giữ nguyên ghép điện thoại.
- Các số đo RAM và ACK ở trên thuộc bản REST trước tối ưu. Kết quả realtime mới được ghi bên dưới.

## Cài và ghép ứng dụng

1. Cài đè `m2/artifacts/alarm-m2-spark.apk` lên điện thoại.
2. Tạo tài khoản Email/Password trong app, hoặc đăng nhập tài khoản đã tạo ở project thật. Tài khoản Emulator không tự chuyển sang Firebase thật.
3. Điện thoại và ESP ở cùng Wi-Fi khi ghép. Mở `http://192.168.100.169/api/pairing` để lấy Device ID và mã hiện tại. Nếu router đổi IP, dùng IP mới của ESP.
4. Trong app nhập IP loa `192.168.100.169`, Device ID và mã sáu số, rồi nhấn **Ghép loa**. Mã đổi khi ESP khởi động lại.
5. Chờ trạng thái **Online**, kiểm tra SD và phiên bản lịch đồng bộ, rồi thử **Phát ngay** / **Dừng**.

Giữ ESP được cấp nguồn và kết nối Wi-Fi. M2 chưa có ngủ sâu/chạy sau mất điện, chưa có thu âm điện thoại và chuyển WAV xuống SD (thuộc M3). Giới hạn 20 báo thức, mỗi lần reo tối đa 30 giây.

## Kết quả tối ưu realtime (2026-10-08)

`node m2/firebase/test/spark-realtime-board.js` đạt trên Firebase thật và ESP32 thật:

- Lịch 20 báo thức tắt được nhận qua SSE và áp dụng đủ; sau đó khôi phục nội dung lịch người dùng, phiên bản mới là 6 ở lần thử này.
- Ba cặp Phát ngay/Dừng: ACK lần lượt `1364/1940`, `1359/1931`, `1346/1943` ms; trạng thái phát/dừng đúng và lệnh đã ACK được xóa khỏi hàng đợi.
- Đo 70 giây rảnh: `desiredReads=0`, `commandReads=0`, `restRequests=1`, `statusWrites=1`, `streamConnections=0` (số kết nối mở thêm), `streamBytes=60`.
- `streamBytes` chỉ đếm nội dung SSE đã đọc, không phải tổng lưu lượng Firebase tính hạn mức (còn HTTP/TLS). Chưa đo tổng GB/tháng trong Console.
- Sau thử: SD mount, loa dừng, RAM nội còn 96503 byte; mức thấp nhất 78243 byte; ghép tài khoản điện thoại được giữ nguyên. Không thấy reset trong bài thử.
- APK tại `m2/artifacts/alarm-m2-spark.apk` đã build lại để khớp nhịp online 60 giây; cần cài đè bản này. Không cần ghép lại nếu app đang lưu thiết bị.

`GET /health` thêm mục `firebase` với các bộ đếm từ lúc ESP khởi động. Đọc endpoint này trong LAN không gọi Firebase. Bài thử lưu số đo tại `m2/.tools/spark-realtime-result.json` và sao lưu lịch gốc ở cùng thư mục.

Tham khảo nguyên lý [Firebase REST streaming SSE](https://firebase.google.com/docs/database/rest/retrieve-data#section-rest-streaming).

## Build / deploy lại

Trong `m2/firebase`:
```powershell
node build-spark-rules.js
.\node_modules\.bin\firebase.cmd deploy --config firebase.spark.json --only database --project alarm-12463
```

Trong `m2/android`, với JDK/SDK đã cấu hình:
```powershell
.\gradlew.bat :app:assembleDebug '-PuseFirebaseEmulator=false' --max-workers=1
```

Trong terminal ESP-IDF ở thư mục gốc:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\m2\prepare-cloud-firmware.ps1
idf.py -B build-cloud -D SDKCONFIG=sdkconfig.cloud -p COM4 flash
```

Bài thử `spark-board-e2e.js` dành cho board `b8f862facf84` chưa ghép: dùng tài khoản tạm, gửi lệnh push giống Android và bỏ ghép bằng quyền quản trị sau khi xong. Không chạy trên loa đang ghép của người dùng.

Spark có hạn mức miễn phí; xem Usage trong Console để theo dõi lưu lượng REST. Tham khảo [gói Firebase](https://firebase.google.com/docs/projects/billing/firebase-pricing-plans), [hạn mức Database](https://firebase.google.com/docs/database/usage/billing), [Firebase Auth REST](https://firebase.google.com/docs/reference/rest/auth).
