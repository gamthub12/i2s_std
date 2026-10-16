# Kế hoạch nâng cấp: loa báo thức điều khiển bằng Android

## 1. Mục tiêu và phạm vi

Nâng cấp firmware ESP32-S3 hiện tại thành loa báo thức có thể hoạt động tại chỗ khi mất Internet. Người dùng quản lý nhiều báo thức, chọn âm báo, phát thử ngay và thu âm làm chuông bằng ứng dụng Android. Firebase đồng bộ cấu hình và lệnh giữa điện thoại với thiết bị; ESP32-S3 tự quyết định thời điểm đổ chuông từ cấu hình đã lưu.

**Mốc đầu tiên:** xác thực loa và khả năng đọc thẻ SD trên thiết bị thật. Chưa dùng micro trong mốc này. Tài liệu hiện tại và smoke test chỉ xác nhận các dịch vụ khởi động, không xác nhận âm thanh phát ra hay việc đọc file thực tế.

Phần cứng thử M0: ESP32-S3 N16R8, ampli MAX98357, một loa 8 Ω 3 W. Ampli và loa của bản thực tế sẽ có công suất lớn hơn; cần thiết kế lại nguồn, ampli, tản nhiệt và ngưỡng âm lượng sau khi chọn linh kiện. Giả định phần mềm ban đầu: mỗi thiết bị có **một loa, đầu ra I2S mono**; giai đoạn thử M2 dùng **một tài khoản**. Múi giờ mặc định `Asia/Ho_Chi_Minh`; nhạc báo thức là WAV PCM 16 bit, mono/stereo, 8–48 kHz như player hiện tại. File stereo được trộn xuống mono trước khi phát qua một loa. Giới hạn 20 báo thức/thiết bị; mỗi âm báo dài tối đa 30 giây, tệp dài hơn chỉ lấy 30 giây đầu. Giới hạn ban đầu 20 âm báo riêng/thiết bị; điều chỉnh sau khi đo bộ nhớ và dung lượng thẻ.

## 2. Điểm xuất phát trong dự án

| Thành phần | Đã có | Việc cần làm |
| --- | --- | --- |
| Loa I2S | `components/audio_player`: phát WAV nhúng và WAV trên SD, có lệnh phát/dừng | Kiểm tra phần cứng; điều phối báo thức, phát thử và dừng bằng một hàng lệnh có ưu tiên |
| Thẻ SD | `components/sd_card`: gắn thẻ, khóa filesystem, theo dõi tháo/lắp | Kiểm tra đọc file, lỗi khi rút thẻ, tải và kiểm tra âm báo |
| Wi-Fi/web | `components/web_server`: giao diện nội bộ và API `/play`, `/stop`, `/status` | Giữ làm công cụ chẩn đoán trong quá trình phát triển; thêm kết nối Firebase qua HTTPS |
| Micro | `main/microphone.c`: thu tối đa 6 giây bằng nút BOOT, lưu `recorded.wav` | Để sau mốc loa/SD; chuyển sang nút riêng và nhiều bản thu có tên/ID |
| Kiểm thử | `pytest_audio_device.py`: kiểm tra log khởi động | Bổ sung kiểm thử logic lịch, đồng bộ và kiểm tra thực tế trên board |

Quyết định M1: nút BOOT/GPIO0 dùng cho **phát ngay/dừng** sau khi khởi động; micro vẫn tắt và khi triển khai M3 sẽ dùng nút khác. GPIO0 còn là chân chọn chế độ khởi động, nên không giữ nút khi cấp nguồn/reset trừ lúc chủ ý nạp firmware.

## 3. Trải nghiệm người dùng

### Ứng dụng Android

- Màn hình thiết bị: trạng thái online, đồng bộ gần nhất, SD, âm đang phát, nút **Phát ngay** và **Dừng**.
- Danh sách báo thức: bật/tắt, giờ và phút, các ngày lặp, tên, âm báo, âm lượng, thời lượng reo, tùy chọn báo lại. Cho phép nhiều báo thức cùng tồn tại.
- Thư viện âm: âm mặc định, WAV đã tải lên, bản thu từ thiết bị; nghe thử và chọn âm cho từng báo thức hoặc làm âm mặc định.
- Nút **Thu âm** dùng micro của **điện thoại Android**; bản thu được giới hạn 30 giây và tải lên làm âm báo ở M3. ESP32 không cần micro hay nút thu âm.
- Sau mỗi thao tác, phân biệt **đã lưu trên Firebase** với **loa đã nhận và áp dụng**. Hiển thị lỗi nếu thẻ vắng mặt hoặc tải âm thất bại.

