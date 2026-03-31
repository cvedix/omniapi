# Hướng dẫn test thủ công – Health, Version, License

> Tài liệu test **Health**, **Version**, **License check** và **License info**. Làm theo thứ tự flow; mỗi bước dùng URL cụ thể, không dùng biến. Nếu server chạy ở host/port khác, thay `http://localhost:8080` bằng URL thực tế.

---

## Mục lục

1. [Mục đích](#mục-đích)
2. [Chuẩn bị trước khi test](#chuẩn-bị-trước-khi-test)
3. [Flow test (theo thứ tự)](#flow-test-theo-thứ-tự)
4. [Kịch bản lỗi](#kịch-bản-lỗi)
5. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
6. [Troubleshooting](#troubleshooting)
7. [Tài liệu liên quan](#tài-liệu-liên-quan)

---

## Mục đích

- **Health:** Kiểm tra service còn sống và trạng thái healthy (GET /v1/core/health).
- **Version:** Lấy phiên bản API, build time, git commit (GET /v1/core/version).
- **License check:** Kiểm tra license có hợp lệ không (GET /v1/core/license/check).
- **License info:** Thông tin chi tiết license – loại, hạn, tính năng (GET /v1/core/license/info).

---

## Chuẩn bị trước khi test

| Hạng mục | Yêu cầu | Cách kiểm tra |
|----------|---------|----------------|
| API server | OmniAPI đang chạy, ví dụ tại `http://localhost:8080`. | Mở trình duyệt hoặc `curl -s http://localhost:8080/v1/core/health` → trả 200 và JSON. |
| Công cụ | **curl**, **jq** (tùy chọn). | `curl --version`, `jq --version`. |

**Lưu ý:** Nếu test từ máy khác, thay `http://localhost:8080` bằng `http://<host>:<port>` thực tế trong mọi lệnh bên dưới.

---

## Flow test (theo thứ tự)

### Bước 0. Kiểm tra server phản hồi

```bash
echo "=== Bước 0: Health ==="
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/v1/core/health
echo ""
curl -s http://localhost:8080/v1/core/health | jq .
```

**Cần kiểm tra:** HTTP 200. JSON có ít nhất một trong: `status` (vd: "healthy"), `timestamp`, `uptime`, `service`, `version`. Nếu 500 → service unhealthy, xem log server.

---

### Bước 1. GET /v1/core/version

```bash
echo "=== Bước 1: Version ==="
curl -s http://localhost:8080/v1/core/version | jq .
```

**Cần kiểm tra:** HTTP 200. JSON có ít nhất: `version`, `build_time`, `git_commit`, `api_version`, `service` (hoặc tương đương).

---

### Bước 2. GET /v1/core/license/check

```bash
echo "=== Bước 2: License check ==="
curl -s http://localhost:8080/v1/core/license/check | jq .
```

**Cần kiểm tra:** HTTP 200. JSON có `valid` (boolean) và `message` (string). Nếu hệ thống không dùng license, response có thể đơn giản hoặc mặc định.

---

### Bước 3. GET /v1/core/license/info

```bash
echo "=== Bước 3: License info ==="
curl -s http://localhost:8080/v1/core/license/info | jq .
```

**Cần kiểm tra:** HTTP 200. JSON có thể có: `licenseType`, `expirationDate`, `features` (array).

---

## Kịch bản lỗi

### Server không chạy (connection refused)

```bash
curl -s http://localhost:8080/v1/core/health
```

**Expected:** Lỗi kết nối (connection refused). Cách xử lý: start server, kiểm tra host/port.

### Health trả 500

**Expected:** Body có thể là JSON lỗi. Cách xử lý: xem log server để biết lỗi nội bộ.

---

## Bảng tóm tắt API

| Method | URL đầy đủ (ví dụ) | Mô tả |
|--------|---------------------|--------|
| GET | http://localhost:8080/v1/core/health | Health check |
| GET | http://localhost:8080/v1/core/version | Version info |
| GET | http://localhost:8080/v1/core/license/check | License validity |
| GET | http://localhost:8080/v1/core/license/info | License details |

---

## Troubleshooting

| Triệu chứng | Nguyên nhân | Cách xử lý |
|-------------|-------------|------------|
| Connection refused | Server chưa chạy hoặc sai host/port | Start server; kiểm tra URL (localhost vs IP). |
| Health 500 | Service unhealthy | Xem log server. |
| License 403/401 | Môi trường yêu cầu API key / cấu hình license | Cấu hình theo tài liệu triển khai. |

---

## Tài liệu liên quan

- SERVER_SETTINGS_MONITORING_MANUAL_TEST.md – Config, system, watchdog, metrics.
- OpenAPI: api-specs/openapi/en/paths/core/ (core_health, core_version); license trong openapi.yaml.
