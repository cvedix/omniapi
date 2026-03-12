# Hướng dẫn test thủ công – Log List API (theo category và date)

> Kịch bản test cho **Log List API**: list log files theo category, theo category + date. Không bao gồm log config (xem LOG_CONFIG_MANUAL_TEST.md). Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [List logs](#list-logs)
5. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
6. [Troubleshooting](#troubleshooting)

---

## Tổng quan

| API | Mô tả |
|-----|--------|
| GET /v1/core/log | List tất cả log files theo category (danh sách category hoặc file) |
| GET /v1/core/log/{category} | List/đọc log theo category (vd: api, general, instance) |
| GET /v1/core/log/{category}/{date} | List/đọc log theo category và ngày (vd: 2024-01-15) |

**Lưu ý:** Cấu hình log (bật/tắt, log_level) qua GET/PUT /v1/core/log/config – xem LOG_CONFIG_MANUAL_TEST.md và LOG_CONFIG_API_GUIDE.md.

---

## Chuẩn bị

1. API server đang chạy (vd: http://localhost:8080).
2. curl, jq.

---

**URL dùng trong tài liệu:** http://localhost:8080 (sửa nếu server khác). Không dùng biến; mọi lệnh ghi URL đầy đủ.

---

## Chuẩn bị theo bước

| Bước | Cần có |
|------|--------|
| Bước 0 | Server chạy |
| Bước 1–3 | Không cần log sẵn; category/date có thể 404 nếu chưa có log |

---

## List logs

### 1. GET /v1/core/log – List log (categories hoặc files)

```bash
echo "=== GET /v1/core/log ==="
curl -s http://localhost:8080/v1/core/log | jq .
```

**Cần kiểm tra:** HTTP 200. Body có thể là danh sách category hoặc danh sách file tùy implementation.

### 2. GET /v1/core/log/{category} – Log theo category

Category thường: api, general, instance, sdk_output (tùy server). Thay `api` nếu server dùng category khác.

```bash
echo "=== GET /v1/core/log/api ==="
curl -s http://localhost:8080/v1/core/log/api | jq .
```

**Cần kiểm tra:** 200; body JSON hoặc text (danh sách file hoặc nội dung). 404 = category không tồn tại.

### 3. GET /v1/core/log/{category}/{date} – Log theo category và ngày

Định dạng date thường YYYY-MM-DD. Thay 2024-01-15 bằng ngày có log (vd: ngày hôm nay).

```bash
echo "=== GET /v1/core/log/api/2024-01-15 ==="
curl -s http://localhost:8080/v1/core/log/api/2024-01-15 | jq .
```

**Cần kiểm tra:** 200 = có dữ liệu; 404 = chưa có log ngày đó.

---

## Bảng tóm tắt API

| Method | Path | Mô tả |
|--------|------|--------|
| GET | /v1/core/log | List log (categories/files) |
| GET | /v1/core/log/{category} | Log theo category |
| GET | /v1/core/log/{category}/{date} | Log theo category và date |

---

## Troubleshooting

| Triệu chứng | Cách xử lý |
|-------------|------------|
| 404 category/date | Category hoặc date không tồn tại; thử category khác (api, general, instance) hoặc ngày có log. |
| 400 | Kiểm tra định dạng date (thường YYYY-MM-DD) theo spec. |

---

## Tài liệu liên quan

- LOG_CONFIG_MANUAL_TEST.md – Cấu hình log (config).
- LOG_CONFIG_API_GUIDE.md – API reference log config.
