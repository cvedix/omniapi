# Hướng dẫn test thủ công – Solution API

> Kịch bản test cho **Solution API**: list, create, get, update, delete solution; defaults; parameters; instance-body. Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [List / Create solution](#list--create-solution)
5. [Get / Update / Delete solution](#get--update--delete-solution)
6. [Defaults](#defaults)
7. [Parameters và instance-body](#parameters-và-instance-body)
8. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
9. [Troubleshooting](#troubleshooting)

---

## Tổng quan

| API | Mô tả |
|-----|--------|
| GET /v1/core/solution | List solutions |
| POST /v1/core/solution | Create custom solution |
| GET /v1/core/solution/{solutionId} | Chi tiết solution |
| PUT /v1/core/solution/{solutionId} | Update solution (chỉ custom) |
| DELETE /v1/core/solution/{solutionId} | Delete solution (chỉ custom) |
| GET /v1/core/solution/defaults | List default solutions |
| POST /v1/core/solution/defaults/{solutionId} | Load default solution vào registry |
| GET /v1/core/solution/{solutionId}/parameters | Tham số solution |
| GET/POST /v1/core/solution/{solutionId}/instance-body | Instance body mẫu / tạo instance từ solution |

---

## Chuẩn bị

1. API server đang chạy (vd: http://localhost:8080).
2. curl, jq.

---

**URL dùng trong tài liệu:** http://localhost:8080. Không dùng biến. Ví dụ solutionId: custom-test-solution (thay nếu trùng).

---

## List và Create solution

### 1. GET /v1/core/solution – List solutions

```bash
echo "=== GET /v1/core/solution ==="
curl -s http://localhost:8080/v1/core/solution | jq .
```

**Expected:** 200, JSON: solutions (array), total, default, custom.

### 2. POST /v1/core/solution – Create custom solution

Body: solutionId, solutionName, solutionType, pipeline (array nodes), defaults (optional).

```bash
curl -s -X POST http://localhost:8080/v1/core/solution -H "Content-Type: application/json" \
  -d '{"solutionId":"custom-test-solution","solutionName":"Custom Test","solutionType":"custom","pipeline":[],"defaults":{}}' | jq .
```

**Expected:** 201, JSON solution config. **Lỗi:** 400 validation, 409 đã tồn tại.

---

## Get, Update, Delete solution

### 3. GET /v1/core/solution/{solutionId}

```bash
curl -s http://localhost:8080/v1/core/solution/custom-test-solution | jq .
```

**Expected:** 200, JSON chi tiết (pipeline, defaults, ...). **Lỗi:** 404.

### 4. PUT /v1/core/solution/{solutionId} – Update (chỉ custom)

```bash
curl -s -X PUT http://localhost:8080/v1/core/solution/custom-test-solution -H "Content-Type: application/json" \
  -d '{"solutionName":"Custom Test Updated","pipeline":[]}' | jq .
```

**Expected:** 200. **Lỗi:** 400 nếu solution mặc định (default) không cho update.

### 5. DELETE /v1/core/solution/{solutionId} – Delete (chỉ custom)

```bash
curl -s -X DELETE http://localhost:8080/v1/core/solution/custom-test-solution | jq .
```

**Expected:** 200/204. **Lỗi:** 400 nếu default.

---

## Defaults

### 6. GET /v1/core/solution/defaults – List default solutions

```bash
curl -s http://localhost:8080/v1/core/solution/defaults | jq .
```

**Expected:** 200, JSON: solutions (id, name, description), total.

### 7. POST /v1/core/solution/defaults/{solutionId} – Load default vào registry

Thay solutionId bằng ID từ list defaults (vd: face_detection).

```bash
curl -s -X POST http://localhost:8080/v1/core/solution/defaults/face_detection | jq .
```

**Expected:** 200, JSON: solutionId, message. **Lỗi:** 404 không tìm thấy.

---

## Parameters và instance-body

### 8. GET /v1/core/solution/{solutionId}/parameters

```bash
curl -s http://localhost:8080/v1/core/solution/custom-test-solution/parameters | jq .
```

**Expected:** 200, JSON danh sách tham số (để điền khi tạo instance).

### 9. GET /v1/core/solution/{solutionId}/instance-body – Body mẫu tạo instance

```bash
curl -s http://localhost:8080/v1/core/solution/custom-test-solution/instance-body | jq .
```

**Expected:** 200, JSON body mẫu (name, solution, additionalParams, ...).

### 10. POST /v1/core/solution/{solutionId}/instance-body – Tạo instance từ solution (nếu hỗ trợ)

Kiểm tra OpenAPI spec; có thể cần body (name, overrides). Ví dụ:

```bash
curl -s -X POST http://localhost:8080/v1/core/solution/custom-test-solution/instance-body -H "Content-Type: application/json" \
  -d '{"name":"instance-from-solution"}' | jq .
```

---

## Bảng tóm tắt API

| Method | Path | Mô tả |
|--------|------|--------|
| GET | /v1/core/solution | List solutions |
| POST | /v1/core/solution | Create solution |
| GET | /v1/core/solution/{solutionId} | Get solution |
| PUT | /v1/core/solution/{solutionId} | Update (custom only) |
| DELETE | /v1/core/solution/{solutionId} | Delete (custom only) |
| GET | /v1/core/solution/defaults | List default solutions |
| POST | /v1/core/solution/defaults/{solutionId} | Load default |
| GET | /v1/core/solution/{solutionId}/parameters | Get parameters |
| GET/POST | /v1/core/solution/{solutionId}/instance-body | Instance body / create |

---

## Troubleshooting

- **409 Create:** solutionId đã tồn tại.
- **400 Update/Delete:** Solution mặc định không sửa/xóa được.
- **404:** solutionId sai hoặc chưa load default.
