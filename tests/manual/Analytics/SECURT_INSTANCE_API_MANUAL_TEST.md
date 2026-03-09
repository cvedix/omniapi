# Hướng dẫn test thủ công – SecuRT Instance API (instance, input, output, stats, …)

> Kịch bản test cho **SecuRT Instance API** (không bao gồm area/lines – xem SECURT_AREA_MANUAL_TEST, SECURT_LINES_MANUAL_TEST): list/create instance, get/update instance, input, output, stats, performance_profile, pip, analytics_entities, attributes_extraction, exclusion_areas, face_detection, feature_extraction, lpr, masking_areas, motion_area, surrender_detection. Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [Instance CRUD](#instance-crud)
5. [Input, output](#input-output)
6. [Stats, performance_profile, pip](#stats-performance_profile-pip)
7. [Analytics & extraction](#analytics--extraction)
8. [Areas & masking](#areas--masking)
9. [Experimental](#experimental)
10. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
11. [Troubleshooting](#troubleshooting)

---

## Tổng quan

Base path: **/v1/securt/instance** và **/v1/securt/instance/{instanceId}/...**.

| Nhóm | Path (vd) | Mô tả |
|------|-----------|--------|
| Instance | .../instance (GET/POST), .../instance/{id} (GET/PUT) | List, create, get, update |
| Input | .../instance/{id}/input | Cấu hình input |
| Output | .../instance/{id}/output | Cấu hình output |
| Stats | .../instance/{id}/stats | Thống kê instance |
| Performance | .../instance/{id}/performance_profile, .../pip | Performance, PIP |
| Analytics | .../analytics_entities, .../attributes_extraction | Entities, attributes |
| Face/LPR | .../face_detection, .../feature_extraction, .../lpr | Face, feature, LPR |
| Areas | .../exclusion_areas, .../masking_areas, .../motion_area | Vùng loại trừ, mask, motion |
| Experimental | .../experimental/instance/{id}/surrender_detection | Surrender detection |

---

## Chuẩn bị

1. API server đang chạy. curl, jq.
2. (Tùy chọn) Đã có SecuRT instance để test get/update/input/output.

---

**URL dùng trong tài liệu:** http://localhost:8080. Thay **instance-id-that** bằng instance_id SecuRT thật. Không dùng biến.

---

## Instance CRUD

### 1. GET /v1/securt/instance – List instances

```bash
echo "=== GET /v1/securt/instance ==="
curl -s http://localhost:8080/v1/securt/instance | jq .
```

**Cần kiểm tra:** 200, JSON danh sách SecuRT instances.

### 2. POST /v1/securt/instance – Create instance

```bash
curl -s -X POST http://localhost:8080/v1/securt/instance -H "Content-Type: application/json" \
  -d '{"name":"securt-test"}' | jq .
```

**Cần kiểm tra:** 201, JSON có instanceId hoặc chi tiết instance. Ghi lại instanceId cho các bước sau.

### 3. GET /v1/securt/instance/{instanceId}

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that | jq .
```

### 4. PUT /v1/securt/instance/{instanceId} – Update

```bash
curl -s -X PUT http://localhost:8080/v1/securt/instance/instance-id-that -H "Content-Type: application/json" \
  -d '{}' | jq .
```

---

## Input, output

### 5. GET/PUT .../instance/{instanceId}/input

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/input | jq .
curl -s -X PUT http://localhost:8080/v1/securt/instance/instance-id-that/input -H "Content-Type: application/json" \
  -d '{"type":"rtsp","url":"rtsp://..."}' | jq .
```

### 6. GET/PUT .../instance/{instanceId}/output

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/output | jq .
```

---

## Stats, performance_profile, pip

### 7. GET .../instance/{instanceId}/stats

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/stats | jq .
```

### 8. GET/PUT .../instance/{instanceId}/performance_profile

```bash
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/performance_profile" | jq .
```

### 9. GET/PUT .../instance/{instanceId}/pip

```bash
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/pip" | jq .
```

---

## Analytics và extraction

### 10. GET/POST/PUT/DELETE .../analytics_entities

```bash
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/analytics_entities" | jq .
```

### 11. GET/POST/PUT/DELETE .../attributes_extraction

```bash
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/attributes_extraction" | jq .
```

### 12. GET/POST .../face_detection, .../feature_extraction, .../lpr

```bash
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/face_detection" | jq .
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/feature_extraction" | jq .
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/lpr" | jq .
```

---

## Areas và masking

### 13. GET/POST/PUT/DELETE .../exclusion_areas, .../masking_areas, .../motion_area

```bash
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/exclusion_areas" | jq .
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/masking_areas" | jq .
curl -s "http://localhost:8080/v1/securt/instance/instance-id-that/motion_area" | jq .
```

(Chi tiết area theo type: xem SECURT_AREA_MANUAL_TEST.md.)

---

## Experimental

### 14. GET/POST .../experimental/instance/{instanceId}/surrender_detection

```bash
curl -s "http://localhost:8080/v1/securt/experimental/instance/instance-id-that/surrender_detection" | jq .
```

---

## Bảng tóm tắt API

| Method | Path | Mô tả |
|--------|------|--------|
| GET | /v1/securt/instance | List instances |
| POST | /v1/securt/instance | Create instance |
| GET | /v1/securt/instance/{instanceId} | Get instance |
| PUT | /v1/securt/instance/{instanceId} | Update instance |
| GET/PUT | .../input, .../output | Input/output config |
| GET | .../stats | Stats |
| GET/PUT | .../performance_profile, .../pip | Performance, PIP |
| GET/POST/PUT/DELETE | .../analytics_entities, .../attributes_extraction | Analytics |
| GET/POST | .../face_detection, .../feature_extraction, .../lpr | Face, feature, LPR |
| GET/POST/PUT/DELETE | .../exclusion_areas, .../masking_areas, .../motion_area | Areas |
| GET/POST | .../experimental/.../surrender_detection | Surrender (experimental) |

---

## Troubleshooting

- **404:** instanceId sai hoặc instance chưa tạo.
- **400:** Body thiếu field bắt buộc (xem OpenAPI từng endpoint).

---

## Tài liệu liên quan

- SECURT_INSTANCE_WORKFLOW_TEST.md – Workflow end-to-end.
- SECURT_AREA_MANUAL_TEST.md – Area APIs.
- SECURT_LINES_MANUAL_TEST.md – Line APIs.
