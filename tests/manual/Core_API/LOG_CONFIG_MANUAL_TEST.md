# Hướng dẫn: Cấu hình ghi log qua API (hệ thống + theo từng instance)

> Tài liệu dành cho **người vận hành / không cần biết lập trình**: copy lệnh vào terminal (hoặc dùng Postman/Swagger) và làm theo từng bước. **Cấu hình áp dụng ngay** — không cần restart server (trừ khi đổi mức log chi tiết có thể cần restart).

---

## Mục đích

- **Log hệ thống**: Bật/tắt ghi log cho **API** (request/response), **Instance** (start/stop instance), **SDK output** (kết quả xử lý). Có thể đổi **mức log** (info, debug, warning, error…). Điều khiển toàn bộ qua API.
- **Log theo từng instance**: Một số instance có thể **bật ghi log riêng** vào thư mục `logs/instance/<tên_instance>/`; instance khác không bật thì không có thư mục riêng. Tiện khi chỉ cần xem log của vài instance cụ thể.

---

## Chuẩn bị

1. **API đang chạy** (ví dụ: `http://localhost:8080`).
2. **Terminal** (Linux/Mac) hoặc **Command Prompt / PowerShell** (Windows), hoặc **Postman** / **Swagger** (`http://<máy-chạy-API>:8080/v1/swagger`).
3. Nếu dùng **curl**: Thay `http://localhost:8080` bằng địa chỉ API thực tế nếu khác.
4. (Tùy chọn) Có ít nhất **một instance** để test log theo instance (tạo instance trước nếu chưa có).

---

# PHẦN A: Cấu hình log hệ thống

## A.1. Hai API cần dùng

| Thao tác | Phương thức | Đường dẫn | Mô tả |
|----------|-------------|-----------|--------|
| **Xem cấu hình log** | **GET** | `/v1/core/log/config` | Xem bật/tắt từng loại log và mức log. |
| **Sửa cấu hình log** | **PUT** | `/v1/core/log/config` | Gửi JSON để bật/tắt hoặc đổi mức log; **áp dụng ngay** (trừ mức log có thể cần restart). |

---

## A.2. Bước 1: Xem cấu hình log hiện tại

Gửi request **GET** (không cần body):

```bash
curl -s "http://localhost:8080/v1/core/log/config"
```

Ví dụ response (rút gọn):

```json
{
  "config": {
    "enabled": true,
    "log_level": "info",
    "api_enabled": false,
    "instance_enabled": false,
    "sdk_output_enabled": false,
    "log_dir": "./logs",
    "current_log_dir": "/opt/omniapi/logs",
    "log_file": "logs/api.log",
    "retention_days": 30,
    "_description": {
      "enabled": "Master switch: when false, all logging categories are effectively off.",
      "log_level": "Minimum level: none, fatal, error, warning, info, debug, verbose. Restart may be required for full effect.",
      "api_enabled": "Log API requests/responses (applies immediately).",
      "instance_enabled": "Log instance start/stop/status (applies immediately).",
      "sdk_output_enabled": "Log SDK output when instances process data (applies immediately)."
    }
  }
}
```

**Giải thích nhanh:**

| Trường | Ý nghĩa | Giá trị thường dùng |
|--------|--------|----------------------|
| **enabled** | Công tắc chung: tắt thì coi như tất cả loại log đều tắt | `true` / `false` |
| **log_level** | Mức log: càng “cao” càng ít dòng (none &lt; fatal &lt; error &lt; warning &lt; info &lt; debug &lt; verbose) | `"info"`, `"debug"`, `"warning"`, `"error"` |
| **api_enabled** | Ghi log mọi request/response của API | `true` / `false` |
| **instance_enabled** | Ghi log khi start/stop instance, đổi trạng thái instance | `true` / `false` |
| **sdk_output_enabled** | Ghi log kết quả xử lý từ SDK (rất nhiều log, chỉ bật khi debug) | `true` / `false` |
| **log_dir** | Thư mục log đã cấu hình (config/env) | Ví dụ: `"./logs"`, `"/opt/omniapi/logs"` |
| **current_log_dir** | **Thư mục thực tế đang ghi log** — dùng để kiểm tra log có lưu đúng vị trí không | Ví dụ: `"/opt/omniapi/logs"` — nếu khác mong muốn, set `LOG_DIR` và restart |

**Kiểm tra log có lưu đúng vị trí:** Gọi GET `/v1/core/log/config` và xem trường **`current_log_dir`**. Đó chính là thư mục đang ghi log (api/, general/, instance/, sdk_output/ nằm bên trong). Nếu khác với vị trí mong muốn, cấu hình `LOG_DIR` khi khởi động server và restart.

