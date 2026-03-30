# Hướng dẫn test thủ công – SecuRT Area API (toàn bộ loại area)

> Kịch bản test cho **SecuRT Area API**: areas chung và từng loại area (armedPerson, crossing, crowdEstimation, crowding, dwelling, faceCovered, fallenPerson, intrusion, loitering, objectEnterExit, objectLeft, objectRemoved, occupancy, vehicleGuard). Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [Areas chung và area theo ID](#areas-chung-và-area-theo-id)
5. [Các loại area (GET/POST/PUT/DELETE)](#các-loại-area-getpostputdelete)
6. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
7. [Troubleshooting](#troubleshooting)

---

## Tổng quan

Tất cả endpoint dùng base path: **/v1/securt/instance/{instanceId}/area/** hoặc **.../areas**.

| Loại area | Path (vd) | Mô tả |
|-----------|-----------|--------|
| areas | .../areas | List/CRUD areas chung |
| area/{areaId} | .../area/{areaId} | Get/Update/Delete một area theo ID |
| armedPerson | .../area/armedPerson | Vùng phát hiện người cầm vũ khí |
| crossing | .../area/crossing | Vượt line/area |
| crowdEstimation | .../area/crowdEstimation | Ước lượng đám đông |
| crowding | .../area/crowding | Mật độ đám đông |
| dwelling | .../area/dwelling | Ở lại lâu trong vùng |
| faceCovered | .../area/faceCovered | Che mặt |
| fallenPerson | .../area/fallenPerson | Ngã |
| intrusion | .../area/intrusion | Xâm nhập |
| loitering | .../area/loitering | Loitering (đã có LOITERING_SECURT_API_TEST) |
| objectEnterExit | .../area/objectEnterExit | Vật vào/ra vùng (đã có BA_AREA_ENTER_EXIT) |
| objectLeft | .../area/objectLeft | Vật bỏ quên |
| objectRemoved | .../area/objectRemoved | Vật bị lấy đi |
| occupancy | .../area/occupancy | Số người trong vùng |
| vehicleGuard | .../area/vehicleGuard | Giám sát xe |

---

## Chuẩn bị

1. API server đang chạy (vd: http://localhost:8080).
2. Đã có **SecuRT instance** (tạo qua POST http://localhost:8080/v1/securt/instance hoặc từ solution). Lấy instanceId từ response hoặc GET http://localhost:8080/v1/securt/instance.
3. curl, jq.

**Quy ước:** Trong ví dụ dùng **instance-id-that** làm placeholder; thay bằng instance_id SecuRT thật (vd: UUID) trong mọi URL dưới đây. Base URL: http://localhost:8080.

---

## Areas chung và area theo ID

### 1. GET /v1/securt/instance/{instanceId}/areas – List areas

```bash
echo "=== GET areas ==="
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/areas | jq .
```

**Cần kiểm tra:** 200, JSON danh sách areas. 404 = instance-id-that sai (thay bằng ID thật).

### 2. POST .../areas – Tạo area

Kiểm tra OpenAPI cho body từng type. Ví dụ intrusion:

```bash
curl -s -X POST http://localhost:8080/v1/securt/instance/instance-id-that/areas -H "Content-Type: application/json" \
  -d '{"type":"intrusion","name":"Zone A","polygon":[[0,0],[100,0],[100,100],[0,100]]}' | jq .
```

### 3. GET .../area/{areaId} – Chi tiết area

Thay area-1 bằng areaId thật (từ list areas).

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/area/area-1 | jq .
```

### 4. PUT .../area/{areaId} – Cập nhật area

```bash
curl -s -X PUT http://localhost:8080/v1/securt/instance/instance-id-that/area/area-1 -H "Content-Type: application/json" \
  -d '{"name":"Zone A Updated"}' | jq .
```

### 5. DELETE .../area/{areaId} – Xóa area

```bash
curl -s -X DELETE http://localhost:8080/v1/securt/instance/instance-id-that/area/area-1 | jq .
```

---

## Các loại area (GET/POST theo type)

Với mỗi loại (armedPerson, crossing, crowdEstimation, crowding, dwelling, faceCovered, fallenPerson, intrusion, loitering, objectEnterExit, objectLeft, objectRemoved, occupancy, vehicleGuard): GET .../area/{type} list, POST .../area/{type} tạo (body theo OpenAPI).

Ví dụ **loitering** (xem LOITERING_SECURT_API_TEST.md):

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/area/loitering | jq .
curl -s -X POST http://localhost:8080/v1/securt/instance/instance-id-that/area/loitering -H "Content-Type: application/json" \
  -d '{"name":"L1","polygon":[[0,0],[200,0],[200,200],[0,200]],"durationSeconds":30}' | jq .
```

Ví dụ **intrusion**:

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/area/intrusion | jq .
curl -s -X POST http://localhost:8080/v1/securt/instance/instance-id-that/area/intrusion -H "Content-Type: application/json" \
  -d '{"name":"I1","polygon":[[0,0],[100,0],[100,100],[0,100]]}' | jq .
```

Ví dụ **objectEnterExit**:

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/area/objectEnterExit | jq .
```

Tham khảo OpenAPI spec cho body chính xác (polygon, threshold, durationSeconds, ...).

---

## Bảng tóm tắt API

| Method | Path (pattern) | Mô tả |
|--------|----------------|--------|
| GET | .../areas | List all areas |
| POST | .../areas | Create area (body: type, polygon, ...) |
| GET | .../area/{areaId} | Get area by ID |
| PUT | .../area/{areaId} | Update area |
| DELETE | .../area/{areaId} | Delete area |
| GET/POST | .../area/armedPerson, .../crossing, .../crowdEstimation, ... | Theo từng type |
| GET/PUT/DELETE | .../area/{type}/{areaId} | CRUD theo type (nếu spec hỗ trợ) |

---

## Troubleshooting

- **404:** instanceId sai hoặc instance chưa start; areaId sai.
- **400:** Body thiếu polygon/type hoặc định dạng không đúng (xem OpenAPI).
- **409:** Tên area trùng hoặc giới hạn số area.

---

## Tài liệu liên quan

- LOITERING_SECURT_API_TEST.md – Loitering chi tiết.
- BA_AREA_ENTER_EXIT_API_TEST.md – Object enter/exit.
- SECURT_INSTANCE_WORKFLOW_TEST.md – Workflow SecuRT instance.
