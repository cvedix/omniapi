# Tài liệu API: Cấu hình ghi log (cho người mới / non-dev)

Tài liệu này mô tả **từng API** một cách chi tiết: khi nào dùng, gửi gì, nhận gì. Không cần biết lập trình, chỉ cần biết gọi API (curl, Postman, Swagger).

**Địa chỉ gốc:** Thay `http://localhost:8080` bằng địa chỉ máy chạy API của bạn (ví dụ: `http://192.168.1.100:8080`).

---

## 1. Xem cấu hình log hệ thống

**Khi nào dùng:** Khi muốn xem hiện tại log đang bật loại nào (API, instance, SDK) và mức log là gì.

| Mục | Nội dung |
|-----|----------|
| **Phương thức** | GET |
| **URL** | `http://localhost:8080/v1/core/log/config` |
| **Body** | Không cần |
| **Query** | Không cần |

**Ví dụ curl:**

```bash
curl -s "http://localhost:8080/v1/core/log/config"
```

**Ví dụ response (ý nghĩa từng trường):**

| Trường trong `config` | Ý nghĩa |
|------------------------|--------|
| `enabled` | `true` = bật log nói chung; `false` = tắt hết. |
| `log_level` | Mức log: `"none"`, `"fatal"`, `"error"`, `"warning"`, `"info"`, `"debug"`, `"verbose"`. Càng “cao” (verbose/debug) càng nhiều dòng. |
| `api_enabled` | `true` = ghi log mọi request/response API. |
| `instance_enabled` | `true` = ghi log khi start/stop instance. |
| `sdk_output_enabled` | `true` = ghi log kết quả xử lý từ SDK (rất nhiều, chỉ bật khi debug). |
| `log_dir` | Thư mục chứa tất cả log (vd: `"./logs"`). |
| `log_file` | Tên file log mặc định. |
| `retention_days` | Số ngày giữ file log, sau đó tự xóa. |
| `_description` | Giải thích thêm từng trường (chỉ để đọc). |

---

## 2. Sửa cấu hình log hệ thống

**Khi nào dùng:** Khi muốn bật/tắt log API, log instance, log SDK, hoặc đổi mức log. **Bật/tắt có hiệu lực ngay**; đổi `log_level` đôi khi cần restart server.

| Mục | Nội dung |
|-----|----------|
| **Phương thức** | PUT |
| **URL** | `http://localhost:8080/v1/core/log/config` |
| **Header** | `Content-Type: application/json` |
| **Body** | JSON, chỉ cần gửi **các trường muốn đổi**. Trường không gửi giữ nguyên. |

**Các trường có thể gửi trong body:**

| Trường | Kiểu | Ý nghĩa | Ví dụ |
|--------|------|--------|--------|
| `enabled` | true/false | Bật/tắt toàn bộ log | `true` |
| `log_level` | chuỗi | Mức log (xem bảng trên) | `"info"`, `"debug"` |
| `api_enabled` | true/false | Bật/tắt log API | `true` |
| `instance_enabled` | true/false | Bật/tắt log instance | `true` |
| `sdk_output_enabled` | true/false | Bật/tắt log SDK output | `false` |
| `log_dir` | chuỗi | Thư mục log | `"./logs"` |
| `retention_days` | số | Số ngày giữ log | `30` |

**Ví dụ curl – chỉ bật log API:**

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{"api_enabled": true}'
```

**Ví dụ curl – bật log API và instance, mức debug:**

```bash
curl -s -X PUT "http://localhost:8080/v1/core/log/config" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "api_enabled": true,
    "instance_enabled": true,
    "log_level": "debug"
  }'
```

**Ví dụ response thành công:** Có `"message"` và phần `"config"` với các giá trị vừa đặt.

---

## 3. Xem cấu hình log của một instance

**Khi nào dùng:** Khi muốn biết instance này có đang bật **log riêng** (ghi vào thư mục theo tên instance) hay không.

| Mục | Nội dung |
|-----|----------|
| **Phương thức** | GET |
| **URL** | `http://localhost:8080/v1/core/instance/{instanceId}/log/config` |
| **Thay thế** | `{instanceId}` = **ID thật** của instance (vd: `a1b2c3d4-e5f6-7890-abcd-ef1234567890`). Lấy từ **GET** `/v1/core/instance`. |
| **Body** | Không cần |

**Ví dụ curl (thay ID bằng ID thật):**

```bash
curl -s "http://localhost:8080/v1/core/instance/a1b2c3d4-e5f6-7890-abcd-ef1234567890/log/config"
```

**Ví dụ response:**

- `config.enabled: false` — instance này **chưa** bật log riêng.
- `config.enabled: true` — instance này **đã** bật; log ghi vào `logs/instance/<instance_id>/`.

---

## 4. Bật hoặc tắt log riêng cho một instance

**Khi nào dùng:** Khi muốn **chỉ instance này** ghi log vào thư mục riêng `logs/instance/<instance_id>/`. Instance khác không bật thì không có thư mục riêng.

| Mục | Nội dung |
|-----|----------|
| **Phương thức** | PUT |
| **URL** | `http://localhost:8080/v1/core/instance/{instanceId}/log/config` |
| **Header** | `Content-Type: application/json` |
| **Body** | JSON: `{"enabled": true}` hoặc `{"enabled": false}` |

**Ví dụ curl – bật log riêng cho instance:**

```bash
curl -s -X PUT "http://localhost:8080/v1/core/instance/a1b2c3d4-e5f6-7890-abcd-ef1234567890/log/config" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'
```

**Ví dụ curl – tắt log riêng:**

```bash
curl -s -X PUT "http://localhost:8080/v1/core/instance/a1b2c3d4-e5f6-7890-abcd-ef1234567890/log/config" \
  -H "Content-Type: application/json" \
  -d '{"enabled": false}'
```

**Ví dụ response thành công:** Có `"message"` và `"config": {"enabled": true}` (hoặc false). Áp dụng ngay.

---

## 5. API bổ sung: Xem danh sách file log và nội dung log

Để **kiểm tra** sau khi bật log, có thể dùng:

| Mục đích | Phương thức | URL mẫu |
|----------|-------------|---------|
| Liệt kê file log theo category | GET | `http://localhost:8080/v1/core/log` |
| Lấy nội dung log (vd: 50 dòng cuối) | GET | `http://localhost:8080/v1/core/log/api/2025-03-09?tail=50` (thay `api` bằng `instance`, `sdk_output`, `general`; thay ngày nếu cần) |

---

## Bảng tóm tắt nhanh

| Muốn làm gì | Method | URL |
|-------------|--------|-----|
| Xem cấu hình log hệ thống | GET | `/v1/core/log/config` |
| Sửa cấu hình log hệ thống | PUT | `/v1/core/log/config` |
| Xem instance có bật log riêng không | GET | `/v1/core/instance/{instanceId}/log/config` |
| Bật/tắt log riêng cho instance | PUT | `/v1/core/instance/{instanceId}/log/config` |
| Xem danh sách file log | GET | `/v1/core/log` |
| Xem nội dung file log (có lọc) | GET | `/v1/core/log/{category}/{date}?tail=50` |

---

**Kết hợp với:** [LOG_CONFIG_MANUAL_TEST.md](./LOG_CONFIG_MANUAL_TEST.md) — hướng dẫn test thủ công từng bước, kịch bản copy-paste và kiểm tra kết quả.
