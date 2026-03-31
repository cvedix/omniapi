# Hướng dẫn test thủ công – Font API

> Tài liệu test **Font API**: list, upload, rename, delete font. Làm theo **đúng thứ tự flow**; mỗi bước có chuẩn bị, lệnh curl và cách kiểm tra kết quả cụ thể.

---

## Mục lục

1. [Mục đích](#mục-đích)
2. [Chuẩn bị trước khi test](#chuẩn-bị-trước-khi-test)
3. [Flow test chính (bắt buộc theo thứ tự)](#flow-test-chính-bắt-buộc-theo-thứ-tự)
5. [Kịch bản bổ sung (list theo thư mục, upload subdirectory)](#kịch-bản-bổ-sung)
6. [Kịch bản lỗi (negative tests)](#kịch-bản-lỗi-negative-tests)
7. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
8. [Troubleshooting](#troubleshooting)
9. [Tài liệu liên quan](#tài-liệu-liên-quan)

---

## Mục đích

- **List:** Xem danh sách font đã upload (toàn cây đệ quy hoặc theo thư mục với `?directory=`).
- **Upload:** Đưa file font (TTF, OTF, WOFF, WOFF2, …) lên server; có thể chỉ định thư mục con.
- **Rename:** Đổi tên file font trên server (dùng cho hiển thị/LPR/overlay).
- **Delete:** Xóa file font trên server.

Sau flow chính bạn sẽ: list → upload → kiểm tra list có file mới → rename → kiểm tra tên mới → delete → kiểm tra file đã mất.

---

## Chuẩn bị trước khi test

### Bảng chuẩn bị

| Hạng mục | Yêu cầu cụ thể | Cách kiểm tra / chuẩn bị |
|----------|-----------------|---------------------------|
| **1. API server** | OmniAPI đang chạy (vd: `http://localhost:8080`). | `curl -s http://localhost:8080/v1/core/health` → 200, JSON có `status`. |
| **2. Công cụ** | **curl**, **jq**. | `curl --version`, `jq --version`. Windows: PowerShell hoặc Postman. |
| **3. File font test** | Ít nhất **một file font** (.ttf, .otf, .woff, .woff2) trên máy test. | Dùng font hệ thống hoặc tải font miễn phí (vd: Google Fonts); xem gợi ý bên dưới. |

**Lưu ý:** Nếu test từ máy khác host chạy API, thay `localhost` bằng IP/hostname của server.

### Có sẵn file font ở đâu (gợi ý)

- **Linux:** `/usr/share/fonts/truetype/` hoặc `~/.local/share/fonts/` (vd: copy một file .ttf ra thư mục test).
- **macOS:** `/Library/Fonts/` hoặc `~/Library/Fonts/`.
- **Windows:** `C:\Windows\Fonts\` (copy một file .ttf ra folder khác để dùng đường dẫn đơn giản).

Ví dụ copy một font ra thư mục hiện tại:

```bash
# Linux: copy font mẫu (điều chỉnh đường dẫn nếu khác)
cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf ./test_font.ttf
ls -la ./test_font.ttf
```

### Cần chuẩn bị sẵn theo từng phase

| Phase | Cần có trước khi bắt đầu |
|-------|---------------------------|
| **Bước 0** | Chỉ cần server chạy. |
| **Bước 1 – List** | Không cần file font. |
| **Bước 2 – Upload** | File font tồn tại tại `FONT_FILE`; đường dẫn đúng. |
| **Bước 3 – List lại** | Đã upload thành công (có tên file trong response). |
| **Bước 4 – Rename** | Tên file gốc chính xác (trùng với đã upload). |
| **Bước 5 – List lại** | Đã rename thành công; nhớ tên mới. |
| **Bước 6 – Delete** | Tên file cần xóa đúng (tên sau rename). |
| **Bước 7 – List cuối** | Đã delete; dùng để xác nhận file không còn. |

---

**URL và tên file trong tài liệu:** Mọi lệnh dùng **http://localhost:8080** và **http://localhost:8080/v1/core/font**. Không dùng biến. Ví dụ tên file: **test_font.ttf** (sau rename: **test_font_renamed.ttf**). Bạn **sửa** đường dẫn file upload (vd: /path/to/your/font.ttf). Nếu server khác host/port, thay http://localhost:8080 trong từng lệnh.

---

## Flow test chính (bắt buộc theo thứ tự)

### Bước 0. Kiểm tra server và endpoint font

**Mục đích:** Đảm bảo API sống và endpoint font phản hồi.

```bash
echo "=== Bước 0: Health + GET /v1/core/font/list ==="
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/v1/core/health
echo ""
curl -s http://localhost:8080/v1/core/font/list | jq .
```

**Cần kiểm tra:**

- Health: HTTP **200**.
- GET font list: HTTP **200**; body có ít nhất `success`, `fonts` (array), `count`. Có thể `fonts` rỗng.

---

### Bước 1. GET /v1/core/font/list (baseline)

**Mục đích:** Ghi nhận danh sách font trước khi upload.

```bash
echo "=== Bước 1: GET /v1/core/font/list (baseline) ==="
curl -s http://localhost:8080/v1/core/font/list | jq .
```

**Cần kiểm tra:**

- HTTP **200**; `success`: true; `fonts` (array); `count` (số). Ghi lại `count` (vd: `count_before`).

---

### Bước 2. POST /v1/core/font/upload – Upload font

**Mục đích:** Upload file font lên server.

**Chuẩn bị:** File font tồn tại. Sửa /path/to/your/font.ttf thành đường dẫn thật.

```bash
echo "=== Bước 2: POST /v1/core/font/upload ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/font/upload -F "file=@/path/to/your/font.ttf" | tee /tmp/font_upload_resp.json
grep -v HTTP_CODE /tmp/font_upload_resp.json | jq .
```

**Cần kiểm tra:**

- HTTP **201**.
- Body có `success`: true; `count` >= 1; `files` (array) có ít nhất 1 phần tử với `filename` (trùng hoặc chứa `FONT_NAME`), có `path` hoặc `url`.
- Ghi lại `filename` trong `files[0]` để dùng rename/delete (thường = `FONT_NAME`).

**Nếu 409:** File cùng tên đã tồn tại → đổi tên file local hoặc xóa font cũ trên server rồi upload lại.

---

### Bước 3. GET /v1/core/font/list – Xác nhận có file vừa upload

**Mục đích:** Xác nhận font vừa upload xuất hiện trong list.

```bash
echo "=== Bước 3: GET /v1/core/font/list (sau upload) ==="
curl -s "${FONT}/list" | jq '.fonts[] | select(.filename != null) | .filename'
```

**Cần kiểm tra:**

- Có dòng in ra = tên file bạn upload (vd: `test_font.ttf`). `count` >= count_before + 1.

---

### Bước 4. PUT /v1/core/font/{fontName} – Rename

**Mục đích:** Đổi tên font trên server.

```bash
echo "=== Bước 4: PUT /v1/core/font/test_font.ttf (rename) ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X PUT http://localhost:8080/v1/core/font/test_font.ttf -H "Content-Type: application/json" \
  -d '{"newName":"test_font_renamed.ttf"}' | tee /tmp/font_rename_resp.json
grep -v HTTP_CODE /tmp/font_rename_resp.json | jq .
```

**Cần kiểm tra:**

- HTTP **200**.
- Body có `success`: true; `oldName` = tên cũ; `newName` = tên mới; có thể có `message`, `path`.

**Nếu 404:** Tên file cũ sai (kiểm tra Bước 2/3). **Nếu 409:** Tên mới đã tồn tại → chọn tên khác.

---

### Bước 5. GET /v1/core/font/list – Xác nhận tên mới

**Mục đích:** Xác nhận list chỉ còn tên mới, không còn tên cũ.

```bash
echo "=== Bước 5: GET /v1/core/font/list (sau rename) ==="
curl -s "${FONT}/list" | jq '.fonts[]? | .filename'
```

**Cần kiểm tra:**

- Có `FONT_NAME_RENAMED`; không còn `FONT_NAME`.

---

### Bước 6. DELETE /v1/core/font/{fontName} – Xóa font

**Mục đích:** Xóa font (dùng tên sau rename).

```bash
echo "=== Bước 6: DELETE /v1/core/font/test_font_renamed.ttf ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X DELETE http://localhost:8080/v1/core/font/test_font_renamed.ttf | tee /tmp/font_delete_resp.txt
```

**Cần kiểm tra:**

- HTTP **200**; body (nếu có) `success`: true; có thể có `message`, `filename`.

**Nếu 404:** File đã bị xóa hoặc tên sai; kiểm tra Bước 5.

---

### Bước 7. GET /v1/core/font/list – Xác nhận file đã mất

**Mục đích:** Xác nhận sau khi xóa, font không còn trong list.

```bash
echo "=== Bước 7: GET /v1/core/font/list (sau delete) ==="
curl -s "${FONT}/list" | jq '.fonts[]? | .filename'
```

**Cần kiểm tra:**

- Không còn `FONT_NAME_RENAMED`; `count` giảm 1 so với Bước 5.

---

## Kịch bản bổ sung

### List theo thư mục (không đệ quy)

Dùng khi đã upload vào subdirectory.

```bash
curl -s "http://localhost:8080/v1/core/font/list?directory=projects/myproject/fonts" | jq .
```

**Expected:** 200; `fonts` chỉ chứa file trong thư mục đó.

### Upload vào thư mục con

```bash
curl -s -X POST "http://localhost:8080/v1/core/font/upload?directory=projects/test/fonts" -F "file=@/path/to/your/font.ttf" | jq .
```

**Expected:** 201; `files[0].path` hoặc response phản ánh đường dẫn chứa `projects/test/fonts`. Sau đó dùng GET list với `?directory=projects/test/fonts` để kiểm tra.

### Rename/Delete font trong thư mục con

Nếu file nằm trong subdirectory, thêm query `directory`:

```bash
curl -s -X PUT "http://localhost:8080/v1/core/font/test_font.ttf?directory=projects/test/fonts" -H "Content-Type: application/json" \
  -d '{"newName":"test_font_renamed.ttf"}' | jq .

curl -s -X DELETE "http://localhost:8080/v1/core/font/test_font_renamed.ttf?directory=projects/test/fonts" | jq .
```

---

## Kịch bản lỗi (negative tests)

### Upload trùng tên (409)

```bash
curl -s -X POST http://localhost:8080/v1/core/font/upload -F "file=@/path/to/your/font.ttf" | jq .
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/font/upload -F "file=@/path/to/your/font.ttf"
```

**Expected lần 2:** HTTP **409** (file đã tồn tại).

### Rename sang tên đã tồn tại (409)

Nếu đã có font `existing.ttf`, rename font khác thành `existing.ttf`:

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X PUT http://localhost:8080/v1/core/font/test_font.ttf -H "Content-Type: application/json" -d '{"newName":"existing.ttf"}'
```

**Expected:** HTTP **409** (hoặc 400).

### Delete font không tồn tại (404)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X DELETE http://localhost:8080/v1/core/font/font_khong_ton_tai_12345.ttf
```

**Expected:** HTTP **404**.

### Upload thiếu file (400)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/font/upload -F "other=value"
```

**Expected:** HTTP **400** (missing file).

---

## Bảng tóm tắt API

| Method | Path | Body / Query | Mô tả |
|--------|------|--------------|--------|
| GET | /v1/core/font/list | `?directory=` (tùy chọn) | List font (đệ quy hoặc theo thư mục). |
| POST | /v1/core/font/upload | multipart `file=@...`; `?directory=` (tùy chọn) | Upload font. |
| PUT | /v1/core/font/{fontName} | `{"newName": "..."}`; `?directory=` (tùy chọn) | Đổi tên font. |
| DELETE | /v1/core/font/{fontName} | `?directory=` (tùy chọn) | Xóa font. |

---

## Troubleshooting

| Triệu chứng | Nguyên nhân có thể | Cách xử lý |
|-------------|--------------------|------------|
| Connection refused | Server chưa chạy hoặc sai host/port. | Start server; kiểm tra biến `SERVER`. |
| Upload 400 | Thiếu field `file` hoặc Content-Type sai. | Dùng đúng `-F "file=@/đường/dẫn/font.ttf"`. |
| Upload 409 | File cùng tên đã tồn tại. | Đổi tên file local hoặc delete font cũ rồi upload lại. |
| Rename/Delete 404 | fontName sai hoặc file trong subdirectory. | Lấy đúng `filename` từ list; thêm `?directory=...` nếu cần. |
| Rename 409 | Tên mới trùng file khác. | Chọn `newName` chưa tồn tại. |

---

## Tài liệu liên quan

- OpenAPI: `api-specs/openapi/en/paths/fonts/` (core_font_list, core_font_upload, core_font_fontname).
- VIDEO_MANUAL_TEST.md, MODEL_MANUAL_TEST.md: Cùng pattern list/upload/rename/delete với query `directory`.