---

## A.3. Bước 2: Chỉnh cấu hình (PUT)

Gửi request **PUT**, body là **JSON** chỉ chứa **những trường bạn muốn đổi**. Trường không gửi sẽ giữ nguyên. **Bật/tắt từng loại log có hiệu lực ngay.**

### Kịch bản 1: Bật log API (ghi mọi request/response)

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "api_enabled": true
  }'
```

Sau khi thành công, gọi vài API (ví dụ GET health, list instance) rồi mở file log trong thư mục `logs/api/` (tên file dạng `YYYY-MM-DD.log`) để xem dòng log API.

---

### Kịch bản 2: Bật log instance (start/stop instance)

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "instance_enabled": true
  }'
```

Sau đó start hoặc stop một instance; log sẽ xuất hiện trong `logs/instance/YYYY-MM-DD.log`.

---

### Kịch bản 3: Bật cả log API và log instance

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "api_enabled": true,
    "instance_enabled": true
  }'
```

---

### Kịch bản 4: Đổi mức log (ví dụ sang debug)

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{
    "log_level": "debug"
  }'
```

**Lưu ý:** Đổi `log_level` có thể cần **restart server** mới áp dụng đầy đủ. Bật/tắt `api_enabled`, `instance_enabled`, `sdk_output_enabled` thì **không cần restart**.

---

### Kịch bản 5: Tắt toàn bộ log (hoặc chỉ tắt từng loại)

Tắt hết:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": false
  }'
```

Chỉ tắt log API, giữ log instance:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{
    "api_enabled": false,
    "instance_enabled": true
  }'
```

---

## A.4. Kiểm tra sau khi chỉnh

1. **Xem lại cấu hình:** Gọi lại GET `/v1/core/log/config` và kiểm tra các trường đã đúng.
2. **Xem danh sách file log:**  
   **GET** `/v1/core/log` — trả về danh sách file log theo từng category (api, instance, sdk_output, general).
3. **Xem nội dung một file log:**  
   **GET** `/v1/core/log/{category}/{date}` (ví dụ: `/v1/core/log/api/2025-03-09`) với query `tail=50` để lấy 50 dòng cuối.

Ví dụ:

```bash
# Danh sách file log
curl -s "http://localhost:8080/v1/core/log"

# 50 dòng cuối của log API ngày 2025-03-09
curl -s "http://localhost:8080/v1/core/log/api/2025-03-09?tail=50"
```

(Thay `2025-03-09` bằng ngày hiện tại nếu cần.)

---

# PHẦN B: Cấu hình log riêng cho từng instance

Khi bật **log riêng** cho một instance, log của instance đó sẽ ghi thêm vào thư mục **`logs/instance/<instance_id>/`** (file dạng `YYYY-MM-DD.log`). Instance không bật thì không có thư mục riêng.

## B.1. Hai API cần dùng

| Thao tác | Phương thức | Đường dẫn | Mô tả |
|----------|-------------|-----------|--------|
| **Xem cấu hình log của instance** | **GET** | `/v1/core/instance/{instanceId}/log/config` | Xem instance này có bật ghi log riêng hay không. |
| **Bật/tắt log riêng cho instance** | **PUT** | `/v1/core/instance/{instanceId}/log/config` | Gửi `{"enabled": true}` hoặc `{"enabled": false}`; **áp dụng ngay**. |

**Lưu ý:** `{instanceId}` là **ID thật** của instance (ví dụ: `abc-123-def-456`). Bạn có thể lấy danh sách instance bằng **GET** `/v1/core/instance`.

---

## B.2. Bước 1: Lấy ID của instance (nếu chưa biết)

```bash
curl -s "http://localhost:8080/v1/core/instance"
```

Trong response, tìm trường **`instanceId`** (hoặc `id`) của instance cần cấu hình. Ví dụ: `"instanceId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890"`.

---

## B.3. Bước 2: Xem cấu hình log của instance

Thay `{instanceId}` bằng ID thật:

```bash
curl -s "http://localhost:8080/v1/core/instance/{instanceId}/log/config"
```

Ví dụ với ID `a1b2c3d4-e5f6-7890-abcd-ef1234567890`:

```bash
curl -s "http://localhost:8080/v1/core/instance/a1b2c3d4-e5f6-7890-abcd-ef1234567890/log/config"
```

Response mẫu:

```json
{
  "config": {
    "enabled": false
  },
  "_description": {
    "enabled": "When true, instance logs are written to logs/instance/<instance_id>/ (per-instance folder)."
  }
}
```

- **`enabled: false`** — chưa bật log riêng cho instance này.
- **`enabled: true`** — đã bật; log ghi vào `logs/instance/<instance_id>/`.

