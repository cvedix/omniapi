# Hướng dẫn test thủ công – Model API

> Kịch bản test cho **Model API**: liệt kê, upload, đổi tên, xóa model. Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [List models](#list-models)
5. [Upload model](#upload-model)
6. [Rename model](#rename-model)
7. [Delete model](#delete-model)
8. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
9. [Troubleshooting](#troubleshooting)

---

## Tổng quan

| API | Mô tả |
|-----|--------|
| GET /v1/core/model/list | Liệt kê model (toàn cây hoặc theo thư mục) |
| POST /v1/core/model/upload | Upload model (ONNX, .weights, .pt, .tflite, ...) |
| PUT /v1/core/model/{modelName} | Đổi tên model |
| DELETE /v1/core/model/{modelName} | Xóa model |

Query `directory` (tùy chọn): thư mục con (vd: projects/myproject/models).

---

## Chuẩn bị

1. API server đang chạy (vd: http://localhost:8080).
2. curl, jq. Một file model (.onnx hoặc tương đương) để test upload.

---

**URL dùng trong tài liệu:** http://localhost:8080 (sửa nếu server khác host/port). Không dùng biến; mọi lệnh ghi URL đầy đủ.

---

## List models

### 1. GET /v1/core/model/list

```bash
echo "=== GET /v1/core/model/list ==="
curl -s http://localhost:8080/v1/core/model/list | jq .
```

**Expected:** 200, JSON: success, models (filename, path, size, modified), count, directory.

### 2. GET /v1/core/model/list?directory=... – Theo thư mục

```bash
curl -s "http://localhost:8080/v1/core/model/list?directory=projects/myproject/models" | jq .
```

---

## Upload model

### 3. POST /v1/core/model/upload – Upload một file

```bash
echo "=== POST /v1/core/model/upload ==="
curl -s -X POST http://localhost:8080/v1/core/model/upload -F "file=@/path/to/your/model.onnx" | jq .
```

**Expected:** 201, JSON: success, message, count, files (filename, path, size, url), warnings (nếu có).

### 4. Upload vào thư mục con

```bash
curl -s -X POST "http://localhost:8080/v1/core/model/upload?directory=detection/yolov8" -F "file=@/path/to/your/model.onnx" | jq .
```

---

## Rename model

### 5. PUT /v1/core/model/{modelName} – Đổi tên

Thay MyModel.onnx và MyModel_v2.onnx bằng tên thật.

```bash
echo "=== PUT /v1/core/model/MyModel.onnx (rename) ==="
curl -s -X PUT http://localhost:8080/v1/core/model/MyModel.onnx -H "Content-Type: application/json" -d '{"newName":"MyModel_v2.onnx"}' | jq .
```

**Expected:** 200, JSON: success, message, oldName, newName, path. **Lỗi:** 404, 409 (tên mới trùng).

---

## Delete model

### 6. DELETE /v1/core/model/{modelName} – Xóa model

```bash
echo "=== DELETE /v1/core/model/MyModel_v2.onnx ==="
curl -s -X DELETE http://localhost:8080/v1/core/model/MyModel_v2.onnx | jq .
```

**Expected:** 200, JSON: success, message, filename. **Lỗi:** 404.

---

## Bảng tóm tắt API

| Method | Path | Mô tả |
|--------|------|--------|
| GET | /v1/core/model/list | List models (?directory= optional) |
| POST | /v1/core/model/upload | Upload (?directory= optional) |
| PUT | /v1/core/model/{modelName} | Rename (body: newName) |
| DELETE | /v1/core/model/{modelName} | Delete |

---

## Troubleshooting

- **409 Upload:** File đã tồn tại; đổi tên hoặc dùng thư mục khác.
- **404 Rename/Delete:** Kiểm tra đúng modelName và directory (nếu dùng).
- **400 Upload:** Thiếu file hoặc định dạng không được hỗ trợ.