### Thiết bị

- Nút **Phát ngay** phát âm đã chọn cho chức năng này; nếu chưa chọn hoặc file lỗi, dùng âm mặc định nhúng trong firmware.
- Khi đến giờ, loa phát âm của báo thức. Có thể dừng bằng nút trên loa hoặc trên app; báo lại theo cấu hình.
- Nếu hai báo thức trùng thời điểm, gom thành một lượt reo theo quy tắc xác định: chọn báo thức có ID nhỏ nhất và ghi nhận cả hai đã kích hoạt; không mở hai luồng I2S cùng lúc.
- Báo thức đã đồng bộ vẫn chạy khi Firebase hoặc Wi-Fi tạm mất. Lệnh **Phát ngay** từ app cần mạng; nút vật lý vẫn hoạt động.

## 4. Kiến trúc đề xuất

```text
Android (Kotlin/Compose)
  ├─ Firebase Authentication: đăng nhập và quyền sở hữu thiết bị
  ├─ Realtime Database: lịch báo thức, lệnh, trạng thái và phiên bản cấu hình
  └─ Cloud Storage: tệp WAV
             │
             ├─ Cloud Functions HTTPS: ghép thiết bị, xác thực ESP,
             │  cấp lệnh và URL tải tệp có hạn dùng
             ▼
ESP32-S3 ── Wi-Fi/TLS ── luồng sự kiện Realtime Database
  ├─ NVS: ID thiết bị, thông tin ghép, phiên bản và lịch đã xác nhận
  ├─ SD: WAV đã tải và bản thu
  ├─ đồng hồ + bộ lập lịch cục bộ
  └─ audio_player: một luồng sở hữu I2S TX
```

- Android dùng Firebase SDK; ESP dùng HTTPS client của ESP-IDF. Cloud Functions xác thực lúc ghép, cấp Firebase Custom Token cho danh tính của **từng thiết bị** và URL tải WAV có hạn dùng. ESP đổi token qua Firebase Auth REST API rồi dùng ID token để đọc luồng sự kiện Realtime Database; khi kết nối rớt thì kết nối lại và lấy bản cấu hình đầy đủ. Không nhúng khóa service account hoặc quyền quản trị Firebase vào firmware.
- Realtime Database lưu cấu hình và trạng thái nhỏ; Storage lưu âm thanh. Security Rules giới hạn ESP chỉ đọc lịch/lệnh của chính nó và ghi ACK/trạng thái của chính nó; app chỉ truy cập thiết bị đã ghép. Khóa ghép riêng của thiết bị lưu trong NVS, có cách thu hồi và đổi khóa.
- Khi app sửa lịch, tăng `revision`. ESP tải bản cấu hình đầy đủ, kiểm tra dữ liệu, lưu bền vững rồi báo `appliedRevision`. Tránh trạng thái app báo thành công khi loa chưa nhận.
- Mỗi lệnh thủ công có `commandId`, thời hạn và trạng thái `pending/ack/done/error`; ESP nhớ ID đã xử lý để không phát lại khi gửi lặp sau mất mạng.
- Âm báo tải xuống file tạm trên SD, kiểm tra kích thước, định dạng và SHA-256, rồi đổi tên vào vị trí chính thức. Chỉ thay cấu hình báo thức sang âm mới sau khi tải thành công.
- ESP đồng bộ giờ bằng NTP khi có mạng. Sau khi đã lấy giờ, lịch chạy cục bộ. Chưa yêu cầu báo thức đúng giờ sau khi mất điện rồi khởi động lại không có mạng trong M2/M3; xem xét RTC có pin dự phòng sau khi các chức năng chính ổn định.
- Cân nhắc chi phí trước khi tạo môi trường thật: triển khai Cloud Functions và sử dụng Cloud Storage for Firebase yêu cầu gói Blaze theo tài liệu Firebase hiện hành. Đặt cảnh báo ngân sách và thử trên Firebase Emulator trước.

### Mô hình dữ liệu tối thiểu

