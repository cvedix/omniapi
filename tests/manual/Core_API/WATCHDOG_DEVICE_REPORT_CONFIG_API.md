# Hướng dẫn cấu hình Device Report (Watchdog) qua API

> **Cấu hình áp dụng ngay khi PUT — không cần restart API server.**

Tài liệu này dành cho **người vận hành / không cần biết lập trình**: chỉ cần biết gọi API (curl, Postman, hoặc giao diện tương tự) và chỉnh cấu hình theo nhu cầu.

---

## Device Report là gì?

- **Device Report** là tính năng gửi thông tin thiết bị (CPU, RAM, ổ đĩa, IP, …) lên một server giám sát (Traccar, dùng giao thức OsmAnd).
- Bạn có thể **bật/tắt**, **đổi server**, **đổi thời gian gửi** (ví dụ 5 phút một lần), **đổi tên thiết bị**, v.v. **toàn bộ qua API**. **Cấu hình áp dụng ngay sau khi PUT — không cần restart server**, không cần sửa file cấu hình thủ công.

---

## Hai API cần dùng

| Thao tác | API | Mô tả ngắn |
|----------|-----|-------------|
| **Xem cấu hình hiện tại** | **GET** `/v1/core/watchdog/config` | Trả về toàn bộ cấu hình và giải thích từng trường. |
| **Sửa cấu hình** | **PUT** `/v1/core/watchdog/config` | Gửi JSON chỉ chứa các trường muốn đổi; **áp dụng ngay, không cần restart server**. |

**Lưu ý:** Thay `http://localhost:8080` bằng địa chỉ API thực tế của bạn nếu khác.

---

## Bước 1: Xem cấu hình hiện tại

Gửi request **GET** (không cần body):

```bash
curl -s "http://localhost:8080/v1/core/watchdog/config"
```

Ví dụ response (đã rút gọn):

```json
{
  "config": {
    "enabled": false,
    "server_url": "",
    "device_id": "",
    "device_type": "aibox",
    "interval_sec": 300,
    "latitude": 0.0,
    "longitude": 0.0,
    "reachability_timeout_sec": 10,
    "report_timeout_sec": 30,
    "_description": {
      "enabled": "Bật/tắt gửi report lên server (true/false).",
      "server_url": "URL server Traccar OsmAnd (vd. http://traccar:5055).",
      "device_id": "Mã thiết bị trên Traccar; để trống sẽ dùng hostname.",
      "device_type": "Loại thiết bị (vd. aibox, omnimedia).",
      "interval_sec": "Chu kỳ gửi report (giây). Mặc định 300 (5 phút).",
      "latitude": "Vĩ độ (thiết bị cố định).",
      "longitude": "Kinh độ (thiết bị cố định).",
      "reachability_timeout_sec": "Timeout kiểm tra server reachable (giây).",
      "report_timeout_sec": "Timeout gửi report (giây)."
    }
  }
}
```

- Phần **`config`**: giá trị đang dùng.
- Phần **`_description`**: giải thích từng trường, giúp bạn biết chỉnh trường nào.

---

## Bước 2: Chỉnh cấu hình (PUT)

Gửi request **PUT**, body là **JSON** chỉ chứa **những trường bạn muốn đổi**. Các trường không gửi sẽ giữ nguyên.

### Các trường có thể chỉnh

| Trường | Ý nghĩa | Ví dụ |
|--------|--------|--------|
| **enabled** | Bật hoặc tắt gửi report | `true` / `false` |
| **server_url** | Địa chỉ server Traccar (OsmAnd) | `"http://192.168.1.100:5055"` |
| **device_id** | Mã thiết bị trên Traccar; để trống = dùng hostname | `"may-camera-01"` |
| **device_type** | Loại thiết bị (phân loại trên Traccar) | `"aibox"` hoặc `"omnimedia"` |
| **interval_sec** | Bao nhiêu **giây** gửi một lần (ví dụ 5 phút = 300) | `300` |
| **latitude** | Vĩ độ (thiết bị cố định) | `21.0285` |
| **longitude** | Kinh độ (thiết bị cố định) | `105.8542` |
| **reachability_timeout_sec** | Timeout kiểm tra server có đang mở không (giây) | `10` |
| **report_timeout_sec** | Timeout khi gửi report (giây) | `30` |

