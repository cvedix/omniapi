# Hướng dẫn test thủ công – Instance FPS API

> Tài liệu test **FPS API**: GET/POST/DELETE cấu hình FPS theo instance. Dùng **URL cụ thể**; không dùng biến. API dùng base path **/api/v1/instances/** (không phải /v1/core/instance). Thay `http://localhost:8080` và `instance-id-that` bằng giá trị thật nếu khác.

---

## Mục lục

1. [Mục đích](#mục-đích)
2. [Chuẩn bị trước khi test](#chuẩn-bị-trước-khi-test)
3. [Lấy instance_id](#lấy-instance_id)
4. [Flow test (theo thứ tự)](#flow-test-theo-thứ-tự)
5. [Kịch bản lỗi](#kịch-bản-lỗi)
6. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
7. [Troubleshooting](#troubleshooting)

---

## Mục đích

- **GET** /api/v1/instances/{instance_id}/fps: Lấy FPS hiện tại của instance.
- **POST** /api/v1/instances/{instance_id}/fps: Đặt FPS (số nguyên >= 1).
- **DELETE** /api/v1/instances/{instance_id}/fps: Reset FPS về mặc định (5).

---

## Chuẩn bị trước khi test

| Hạng mục | Yêu cầu | Cách kiểm tra |
|----------|---------|----------------|
| API server | Đang chạy, ví dụ `http://localhost:8080`. | `curl -s http://localhost:8080/v1/core/health` → 200. |
| Công cụ | curl, jq. | `curl --version`, `jq --version`. |
| Instance | Ít nhất một instance đã tồn tại (UUID). | Gọi GET http://localhost:8080/v1/core/instance → có ít nhất một phần tử trong `instances`. |

---

## Lấy instance_id

Trước khi chạy flow, lấy một `instanceId` từ list instance (dùng cho mọi URL có `{instance_id}`):

```bash
curl -s http://localhost:8080/v1/core/instance | jq -r '.instances[0].instanceId'
```

Copy giá trị in ra (vd: `550e8400-e29b-41d4-a716-446655440000`) và thay vào chỗ **instance-id-that** trong các lệnh bên dưới. Hoặc dùng trực tiếp trong curl nếu bạn set vào shell (ví dụ trong doc dùng literal `instance-id-that` để bạn thay thủ công).

---

## Flow test (theo thứ tự)

**Quy ước:** Trong ví dụ dùng `instance-id-that` làm placeholder; bạn thay bằng instance_id thật (vd: `550e8400-e29b-41d4-a716-446655440000`).

### Bước 0. Kiểm tra server và instance tồn tại

```bash
curl -s http://localhost:8080/v1/core/health | jq .
curl -s http://localhost:8080/v1/core/instance | jq '.instances | length'
```

**Cần kiểm tra:** Health 200; list instance có ít nhất 1 phần tử. Ghi lại `instanceId` dùng cho các bước sau.

---

### Bước 1. GET FPS (hiện tại)

```bash
echo "=== GET FPS ==="
curl -s http://localhost:8080/api/v1/instances/instance-id-that/fps | jq .
```

**Thay `instance-id-that`** bằng instance_id thật (vd: `550e8400-e29b-41d4-a716-446655440000`).

**Cần kiểm tra:** HTTP 200. JSON có `instance_id` và `fps` (số). Nếu 404 → instance_id sai hoặc instance chưa tồn tại.

---

### Bước 2. POST set FPS = 10

```bash
echo "=== POST set FPS = 10 ==="
curl -s -X POST http://localhost:8080/api/v1/instances/instance-id-that/fps \
  -H "Content-Type: application/json" \
  -d '{"fps":10}' | jq .
```

**Thay `instance-id-that`** bằng instance_id thật.

**Cần kiểm tra:** HTTP 200. JSON có `message`, `instance_id`, `fps` = 10. Nếu 400 → fps không hợp lệ (phải >= 1). Nếu 404 → instance_id sai.

---

### Bước 3. GET FPS lại – xác nhận đã đổi

```bash
curl -s http://localhost:8080/api/v1/instances/instance-id-that/fps | jq .
```

**Cần kiểm tra:** `fps` = 10 (đúng giá trị vừa set).

---

### Bước 4. DELETE reset FPS về mặc định (5)

```bash
echo "=== DELETE reset FPS ==="
curl -s -X DELETE http://localhost:8080/api/v1/instances/instance-id-that/fps | jq .
```

**Cần kiểm tra:** HTTP 200. JSON có `message`, `instance_id`, `fps` = 5.

---

### Bước 5. GET FPS – xác nhận đã reset

```bash
curl -s http://localhost:8080/api/v1/instances/instance-id-that/fps | jq .
```

**Cần kiểm tra:** `fps` = 5.

---

## Kịch bản lỗi

### GET/POST/DELETE với instance_id không tồn tại (404)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" http://localhost:8080/api/v1/instances/00000000-0000-0000-0000-000000000000/fps
```

**Expected:** HTTP 404.

### POST FPS = 0 hoặc âm (400)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/api/v1/instances/instance-id-that/fps \
  -H "Content-Type: application/json" \
  -d '{"fps":0}'
```

**Expected:** HTTP 400 (fps phải >= 1). Thay `instance-id-that` bằng ID thật trước khi chạy.

---

## Bảng tóm tắt API

| Method | URL (ví dụ) | Body | Mô tả |
|--------|-------------|------|--------|
| GET | http://localhost:8080/api/v1/instances/{instance_id}/fps | - | Lấy FPS hiện tại |
| POST | http://localhost:8080/api/v1/instances/{instance_id}/fps | {"fps": 10} | Đặt FPS (>= 1) |
| DELETE | http://localhost:8080/api/v1/instances/{instance_id}/fps | - | Reset về 5 |

---

## Troubleshooting

| Triệu chứng | Nguyên nhân | Cách xử lý |
|-------------|-------------|------------|
| 404 mọi FPS API | instance_id sai hoặc instance chưa tạo | Lấy ID từ GET http://localhost:8080/v1/core/instance. |
| 400 POST | fps <= 0 hoặc không phải số nguyên | Gửi body {"fps": <số nguyên >= 1}. |
