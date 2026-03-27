# Hướng dẫn test thủ công – Node API

> Kịch bản test cho **Node API**: list nodes, node theo ID, preconfigured, template, build-solution, stats. Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [List nodes](#list-nodes)
5. [Get node by ID](#get-node-by-id)
6. [Preconfigured nodes](#preconfigured-nodes)
7. [Template](#template)
8. [Build solution](#build-solution)
9. [Node stats](#node-stats)
10. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
11. [Troubleshooting](#troubleshooting)

---

## Tổng quan

| API | Mô tả |
|-----|--------|
| GET /v1/core/node | List nodes (filter: available, category) |
| GET /v1/core/node/{nodeId} | Chi tiết một node |
| GET /v1/core/node/preconfigured | List preconfigured nodes |
| GET /v1/core/node/preconfigured/available | Preconfigured available |
| GET /v1/core/node/stats | Thống kê node |
| GET /v1/core/node/template | List templates |
| GET /v1/core/node/template/{category} | Templates theo category |
| GET /v1/core/node/template/{templateId} | Chi tiết template |
| POST /v1/core/node/build-solution | Build solution từ node/template |

---

## Chuẩn bị

1. API server đang chạy (vd: http://localhost:8080).
2. curl, jq.

---

**URL dùng trong tài liệu:** http://localhost:8080 (sửa nếu server khác). Không dùng biến; mọi lệnh ghi URL đầy đủ.

---

## List nodes

### 1. GET /v1/core/node – List tất cả nodes

```bash
echo "=== GET /v1/core/node ==="
curl -s http://localhost:8080/v1/core/node | jq .
```

**Cần kiểm tra:** 200, JSON danh sách nodes.

### 2. Filter available=true

```bash
curl -s "http://localhost:8080/v1/core/node?available=true" | jq .
```

### 3. Filter category

Thay detection bằng category thật nếu có.

```bash
curl -s "http://localhost:8080/v1/core/node?category=detection" | jq .
```

---

## Get node by ID

### 4. GET /v1/core/node/{nodeId}

Thay node-1 bằng nodeId thật (lấy từ list bước 1).

```bash
curl -s http://localhost:8080/v1/core/node/node-1 | jq .
```

**Expected:** 200, JSON chi tiết node. **Lỗi:** 404.

---

## Preconfigured nodes

### 5. GET /v1/core/node/preconfigured

```bash
curl -s http://localhost:8080/v1/core/node/preconfigured | jq .
```

### 6. GET /v1/core/node/preconfigured/available

```bash
curl -s http://localhost:8080/v1/core/node/preconfigured/available | jq .
```

---

## Template

### 7. GET /v1/core/node/template – List templates

```bash
curl -s http://localhost:8080/v1/core/node/template | jq .
```

### 8. GET /v1/core/node/template/{category}

```bash
curl -s http://localhost:8080/v1/core/node/template/detection | jq .
```

### 9. GET /v1/core/node/template/{templateId}

```bash
curl -s http://localhost:8080/v1/core/node/template/template-id-1 | jq .
```

---

## Build solution

### 10. POST /v1/core/node/build-solution

Body theo spec (node/template + params). Ví dụ:

```bash
curl -s -X POST http://localhost:8080/v1/core/node/build-solution -H "Content-Type: application/json" -d '{"templateId":"...","parameters":{}}' | jq .
```

**Expected:** 200/201, JSON solution hoặc build result. Kiểm tra OpenAPI spec để biết body chính xác.

---

## Node stats

### 11. GET /v1/core/node/stats

```bash
curl -s http://localhost:8080/v1/core/node/stats | jq .
```

**Expected:** 200, JSON thống kê (số node, available, in use, ...).

---

## Bảng tóm tắt API

| Method | Path | Mô tả |
|--------|------|--------|
| GET | /v1/core/node | List nodes (?available=, ?category=) |
| GET | /v1/core/node/{nodeId} | Get node |
| GET | /v1/core/node/preconfigured | Preconfigured list |
| GET | /v1/core/node/preconfigured/available | Preconfigured available |
| GET | /v1/core/node/stats | Node stats |
| GET | /v1/core/node/template | List templates |
| GET | /v1/core/node/template/{category} | Templates by category |
| GET | /v1/core/node/template/{templateId} | Get template |
| POST | /v1/core/node/build-solution | Build solution |

---

## Troubleshooting

- **404 node/template:** Kiểm tra nodeId/templateId có trong list.
- **400 build-solution:** Kiểm tra body theo spec (templateId/nodeId, parameters).