---

## B.4. Bước 3: Bật log riêng cho instance

Thay `{instanceId}` bằng ID thật:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/instance/{instanceId}/log/config" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'
```

Ví dụ:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/instance/a1b2c3d4-e5f6-7890-abcd-ef1234567890/log/config" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'
```

Sau khi thành công, khi instance đó start (hoặc có sự kiện được ghi), log sẽ xuất hiện trong thư mục `logs/instance/<instance_id>/YYYY-MM-DD.log`.

---

## B.5. Bước 4: Tắt log riêng cho instance

```bash
curl -s -X PUT "http://localhost:8080/v1/core/instance/{instanceId}/log/config" \
  -H "Content-Type: application/json" \
  -d '{"enabled": false}'
```

---

## B.6. Kiểm tra log theo instance

1. Bật log riêng cho instance (PUT với `"enabled": true`).
2. Start instance đó (nếu đang dừng): **POST** `/v1/core/instance/{instanceId}/start`.
3. Trên máy chạy API, mở thư mục `logs/instance/<instance_id>/` và xem file `YYYY-MM-DD.log` (ngày hiện tại). Hoặc dùng **GET** `/v1/core/log` để xem danh sách category; thư mục instance riêng nằm dưới dạng `instance/<id>/`.

---

# Tóm tắt nhanh

| Muốn làm gì | API | Ghi chú |
|-------------|-----|--------|
| Xem cấu hình log hệ thống | **GET** `/v1/core/log/config` | Không cần body. |
| Bật/tắt log API / instance / SDK | **PUT** `/v1/core/log/config` với `api_enabled`, `instance_enabled`, `sdk_output_enabled` | Áp dụng ngay. |
| Đổi mức log (info, debug, …) | **PUT** `/v1/core/log/config` với `log_level` | Có thể cần restart. |
| Xem log của instance có bật riêng không | **GET** `/v1/core/instance/{id}/log/config` | Thay `{id}` bằng ID instance. |
| Bật log riêng cho một instance | **PUT** `/v1/core/instance/{id}/log/config` với `{"enabled": true}` | Log ghi vào `logs/instance/<id>/`. |
| Tắt log riêng cho instance | **PUT** `/v1/core/instance/{id}/log/config` với `{"enabled": false}` | Áp dụng ngay. |
| Xem danh sách file log | **GET** `/v1/core/log` | Theo category. |

---

# Lỗi thường gặp và cách xử lý

| Tình huống | Cách xử lý |
|------------|------------|
| PUT trả về **400** | Body phải là JSON đúng (dấu ngoặc, dấu phẩy, ngoặc kép). Kiểm tra lại hoặc dùng Postman/Swagger. |
| PUT trả về **500** | Ghi config thất bại (quyền ghi file, đĩa đầy…). Xem log server. |
| GET/PUT instance log config trả về **404** | Instance không tồn tại. Kiểm tra ID bằng **GET** `/v1/core/instance`. |
| Bật log nhưng không thấy file mới | Kiểm tra current_log_dir trong GET /v1/core/log/config (thư mục thực tế đang ghi log); mặc định có thể là ./logs hoặc /opt/omniapi/logs. Tạo vài request hoặc start instance để có log. Đảm bảo `enabled: true` và đúng category đã bật. |
| Muốn chỉ vài instance có log riêng | Bật `instance_enabled` toàn hệ thống (hoặc không), rồi chỉ **PUT** `{"enabled": true}` cho từng instance cần log riêng. |

---

# Dùng Swagger / Postman thay vì curl

1. Mở trình duyệt: `http://<máy-chạy-API>:8080/v1/swagger` (Swagger) hoặc mở Postman.
2. **Log hệ thống:** Tìm **Log** → **GET /v1/core/log/config** (xem) và **PUT /v1/core/log/config** (sửa). Với PUT: nhấn "Try it out", điền body JSON (ví dụ `{"api_enabled": true}`), rồi Execute.
3. **Log theo instance:** Tìm **Instance** → **GET /v1/core/instance/{instanceId}/log/config** và **PUT /v1/core/instance/{instanceId}/log/config**. Điền đúng `instanceId` vào path và với PUT dùng body `{"enabled": true}` hoặc `{"enabled": false}`.
4. Sau khi Execute, kiểm tra response và (nếu cần) xem file trong thư mục log trên máy chạy API.

---

Nếu làm đúng từng bước trên, người mới / non-dev có thể tự bật tắt log và xem log theo từng instance mà không cần sửa code hay config file thủ công.
hể tự bật tắt log và xem log theo từng instance mà không cần sửa code hay config file thủ công.