| Đường dẫn/đối tượng | Trường chính |
| --- | --- |
| `devices/{deviceId}/desired` | `revision`, `timezone`, `defaultSoundId`, `manualSoundId`, `alarms[]` |
| `alarms[]` | `id`, `enabled`, `hour`, `minute`, `daysOfWeek`, `soundId`, `volume`, `ringSeconds`, `snoozeMinutes` |
| `devices/{deviceId}/commands/{commandId}` | `type`, `soundId?`, `issuedAt`, `expiresAt`, `status` |
| `devices/{deviceId}/reported` | `appliedRevision`, `lastSeen`, `sdMounted`, `clockSynced`, `playback`, `errorCode` |
| `sounds/{soundId}` | `name`, `source`, `storagePath`, `sha256`, `size`, `duration`, `format` |

Chỉ lưu ID/path trong lịch; không nhúng URL tải có hạn dùng vào cấu hình lâu dài. Chuẩn hóa ngày lặp và múi giờ thành một quy ước duy nhất giữa Android, Firebase và firmware.

## 5. Các mốc triển khai và điều kiện nghiệm thu

### M0 — Xác thực loa và thẻ SD, chưa dùng micro

1. Chốt sơ đồ chân và nguồn; ghi loại board, loại loa/ampli I2S, dung lượng và định dạng thẻ SD. Tạo cấu hình kiểm tra tạm thời không khởi tạo task micro để cô lập phép thử.
2. Flash firmware hiện tại, xác nhận log gắn SD thành công. Tạo hai WAV hợp lệ trên thẻ: một file mono và một file stereo để kiểm tra khả năng đọc định dạng; cả hai đều phát qua **cùng một loa**.
3. Phát WAV nhúng, phát từng WAV từ SD qua giao diện hiện tại; nghe thực tế loa phát rõ, không rè hoặc im tiếng. Dừng giữa bài và phát lại nhiều lần. Thử file stereo ở đây là kiểm tra trộn xuống mono, không phải kiểm tra hai loa.
4. Đọc và so SHA-256 của một tệp từ SD với bản gốc; thử thẻ vắng mặt, thẻ hỏng file, tháo/lắp lại lúc không phát và báo lỗi rõ ràng.
5. Lặp ít nhất 50 lượt phát → dừng → phát, ghi log lỗi I2S/SD và bộ nhớ heap thấp nhất. Chưa gọi chức năng micro.

**Đạt khi:** loa phát và dừng được, dữ liệu đọc đúng, trạng thái web khớp âm đang phát, không treo hoặc rò bộ nhớ tăng đều. Nếu không đạt, sửa phần cứng/player/SD trước khi sang M1.

### M1 — Báo thức cục bộ

Đã có firmware và giao diện web local để thao tác tại `/alarms`; kết quả thử trên board nằm trong [M1_RESULT.md](M1_RESULT.md). Kiểm tra mất Wi-Fi lâu sau đồng bộ giờ vẫn là hạng mục nghiệm thu còn lại.

1. Tách bộ lập lịch khỏi web server; định nghĩa alarm model, lưu lịch trong NVS với `revision` và CRC/phiên bản schema.
2. Đồng bộ giờ, tính lần reo tiếp theo theo múi giờ và ngày lặp; kiểm tra qua nửa đêm, đổi ngày, khởi động lại, cùng giờ nhiều báo thức.
3. Thêm bộ điều phối audio duy nhất cho báo thức, phát thử, phát ngay, dừng, báo lại; xác định ưu tiên để lệnh mới không ghi đè sai trạng thái.
4. Thêm nút vật lý **Phát ngay** và thao tác dừng. Dùng âm mặc định nhúng nếu SD hoặc file được chọn không sẵn sàng.

**Đạt khi:** tạo/sửa/xóa ít nhất 20 báo thức, reo đúng giờ khi mất Wi-Fi sau khi đã đồng bộ giờ, không reo trùng sau reboot, nút vật lý dùng được khi offline.

### M2 — Firebase và Android

1. Tạo dự án Android Kotlin/Jetpack Compose, Firebase project, môi trường dev và bộ quy tắc truy cập Database/Storage.
2. Làm đăng nhập, ghép thiết bị bằng mã dùng một lần, danh sách báo thức, chọn âm, nút phát ngay/dừng, trạng thái đồng bộ.
3. Làm endpoint HTTPS ghép/xác thực ESP bằng khóa riêng từng thiết bị, cấp Custom Token, hỗ trợ thu hồi khóa và ngăn gửi lại yêu cầu. ESP kiểm tra chứng chỉ TLS, đổi và làm mới Firebase ID token.
4. ESP dùng Realtime Database REST streaming để nhận cấu hình/lệnh, gửi ACK/trạng thái qua REST; thử tự kết nối lại, lấy bản đầy đủ sau gián đoạn, gửi lặp, cập nhật nhanh liên tiếp và một tài khoản có nhiều thiết bị. Đo độ trễ lệnh **Phát ngay**; mục tiêu ban đầu ≤ 5 giây khi mạng ổn định.

