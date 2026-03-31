# Hướng dẫn test thủ công – Core AI API (process, batch, metrics, status)

> Tài liệu test **Core AI API**: gửi ảnh đơn/batch để xử lý, và lấy metrics/status của pipeline AI. Làm theo **đúng thứ tự flow**; mỗi bước có chuẩn bị, lệnh và cách kiểm tra kết quả cụ thể.

---

## Mục lục

1. [Mục đích](#mục-đích)
2. [Chuẩn bị trước khi test](#chuẩn-bị-trước-khi-test)
3. [Flow test chính (theo thứ tự)](#flow-test-chính-theo-thứ-tự)
5. [Kịch bản bổ sung (batch, priority)](#kịch-bản-bổ-sung)
6. [Kịch bản lỗi (negative tests)](#kịch-bản-lỗi-negative-tests)
7. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
8. [Troubleshooting](#troubleshooting)
9. [Tài liệu liên quan](#tài-liệu-liên-quan)

---

## Mục đích

- **Process (đơn ảnh):** Gửi một ảnh (base64) qua `POST /v1/core/ai/process` để đưa vào hàng đợi xử lý AI; dùng khi cần gửi frame đơn (vd: từ camera snapshot).
- **Batch:** Gửi nhiều ảnh một lần qua `POST /v1/core/ai/batch` (có thể trả **501 Not Implemented** tùy môi trường).
- **Metrics:** Xem thống kê cache và rate limiter qua `GET /v1/core/ai/metrics`.
- **Status:** Xem trạng thái queue (số item trong hàng, giới hạn) và tài nguyên (GPU nếu có) qua `GET /v1/core/ai/status`.

**Lưu ý:** Nếu môi trường của bạn **chưa bật** AI process/batch (chỉ dùng instance-based pipeline), process/batch có thể trả **501**. Khi đó vẫn test được metrics và status; phần process/batch ghi nhận 501 là đúng hành vi.

---

## Chuẩn bị trước khi test

### Bảng chuẩn bị

| Hạng mục | Yêu cầu cụ thể | Cách kiểm tra / chuẩn bị |
|----------|----------------|---------------------------|
| **1. API server** | OmniAPI đang chạy (vd: `http://localhost:8080`). | `curl -s http://localhost:8080/v1/core/health` → 200, JSON có `status`. |
| **2. Công cụ** | **curl**, **jq** (để đọc JSON). | `curl --version`, `jq --version`. Windows: PowerShell hoặc Postman. |
| **3. Ảnh base64** | Chuỗi base64 của **một ảnh nhỏ** (PNG/JPEG) để gửi process/batch. | Dùng sẵn trong script (1x1 pixel) hoặc tạo từ file (xem bên dưới). |

### Tạo chuỗi base64 từ file ảnh (tùy chọn)

Nếu muốn test với ảnh thật (vd: file PNG/JPEG trên máy):

```bash
# Linux/macOS: encode file ảnh thành base64 (một dòng)
# Linux: base64 -w 0 /path/to/your/image.png
# macOS: base64 -i /path/to/image.png | tr -d '\n'
```

Trong lệnh bên dưới đã gắn sẵn chuỗi base64 ảnh 1x1 pixel PNG; bạn có thể thay bằng chuỗi base64 từ file ảnh của mình.

### Cần chuẩn bị sẵn theo từng phase

| Phase | Cần có trước khi bắt đầu |
|-------|---------------------------|
| **Bước 0** | Chỉ cần server chạy. |
| **Bước 1 – Status** | Không cần ảnh. |
| **Bước 2 – Metrics** | Không cần ảnh. |
| **Bước 3 – Process** | Chuỗi base64 đã có sẵn trong lệnh (1x1 pixel PNG). |
| **Bước 4 – Status lại** | Đã gọi process ít nhất một lần (để so sánh queue_size nếu implementation cập nhật ngay). |
| **Bước 5 – Batch** | Cùng chuỗi base64 trong lệnh; chấp nhận 501. |

---

## Biến dùng chung

**URL trong tài liệu:** **http://localhost:8080**. Không dùng biến. Ảnh base64 mẫu (1x1 pixel PNG) dùng trong lệnh process/batch: `iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==` (đã gắn sẵn trong lệnh bên dưới). Nếu server khác host/port, thay http://localhost:8080 trong từng lệnh.

---

## Flow test chính (theo thứ tự)

### Bước 0. Kiểm tra server và endpoint AI

**Mục đích:** Đảm bảo API sống và endpoint AI phản hồi (không 404).

```bash
echo "=== Bước 0: Health + AI status ==="
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/v1/core/health
echo ""
curl -s http://localhost:8080/v1/core/ai/status | jq .
```

**Cần kiểm tra:**

- Health: HTTP **200**.
- `GET /v1/core/ai/status`: HTTP **200**; body JSON (có thể có `queue_size`, `queue_max`, hoặc trường tương đương). Nếu 404 → kiểm tra base path `/v1/core/ai`.

---

### Bước 1. GET /v1/core/ai/status (baseline)

**Mục đích:** Ghi nhận trạng thái queue và tài nguyên trước khi gửi job.

```bash
echo "=== Bước 1: GET /v1/core/ai/status (baseline) ==="
curl -s http://localhost:8080/v1/core/ai/status | jq .
```

**Cần kiểm tra:**

- HTTP **200**.
- Response có ít nhất một trong: `queue_size`, `queue_max`, hoặc object mô tả queue/GPU/tài nguyên. Ghi lại `queue_size` (vd: `q0`) để so sánh sau Bước 3 (nếu backend cập nhật ngay thì queue_size có thể tăng rồi giảm).

---

### Bước 2. GET /v1/core/ai/metrics (baseline)

**Mục đích:** Ghi nhận metrics cache và rate limiter.

```bash
echo "=== Bước 2: GET /v1/core/ai/metrics ==="
curl -s http://localhost:8080/v1/core/ai/metrics | jq .
```

**Cần kiểm tra:**

- HTTP **200**.
- JSON có thể chứa: `cache` (vd: `size`, `max_size`, `hit_rate`), `rate_limiter` (vd: `total_keys`, `active_keys`). Cấu trúc có thể khác tùy implementation; miễn là 200 và body hợp lệ.

---

### Bước 3. POST /v1/core/ai/process – Gửi một ảnh

**Mục đích:** Gửi job xử lý một ảnh; kiểm tra 200 (hoặc 501 nếu chưa implement).

```bash
echo "=== Bước 3: POST /v1/core/ai/process ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/ai/process \
  -H "Content-Type: application/json" \
  -d '{"image":"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==","priority":"medium"}' | tee /tmp/ai_process_resp.txt
```

Đọc phần JSON (bỏ dòng HTTP_CODE):

```bash
grep -v HTTP_CODE /tmp/ai_process_resp.txt | jq .
```

**Cần kiểm tra:**

- **Nếu 200:** Body có ít nhất `job_id` (string) và/hoặc `status` (vd: `queued`). Ghi nhận để sau này đối chiếu với metrics/status nếu có API lấy kết quả theo job_id.
- **Nếu 501:** Implementation chưa bật process; ghi nhận "501 Not Implemented" và chuyển Bước 4/5 vẫn chạy (metrics/status vẫn hợp lệ).
- **400:** Thiếu `image` hoặc base64 không hợp lệ → kiểm tra body có field "image" (chuỗi base64) và Content-Type application/json.
- **429:** Rate limit → giảm tần suất gọi hoặc đợi vài giây.

---

### Bước 4. GET /v1/core/ai/status (sau process)

**Mục đích:** So sánh queue sau khi gửi process (nếu backend cập nhật ngay).

```bash
echo "=== Bước 4: GET /v1/core/ai/status (sau process) ==="
curl -s http://localhost:8080/v1/core/ai/status | jq .
```

**Cần kiểm tra:**

- HTTP **200**. So sánh `queue_size` với Bước 1 (có thể tăng 1 rồi giảm nếu xử lý nhanh; hoặc không đổi nếu 501).

---

### Bước 5. POST /v1/core/ai/batch – Gửi batch ảnh

**Mục đích:** Kiểm tra batch (200 hoặc 501).

```bash
echo "=== Bước 5: POST /v1/core/ai/batch ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/ai/batch \
  -H "Content-Type: application/json" \
  -d '{"images":["iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==","iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="]}' | tee /tmp/ai_batch_resp.txt
grep -v HTTP_CODE /tmp/ai_batch_resp.txt | jq .
```

**Cần kiểm tra:**

- **200:** Body có thể có `job_ids` (array). Ghi nhận số phần tử = số ảnh gửi (2).
- **501:** Not Implemented; ghi nhận và kết thúc flow process/batch.

---

### Bước 6. GET /v1/core/ai/metrics (sau khi gửi job)

**Mục đích:** Xem metrics sau khi đã gửi process (và batch nếu 200).

```bash
echo "=== Bước 6: GET /v1/core/ai/metrics (sau jobs) ==="
curl -s http://localhost:8080/v1/core/ai/metrics | jq .
```

**Cần kiểm tra:**

- HTTP **200**. So sánh với Bước 2: `cache.size` hoặc `rate_limiter.active_keys` có thể thay đổi tùy implementation.

---

## Kịch bản bổ sung

### Gửi process với priority (low / medium / high)

```bash
for p in low medium high; do
  echo "=== Priority: $p ==="
  curl -s -X POST http://localhost:8080/v1/core/ai/process -H "Content-Type: application/json" \
    -d "{\"image\":\"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\",\"priority\":\"$p\"}" | jq .
done
```

**Expected:** Mỗi lần 200 (hoặc 501); body có `job_id`/`status` nếu 200.

### Gửi process kèm config (nếu API hỗ trợ)

```bash
curl -s -X POST http://localhost:8080/v1/core/ai/process -H "Content-Type: application/json" \
  -d '{"image":"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==","config":"{\"model\":\"yolo\",\"threshold\":0.5}"}' | jq .
```

**Expected:** 200 hoặc 501; 400 nếu format `config` không đúng.

---

## Kịch bản lỗi (negative tests)

### Thiếu field `image` (400)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/ai/process \
  -H "Content-Type: application/json" \
  -d '{"priority":"medium"}'
```

**Expected:** HTTP **400**; body có thông báo lỗi (vd: missing required field `image`).

### Base64 không hợp lệ (400)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/ai/process \
  -H "Content-Type: application/json" \
  -d '{"image":"not-valid-base64!!!","priority":"medium"}'
```

**Expected:** HTTP **400** (hoặc 422 tùy implementation).

### Gửi batch rỗng (400 hoặc 200 tùy implementation)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/ai/batch \
  -H "Content-Type: application/json" \
  -d '{"images":[]}'
```

**Expected:** 400 (bad request) hoặc 200 với `job_ids: []`; ghi nhận hành vi thực tế.

---

## Bảng tóm tắt API

| Method | Path | Body | Mô tả |
|--------|------|------|--------|
| POST | /v1/core/ai/process | `image` (base64), `priority?`, `config?` | Gửi một ảnh xử lý. |
| POST | /v1/core/ai/batch | `images[]`, `config?` | Gửi batch ảnh (có thể 501). |
| GET | /v1/core/ai/metrics | - | Cache & rate limiter metrics. |
| GET | /v1/core/ai/status | - | Queue size/max, GPU/tài nguyên. |

---

## Troubleshooting

| Triệu chứng | Nguyên nhân có thể | Cách xử lý |
|-------------|--------------------|------------|
| 501 process/batch | Backend chưa bật AI process/batch. | Ghi nhận 501; test metrics/status. Dùng instance-based pipeline cho xử lý thật. |
| 400 process | Thiếu `image` hoặc base64 sai. | Đảm bảo body có `"image": "<base64>"` và Content-Type `application/json`. |
| 429 | Vượt rate limit. | Giảm tần suất gọi; đợi vài giây rồi thử lại. |
| 404 /v1/core/ai/* | Sai base path hoặc server cũ. | Kiểm tra OpenAPI spec và phiên bản server. |

---

## Tài liệu liên quan

- OpenAPI: paths `/v1/core/ai/process`, `/v1/core/ai/batch`, `/v1/core/ai/metrics`, `/v1/core/ai/status`.
- INSTANCE_API_MANUAL_TEST.md: Xử lý video/stream qua instance (pipeline theo instance, không qua AI process/batch).
