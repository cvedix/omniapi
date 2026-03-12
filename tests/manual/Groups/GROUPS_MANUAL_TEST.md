# Hướng dẫn test thủ công – Groups API

> Tài liệu test **Groups API**: tạo nhóm, sửa/xóa nhóm, gán instance vào nhóm. Làm theo **đúng thứ tự flow**; mỗi bước có chuẩn bị, lệnh curl và cách kiểm tra kết quả cụ thể.

---

## Mục lục

1. [Mục đích](#mục-đích)
2. [Chuẩn bị trước khi test](#chuẩn-bị-trước-khi-test)
3. [Flow test chính (bắt buộc theo thứ tự)](#flow-test-chính-bắt-buộc-theo-thứ-tự)
5. [Kịch bản bổ sung (gán instance vào group)](#kịch-bản-bổ-sung)
6. [Kịch bản lỗi (negative tests)](#kịch-bản-lỗi-negative-tests)
7. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
8. [Troubleshooting](#troubleshooting)
9. [Tài liệu liên quan](#tài-liệu-liên-quan)

---

## Mục đích

- **Groups** dùng để nhóm các instance (vd: theo camera, theo khu vực). Mỗi group có `groupId` (duy nhất), `groupName`, `description`, và danh sách instance thuộc group.
- **List:** Xem tất cả group (GET /v1/core/groups).
- **Create:** Tạo group mới (POST); `groupId` bắt buộc, pattern `^[A-Za-z0-9_-]+$`, `groupName`/`description` tùy chọn.
- **Get/Update/Delete:** Xem chi tiết, sửa tên/mô tả, xóa group (chỉ custom; group read-only không xóa được).
- **Instances trong group:** GET danh sách instance của group; PUT gán lại danh sách instance (instanceIds).

Sau flow chính bạn sẽ: list → tạo group → get chi tiết → update → get lại → (tùy chọn) gán instance → get instances → xóa group.

---

## Chuẩn bị trước khi test

### Bảng chuẩn bị

| Hạng mục | Yêu cầu cụ thể | Cách kiểm tra / chuẩn bị |
|----------|----------------|---------------------------|
| **1. API server** | Edge AI API đang chạy (vd: `http://localhost:8080`). | `curl -s http://localhost:8080/v1/core/health` → 200. |
| **2. Công cụ** | **curl**, **jq**. | `curl --version`, `jq --version`. Windows: PowerShell hoặc Postman. |
| **3. Instance (tùy chọn)** | Để test gán instance vào group, cần **ít nhất 1 instance** (Core instance). | Gọi `GET /v1/core/instance` → lấy một `instanceId` từ `instances[].instanceId`. Nếu chưa có, tạo bằng POST /v1/core/instance (xem INSTANCE_API_MANUAL_TEST.md). |

### Quy tắc groupId và groupName

- **groupId:** Chỉ chữ cái, số, gạch dưới, gạch ngang (`^[A-Za-z0-9_-]+$`). Ví dụ: `cameras`, `zone-a`, `test_manual`.
- **groupName:** Thường `^[A-Za-z0-9 -_]+$` (có thể có dấu cách). Ví dụ: `Security Cameras`, `Zone A`.
- **description:** Tùy ý, string.

### Cần chuẩn bị sẵn theo từng phase

| Phase | Cần có trước khi bắt đầu |
|-------|---------------------------|
| **Bước 0** | Chỉ cần server chạy. |
| **Bước 1 – List** | Không cần group nào. |
| **Bước 2 – Create** | Chọn `GROUP_ID` chưa tồn tại (vd: `test-group-manual-$(date +%s)` để tránh trùng). |
| **Bước 3 – Get** | Đã tạo group thành công (có groupId). |
| **Bước 4 – Update** | Group đã tạo và **không** read-only. |
| **Bước 5 – Get lại** | Đã update thành công. |
| **Bước 6 – Instances (PUT)** | Có groupId và (nếu muốn gán) ít nhất một instanceId hợp lệ từ GET /v1/core/instance. |
| **Bước 7 – Delete** | Group không read-only; có thể xóa group test. |

---

## Biến dùng chung

**URL trong tài liệu:** **http://localhost:8080**. Không dùng biến. Ví dụ groupId: **test-group-manual** (thay bằng tên khác nếu trùng). Mọi lệnh ghi URL đầy đủ.

**Kiểm tra group chưa tồn tại (trước khi tạo):**

```bash
curl -s http://localhost:8080/v1/core/groups | jq '.groups[] | select(.groupId == "test-group-manual")'
```
Nếu không có output thì an toàn để tạo mới.

---

## Flow test chính (bắt buộc theo thứ tự)

### Bước 0. Kiểm tra server và endpoint groups

**Mục đích:** Đảm bảo API sống và endpoint groups phản hồi.

```bash
echo "=== Bước 0: Health + GET /v1/core/groups ==="
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/v1/core/health
echo ""
curl -s http://localhost:8080/v1/core/groups | jq .
```

**Cần kiểm tra:**

- Health: HTTP **200**.
- GET groups: HTTP **200**; body có `groups` (array), `total` (số). Có thể `groups` rỗng.

---

### Bước 1. GET /v1/core/groups – List (baseline)

**Mục đích:** Ghi nhận số group và danh sách trước khi tạo mới.

```bash
echo "=== Bước 1: GET /v1/core/groups (baseline) ==="
curl -s http://localhost:8080/v1/core/groups | jq .
```

**Cần kiểm tra:**

- HTTP **200**.
- Có `total`, `groups` (array). Mỗi phần tử có ít nhất `groupId`, có thể có `groupName`, `description`, `instanceCount`, `isDefault`, `readOnly`. Ghi lại `total` (vd: `total_before`).

---

### Bước 2. POST /v1/core/groups – Tạo group

**Mục đích:** Tạo group test để dùng cho các bước sau.

```bash
echo "=== Bước 2: POST /v1/core/groups ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/groups \
  -H "Content-Type: application/json" \
  -d '{"groupId":"test-group-manual","groupName":"Test Group Manual","description":"Group for manual test"}' | tee /tmp/group_create_resp.txt
grep -v HTTP_CODE /tmp/group_create_resp.txt | jq .
```

**Cần kiểm tra:**

- HTTP **201**.
- Body có `groupId` = test-group-manual, `groupName`, `description` (hoặc tương đương); có thể có `instanceCount`, `isDefault`, `readOnly`, timestamps.

**Nếu 400:** `groupId` đã tồn tại hoặc không đúng pattern → chọn groupId khác (vd: thêm suffix) và chạy lại.

---

### Bước 3. GET /v1/core/groups/{groupId} – Chi tiết group

**Mục đích:** Xác nhận group vừa tạo và xem danh sách instance (ban đầu thường rỗng).

```bash
echo "=== Bước 3: GET /v1/core/groups/test-group-manual ==="
curl -s http://localhost:8080/v1/core/groups/test-group-manual | jq .
```

**Cần kiểm tra:**

- HTTP **200**.
- Body có `groupId`, `groupName`, `description`; có thể có `instanceIds` (array). Nếu mới tạo, `instanceIds` thường `[]`.

**Nếu 404:** groupId sai hoặc chưa tạo thành công; kiểm tra Bước 2.

---

### Bước 4. PUT /v1/core/groups/{groupId} – Cập nhật groupName, description

**Mục đích:** Sửa tên và mô tả group.

```bash
echo "=== Bước 4: PUT /v1/core/groups/test-group-manual ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X PUT http://localhost:8080/v1/core/groups/test-group-manual \
  -H "Content-Type: application/json" \
  -d '{"groupName":"Test Group Updated","description":"Updated description for manual test"}' | tee /tmp/group_update_resp.txt
grep -v HTTP_CODE /tmp/group_update_resp.txt | jq .
```

**Cần kiểm tra:**

- HTTP **200**.
- Body phản ánh `groupName` và `description` mới.

**Nếu 400:** Group có thể read-only (default group) → không sửa được; dùng group custom khác để test update.

---

### Bước 5. GET /v1/core/groups/{groupId} – Xác nhận sau update

**Mục đích:** Đảm bảo dữ liệu đã lưu.

```bash
echo "=== Bước 5: GET /v1/core/groups/test-group-manual (sau update) ==="
curl -s http://localhost:8080/v1/core/groups/test-group-manual | jq .
```

**Cần kiểm tra:**

- `groupName` = "Test Group Updated", `description` = "Updated description for manual test" (hoặc tương đương).

---

### Bước 6. GET và PUT /v1/core/groups/{groupId}/instances

**Mục đích:** Xem danh sách instance trong group; (tùy chọn) gán instance vào group.

**6a. GET instances trong group**

```bash
echo "=== Bước 6a: GET /v1/core/groups/test-group-manual/instances ==="
curl -s http://localhost:8080/v1/core/groups/test-group-manual/instances | jq .
```

**Expected:** 200; body có thể là array instance IDs hoặc object chứa `instanceIds`. Ban đầu rỗng nếu chưa gán.

**6b. PUT gán instance vào group (nếu có instance)**

Lấy một instanceId từ list instance (vd: đã tạo trước):

```bash
# Lấy instanceId đầu tiên (nếu có)
# Lấy instanceId từ list (nếu có). Thay instance-id-that bằng ID thật trong lệnh PUT bên dưới.
# curl -s http://localhost:8080/v1/core/instance | jq -r '.instances[0].instanceId'
echo "=== Bước 6b: PUT instances (thay instance-id-that bằng instanceId thật) ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X PUT http://localhost:8080/v1/core/groups/test-group-manual/instances \
  -H "Content-Type: application/json" \
  -d '{"instanceIds":["instance-id-that"]}' | tee /tmp/group_instances_resp.txt
  grep -v HTTP_CODE /tmp/group_instances_resp.txt | jq .
Nếu không có instance, bỏ qua 6b hoặc tạo instance trước (xem INSTANCE_API_MANUAL_TEST.md).
```

**Cần kiểm tra (khi có instance):** HTTP 200 hoặc 204. Gọi lại GET .../instances hoặc GET .../groups/{groupId} để thấy `instanceIds` không rỗng.

**Gán rỗng (xóa hết instance khỏi group):**

```bash
curl -s -X PUT http://localhost:8080/v1/core/groups/test-group-manual/instances -H "Content-Type: application/json" \
  -d '{"instanceIds":[]}' | jq .
```

---

### Bước 7. DELETE /v1/core/groups/{groupId} – Xóa group test

**Mục đích:** Dọn group test (chỉ khi group **không** read-only).

```bash
echo "=== Bước 7: DELETE /v1/core/groups/test-group-manual ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X DELETE http://localhost:8080/v1/core/groups/test-group-manual
```

**Cần kiểm tra:**

- HTTP **200** hoặc **204** (no content).
- Gọi lại GET /v1/core/groups → group có groupId = test-group-manual không còn trong list; `total` giảm 1.

**Nếu 400:** Group read-only → không xóa được; bỏ qua bước này hoặc dùng group custom khác để test delete.

---

## Kịch bản bổ sung

### Tạo nhiều group rồi list lại

```bash
for i in 1 2 3; do
  curl -s -X POST http://localhost:8080/v1/core/groups -H "Content-Type: application/json" \
    -d "{\"groupId\": \"test-multi-$i\", \"groupName\": \"Multi $i\"}" | jq -c .
done
curl -s http://localhost:8080/v1/core/groups | jq '.total, .groups | length'
```

Sau đó có thể xóa: curl -s -X DELETE http://localhost:8080/v1/core/groups/test-multi-1 (tương tự 2, 3).

---

## Kịch bản lỗi (negative tests)

### Tạo group với groupId trùng (409 hoặc 400)

```bash
# Tạo lần 1
curl -s -X POST http://localhost:8080/v1/core/groups -H "Content-Type: application/json" \
  -d '{"groupId":"dup-test","groupName":"Dup"}' | jq .
# Tạo lần 2 cùng groupId
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/groups -H "Content-Type: application/json" \
  -d '{"groupId":"dup-test","groupName":"Dup2"}'
```

**Expected:** Lần 2 trả **400** hoặc **409** với thông báo group đã tồn tại.

### Get group không tồn tại (404)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" http://localhost:8080/v1/core/groups/group-khong-ton-tai-12345
```

**Expected:** HTTP **404**.

### groupId không đúng pattern (400)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/groups -H "Content-Type: application/json" \
  -d '{"groupId": "có dấu cách và ký tự đặc biệt!", "groupName": "Bad"}'
```

**Expected:** HTTP **400** (validation failed).

---

## Bảng tóm tắt API

| Method | Path | Body / Query | Mô tả |
|--------|------|--------------|--------|
| GET | /v1/core/groups | - | List groups. |
| POST | /v1/core/groups | groupId (bắt buộc), groupName?, description? | Tạo group. |
| GET | /v1/core/groups/{groupId} | - | Chi tiết group + instanceIds. |
| PUT | /v1/core/groups/{groupId} | groupName?, description? | Update (custom only). |
| DELETE | /v1/core/groups/{groupId} | - | Xóa group (custom only). |
| GET | /v1/core/groups/{groupId}/instances | - | List instance trong group. |
| PUT | /v1/core/groups/{groupId}/instances | instanceIds (array) | Gán lại danh sách instance. |

---

## Troubleshooting

| Triệu chứng | Nguyên nhân có thể | Cách xử lý |
|-------------|--------------------|------------|
| 400 khi tạo group | groupId trùng hoặc sai pattern. | Đổi groupId (chỉ A-Za-z0-9_-). Kiểm tra GET groups xem đã có chưa. |
| 404 Get/Put/Delete | groupId sai hoặc đã xóa. | Kiểm tra chính tả, phân biệt hoa thường. |
| 400 Update/Delete | Group read-only (default). | Chỉ custom group mới sửa/xóa được. |
| PUT instances 400 | instanceIds chứa ID không tồn tại (một số implementation). | Chỉ dùng instanceId từ GET /v1/core/instance. |

---

## Tài liệu liên quan

- OpenAPI: paths `/v1/core/groups`, `/v1/core/groups/{groupId}`, `/v1/core/groups/{groupId}/instances`.
- INSTANCE_API_MANUAL_TEST.md: Tạo và quản lý instance (để lấy instanceId gán vào group).