---

### Ví dụ 1: Bật report và đặt server

Bật gửi report và trỏ tới server Traccar tại `http://192.168.1.100:5055`:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/watchdog/config" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "server_url": "http://192.168.1.100:5055"
  }'
```

Sau khi thành công, cấu hình mới **được áp dụng ngay** (không cần restart). Response sẽ có dạng:

```json
{
  "message": "Device report config updated and applied.",
  "config": { ... }
}
```

---

### Ví dụ 2: Tắt report

Chỉ cần gửi một trường:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/watchdog/config" \
  -H "Content-Type: application/json" \
  -d '{"enabled": false}'
```

---

### Ví dụ 3: Đổi thời gian gửi (ví dụ 5 phút một lần)

- 5 phút = 300 giây → `interval_sec: 300`
- 10 phút = 600 giây → `interval_sec: 600`

```bash
curl -s -X PUT "http://localhost:8080/v1/core/watchdog/config" \
  -H "Content-Type: application/json" \
  -d '{"interval_sec": 300}'
```

Chỉ đổi chu kỳ, các trường khác giữ nguyên.

---

### Ví dụ 4: Đổi server và tên thiết bị

Vừa đổi URL server vừa đặt mã thiết bị trên Traccar:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/watchdog/config" \
  -H "Content-Type: application/json" \
  -d '{
    "server_url": "http://traccar.congty.com:5055",
    "device_id": "camera-cua-so-1",
    "device_type": "aibox"
  }'
```

---

### Ví dụ 5: Đặt tọa độ (thiết bị cố định)

Dùng khi thiết bị lắp cố định và muốn hiển thị vị trí trên bản đồ Traccar:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/watchdog/config" \
  -H "Content-Type: application/json" \
  -d '{
    "latitude": 21.0285,
    "longitude": 105.8542
  }'
```

---

## Bước 3: Kiểm tra trạng thái sau khi chỉnh

- **Trạng thái tổng hợp** (watchdog + device report):  
  `GET /v1/core/watchdog`  
  Trong response, xem phần **`device_report`**: `enabled`, `server`, `report_count`, `last_status`, `server_reachable`, …

- **Gửi thử một report ngay** (không đợi đến chu kỳ):  
  `GET /v1/core/watchdog/report-now`  
  Response có `sent: true` là đã gửi thành công.

---

## Lỗi thường gặp và cách xử lý

| Tình huống | Cách xử lý |
|------------|------------|
| PUT trả về **400** | Body phải là JSON hợp lệ. Kiểm tra dấu ngoặc, dấu phẩy, dấu ngoặc kép. |
| PUT trả về **500** | Ghi config thất bại (quyền ghi file, đĩa đầy, …). Xem log API. |
| Bật report nhưng **không thấy dữ liệu trên Traccar** | Kiểm tra: (1) `server_url` đúng và Traccar đang chạy, (2) Trên Traccar đã tạo Device với **Identifier** trùng `device_id` (hoặc hostname nếu để trống). |
| **last_status = "network_error"** | API không kết nối được tới server (firewall, sai URL/port). Kiểm tra `server_url` và mạng. |
| Muốn **đổi chu kỳ** | Chỉ cần PUT một lần với `interval_sec` (đơn vị giây). Ví dụ 5 phút = `300`. |

---

## Tóm tắt nhanh

1. **Xem cấu hình:** `GET /v1/core/watchdog/config`
2. **Sửa cấu hình:** `PUT /v1/core/watchdog/config` với body JSON (chỉ gửi trường cần đổi).
3. Cấu hình **được lưu vào file** và **áp dụng ngay** (service device report tự reload với config mới) — **không cần restart API server**.
4. Mọi thứ user cần điều chỉnh (bật/tắt, server, thời gian, tên thiết bị, tọa độ, timeout) đều qua API này; có thể làm theo từng bước trên mà không cần kiến thức lập trình.
