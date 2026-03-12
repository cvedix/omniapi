# Hướng dẫn test thủ công – SecuRT Lines API

> Kịch bản test cho **SecuRT Line API**: lines, line counting, line crossing, tailgating (GET/POST/PUT/DELETE). Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [Lines chung](#lines-chung)
5. [Line counting](#line-counting)
6. [Line crossing](#line-crossing)
7. [Tailgating](#tailgating)
8. [Line theo lineId](#line-theo-lineid)
9. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
10. [Troubleshooting](#troubleshooting)

---

## Tổng quan

Base path: **/v1/securt/instance/{instanceId}/line/** hoặc **.../lines**.

| API | Path (vd) | Mô tả |
|-----|-----------|--------|
| lines | .../lines | List/create lines |
| line/{lineId} | .../line/{lineId} | Get/update/delete một line |
| counting | .../line/counting | Line đếm (counting) |
| counting/{lineId} | .../line/counting/{lineId} | CRUD line counting theo ID |
| crossing | .../line/crossing | Line vượt (crossing) |
| crossing/{lineId} | .../line/crossing/{lineId} | CRUD line crossing theo ID |
| tailgating | .../line/tailgating | Tailgating |
| tailgating/{lineId} | .../line/tailgating/{lineId} | CRUD tailgating theo ID |

---

## Chuẩn bị

1. API server đang chạy. Đã có **SecuRT instance**.
2. curl, jq. INSTANCE_ID = ID instance SecuRT.

---

**URL dùng trong tài liệu:** http://localhost:8080. Thay **instance-id-that** bằng instance_id SecuRT thật trong mọi lệnh. Không dùng biến.

---

## Lines chung

### 1. GET .../lines – List lines

```bash
echo "=== GET lines ==="
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/lines | jq .
```

**Cần kiểm tra:** 200, JSON danh sách lines. 404 = instance-id-that sai.

### 2. POST .../lines – Tạo line

Kiểm tra OpenAPI cho body. Ví dụ:

```bash
curl -s -X POST http://localhost:8080/v1/securt/instance/instance-id-that/lines -H "Content-Type: application/json" \
  -d '{"name":"L1","type":"counting","points":[[0,100],[200,100]]}' | jq .
```

---

## Line counting

### 3. GET .../line/counting

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/line/counting | jq .
```

### 4. POST .../line/counting

```bash
curl -s -X POST http://localhost:8080/v1/securt/instance/instance-id-that/line/counting -H "Content-Type: application/json" \
  -d '{"name":"Count1","line":[[0,100],[300,100]],"direction":"up"}' | jq .
```

### 5. GET/PUT/DELETE .../line/counting/{lineId}

Thay line-id-1 bằng lineId thật.

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/line/counting/line-id-1 | jq .
curl -s -X PUT http://localhost:8080/v1/securt/instance/instance-id-that/line/counting/line-id-1 -H "Content-Type: application/json" -d '{}' | jq .
curl -s -X DELETE http://localhost:8080/v1/securt/instance/instance-id-that/line/counting/line-id-1 | jq .
```

---

## Line crossing

### 6. GET .../line/crossing

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/line/crossing | jq .
```

### 7. POST .../line/crossing

```bash
curl -s -X POST http://localhost:8080/v1/securt/instance/instance-id-that/line/crossing -H "Content-Type: application/json" \
  -d '{"name":"Cross1","line":[[0,100],[300,100]]}' | jq .
```

### 8. GET/PUT/DELETE .../line/crossing/{lineId}

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/line/crossing/line-id-1 | jq .
curl -s -X PUT http://localhost:8080/v1/securt/instance/instance-id-that/line/crossing/line-id-1 -H "Content-Type: application/json" -d '{}' | jq .
curl -s -X DELETE http://localhost:8080/v1/securt/instance/instance-id-that/line/crossing/line-id-1 | jq .
```

---

## Tailgating

### 9. GET .../line/tailgating

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/line/tailgating | jq .
```

### 10. POST .../line/tailgating

```bash
curl -s -X POST http://localhost:8080/v1/securt/instance/instance-id-that/line/tailgating -H "Content-Type: application/json" \
  -d '{"name":"Tail1","line":[[0,100],[300,100]],"minGapMs":500}' | jq .
```

### 11. GET/PUT/DELETE .../line/tailgating/{lineId}

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/line/tailgating/line-id-1 | jq .
curl -s -X DELETE http://localhost:8080/v1/securt/instance/instance-id-that/line/tailgating/line-id-1 | jq .
```

---

## Line theo lineId

### 12. GET .../line/{lineId}

```bash
curl -s http://localhost:8080/v1/securt/instance/instance-id-that/line/line-id-1 | jq .
```

(Dùng khi API hỗ trợ lấy một line bất kỳ theo ID.)

---

## Bảng tóm tắt API

| Method | Path | Mô tả |
|--------|------|--------|
| GET | .../lines | List lines |
| POST | .../lines | Create line |
| GET | .../line/{lineId} | Get line by ID |
| GET | .../line/counting | List counting lines |
| POST | .../line/counting | Create counting line |
| GET/PUT/DELETE | .../line/counting/{lineId} | CRUD counting line |
| GET | .../line/crossing | List crossing lines |
| POST | .../line/crossing | Create crossing line |
| GET/PUT/DELETE | .../line/crossing/{lineId} | CRUD crossing line |
| GET | .../line/tailgating | List tailgating lines |
| POST | .../line/tailgating | Create tailgating line |
| GET/PUT/DELETE | .../line/tailgating/{lineId} | CRUD tailgating line |

---

## Troubleshooting

- **404:** instanceId sai hoặc lineId sai.
- **400:** Body thiếu line/points, name hoặc định dạng sai (xem OpenAPI).
- **409:** Tên line trùng.