**Đạt khi:** sửa báo thức trên Android xuất hiện trên loa và có `appliedRevision`; lệnh phát ngay/dừng từ app được xác nhận và không thực hiện hai lần; lịch cũ vẫn reo khi Firebase mất kết nối.

### M3 — Thư viện âm và thu âm

1. Android tải WAV lên Storage; backend kiểm tra quyền, kích thước/định dạng. ESP tải về SD theo file tạm và kiểm tra hash.
2. Chọn âm báo riêng cho từng báo thức, âm phát ngay riêng và âm mặc định. Xử lý file bị xóa hoặc SD đầy bằng âm nhúng.
3. Thu âm bằng micro Android tối đa 30 giây. Nếu người dùng chọn bản ghi dài hơn, chỉ xử lý 30 giây đầu; chuẩn hóa WAV PCM 16 bit rồi mới tải lên Storage. Không bật task micro cũ trên ESP32.
4. Sau khi thu, cho nghe thử, đặt tên, chọn làm chuông và đồng bộ metadata/tệp nếu cần dùng trên nhiều thiết bị.

**Đạt khi:** thu, phát lại, chọn bản thu cho một báo thức, khởi động lại vẫn giữ bản thu; lỗi SD/micro không làm hỏng lịch hoặc dừng các báo thức khác.

### M4 — Hoàn thiện

- Kiểm tra trường hợp mất Wi-Fi, mất Firebase, SD bị rút, file lỗi, điện chập chờn, hai lệnh cùng lúc và báo thức đúng lúc đang nghe thử.
- Đo độ lệch giờ reo, thời gian phản hồi lệnh từ app, heap tối thiểu và số lần ghi NVS/SD; chỉnh chu kỳ đồng bộ theo kết quả đo.
- Bổ sung cập nhật firmware OTA có kiểm tra chữ ký hoặc cơ chế rollback trước khi phát hành nhiều thiết bị; cập nhật partition table hiện chỉ có một phân vùng app `factory`.
- Viết hướng dẫn ghép thiết bị, cấu hình Wi-Fi, quản lý âm báo, khôi phục sau mất mạng và kiểm tra phần cứng.

## 6. Quyết định cần chốt trước khi triển khai M2/M3

- **Đã chốt:** chưa yêu cầu chạy sau mất điện khi khởi động lại không có Wi-Fi; xem xét RTC sau khi các phần khác ổn định.
- **Đã chốt:** thu âm bằng micro điện thoại Android, không dùng micro ESP32.
- Chốt loa/ampli, chân GPIO còn trống, nút bấm riêng, nguồn và loại thẻ SD bằng kiểm tra M0.
- **Đã chốt cho giai đoạn thử:** một tài khoản, tối đa 20 báo thức/thiết bị, tối đa 30 giây âm báo; phần dài hơn bị cắt sau giây 30. Giới hạn số thiết bị và dung lượng tệp chốt sau khi đo SD, PSRAM và chi phí Firebase.

## 7. Tài liệu tham khảo cho lựa chọn Firebase

- [Firebase Realtime Database REST API](https://firebase.google.com/docs/reference/rest/database/): ESP có thể trao đổi dữ liệu qua HTTPS, nhưng kế hoạch dùng Cloud Functions làm lớp xác thực thiết bị.
- [Realtime Database REST streaming](https://firebase.google.com/docs/database/rest/retrieve-data#section-streaming): nhận thay đổi mà không phải gọi Cloud Functions liên tục.
- [Firebase Custom Tokens](https://firebase.google.com/docs/auth/admin/create-custom-tokens) và [Auth REST API](https://firebase.google.com/docs/reference/rest/auth): cấp và làm mới danh tính giới hạn cho ESP.
- [Firebase Database Security Rules](https://firebase.google.com/docs/database/security/): quyền đọc/ghi theo tài khoản và thiết bị.
- [Cloud Storage for Firebase trên Android](https://firebase.google.com/docs/storage/android/upload-files): tải WAV từ app.
- [Cloud Functions for Firebase](https://firebase.google.com/docs/functions/get-started) và [yêu cầu gói Blaze cho Storage](https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024): điều kiện triển khai và chi phí cần theo dõi.
