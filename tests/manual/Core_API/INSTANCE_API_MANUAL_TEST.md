# Hướng dẫn test thủ công – Core Instance API (toàn bộ endpoint)

> Kịch bản test cho **toàn bộ Core Instance API**: list/create/delete instance, lifecycle (load/start/stop/restart/unload), batch, config, input/output, lines, jams, stops, frame, preview, events, statistics, quick, status summary. Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [Instance CRUD và list](#instance-crud-và-list)
5. [Lifecycle: load, start, stop, restart, unload](#lifecycle-load-start-stop-restart-unload)
6. [Batch operations](#batch-operations)
7. [Config, input, output](#config-input-output)
8. [Lines, jams, stops](#lines-jams-stops)
9. [Frame, preview, push, consume_events](#frame-preview-push-consume_events)
10. [Statistics, state, classes](#statistics-state-classes)
11. [Quick, status summary](#quick-status-summary)
12. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
13. [Troubleshooting](#troubleshooting)

---

## Tổng quan

API Core Instance gồm: **list/create/delete** instance; **load/start/stop/restart/unload**; **batch start/stop/restart**; **config, input, output** (HLS, RTSP, stream); **lines** (core), **jams**, **stops**; **frame, preview, push** (compressed/encoded); **consume_events**; **statistics, state, classes**; **quick**, **status/summary**.

---

## Chuẩn bị

1. API server đang chạy (vd: http://localhost:8080).
2. curl, jq. (Tùy chọn) Solution/instance đã có sẵn để test start/stop.

---

**URL dùng trong tài liệu:** http://localhost:8080. Thay **instance-id-that** bằng instanceId Core thật (UUID từ POST create). Không dùng biến.

---

## Instance CRUD và list

### 1. GET /v1/core/instance – List instances

```bash
curl -s http://localhost:8080/v1/core/instance | jq .
```

**Expected:** 200, JSON: instances (array), total, running, stopped.

### 2. POST /v1/core/instance – Create instance

Body tối thiểu: name, solution (hoặc config tương đương). Có thể có additionalParams.

```bash
curl -s -X POST http://localhost:8080/v1/core/instance -H "Content-Type: application/json" \
  -d '{"name":"test-inst","solution":"your_solution_id"}' | jq .
```

**Expected:** 201, JSON có instanceId (UUID). Nếu dùng solution async build: building, status (building/ready); poll GET /v1/core/instance/{instanceId} đến khi status=ready rồi mới start.

### 3. GET /v1/core/instance/{instanceId} – Chi tiết instance

```bash
curl -s http://localhost:8080/v1/core/instance/instance-id-that | jq .
```

**Expected:** 200, JSON chi tiết instance (config, state, ...).

### 4. DELETE /v1/core/instance (xóa tất cả) – Cẩn thận

```bash
curl -s -X DELETE http://localhost:8080/v1/core/instance | jq .
```

**Expected:** 200, JSON: success, total, deleted, failed, results. Chỉ dùng khi test xong muốn dọn.

---

## Lifecycle: load, start, stop, restart, unload

### 5. POST .../load – Load instance (nếu hỗ trợ)

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/instance-id-that/load" | jq .
```

### 6. POST .../start – Start instance

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/instance-id-that/start" | jq .
```

**Expected:** 200. **Lỗi:** 400 nếu đang building hoặc đã running.

### 7. GET .../state – Trạng thái

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/state" | jq .
```

### 8. POST .../stop – Stop instance

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/instance-id-that/stop" | jq .
```

### 9. POST .../restart – Restart instance

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/instance-id-that/restart" | jq .
```

### 10. POST .../unload – Unload instance

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/instance-id-that/unload" | jq .
```

---

## Batch operations

### 11. POST /v1/core/instance/batch/start

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/batch/start" -H "Content-Type: application/json" \
  -d '{"instanceIds":["id1","id2"]}' | jq .
```

### 12. POST /v1/core/instance/batch/stop

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/batch/stop" -H "Content-Type: application/json" \
  -d '{"instanceIds":["id1","id2"]}' | jq .
```

### 13. POST /v1/core/instance/batch/restart

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/batch/restart" -H "Content-Type: application/json" \
  -d '{"instanceIds":["id1","id2"]}' | jq .
```

---

## Config, input, output

### 14. GET/PUT /v1/core/instance/{instanceId}/config

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/config" | jq .
curl -s -X PUT "http://localhost:8080/v1/core/instance/instance-id-that/config" -H "Content-Type: application/json" -d '{}' | jq .
```

### 15. GET/PUT .../input – Cấu hình input (URL, file, ...)

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/input" | jq .
```

### 16. GET/PUT .../output – Output chung

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/output" | jq .
```

### 17. GET/PUT .../output/hls, .../output/rtsp, .../output/stream

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/output/hls" | jq .
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/output/rtsp" | jq .
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/output/stream" | jq .
```

(Xem thêm EVENTS_OUTPUT_MANUAL_TEST.md.)

---

## Lines, jams, stops

### 18. GET/POST /v1/core/instance/{instanceId}/lines

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/lines" | jq .
```

### 19. GET/PUT/DELETE .../lines/{lineId}

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/lines/line-1" | jq .
```

### 20. POST .../lines/batch – Cập nhật nhiều line

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/instance-id-that/lines/batch" -H "Content-Type: application/json" \
  -d '{"lines":[]}' | jq .
```

### 21. GET/POST .../jams, .../jams/batch, .../jams/{jamId}

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/jams" | jq .
```

### 22. GET/POST .../stops, .../stops/batch, .../stops/{stopId}

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/stops" | jq .
```

---

## Frame, preview, push, consume_events

### 23. GET .../frame – Lấy frame hiện tại

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/frame" -o frame.jpg
```

### 24. GET .../preview – Preview stream/thumbnail

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/preview" -o preview.jpg
```

### 25. POST .../push/compressed, .../push/encoded/{codecId}

(Gửi frame nén/encoded lên instance.)

### 26. POST .../consume_events – Lấy events từ queue

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/instance-id-that/consume_events" -H "Content-Type: application/json" \
  -d '{"maxEvents":10}' | jq .
```

---

## Statistics, state, classes

### 27. GET .../statistics

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/statistics" | jq .
```

### 28. GET .../classes – Danh sách class (detection)

```bash
curl -s "http://localhost:8080/v1/core/instance/instance-id-that/classes" | jq .
```

---

## Quick, status summary

### 29. GET /v1/core/instance/quick – Instance quick (tạo nhanh)

```bash
curl -s "http://localhost:8080/v1/core/instance/quick" | jq .
```

(Xem spec để biết query/body nếu có.)

### 30. GET /v1/core/instance/status/summary – Tóm tắt trạng thái tất cả instance

```bash
curl -s "http://localhost:8080/v1/core/instance/status/summary" | jq .
```

---

## Bảng tóm tắt API

| Method | Path | Mô tả |
|--------|------|--------|
| GET | /v1/core/instance | List instances |
| POST | /v1/core/instance | Create instance |
| DELETE | /v1/core/instance | Delete all instances |
| GET | /v1/core/instance/{instanceId} | Get instance |
| POST | .../load, start, stop, restart, unload | Lifecycle |
| GET | .../state, statistics, classes | State & stats |
| GET/PUT | .../config, input, output | Config & I/O |
| GET/PUT | .../output/hls, output/rtsp, output/stream | Output formats |
| GET/POST/PUT/DELETE | .../lines, .../lines/{lineId}, .../lines/batch | Lines |
| GET/POST | .../jams, .../jams/batch, .../jams/{jamId} | Jams |
| GET/POST | .../stops, .../stops/batch, .../stops/{stopId} | Stops |
| GET | .../frame, .../preview | Frame / preview |
| POST | .../push/compressed, .../push/encoded/{codecId} | Push frame |
| POST | .../consume_events | Consume events |
| POST | /v1/core/instance/batch/start, stop, restart | Batch ops |
| GET | /v1/core/instance/quick | Quick instance |
| GET | /v1/core/instance/status/summary | Status summary |

---

## Troubleshooting

- **Pipeline still building:** Đợi GET .../{instanceId} trả về status=ready rồi mới start.
- **503:** Có thể do max_running_instances hoặc resource limit (xem SERVER_SETTINGS_MONITORING_MANUAL_TEST.md).
- **404:** instanceId sai hoặc instance đã bị xóa.

---

## Tài liệu liên quan

- EVENTS_OUTPUT_MANUAL_TEST.md – consume_events, HLS, RTSP.
- INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md – Cập nhật instance hot reload.
- LOG_CONFIG_MANUAL_TEST.md – Log theo instance.
