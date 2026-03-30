# Hướng dẫn test thủ công – Video API

> Tài liệu test **Video API**: list, upload, rename, delete video. Làm theo **đúng thứ tự flow** bên dưới; mỗi bước có chuẩn bị, lệnh curl và cách kiểm tra kết quả cụ thể.

---

## Mục lục

1. [Mục đích](#mục-đích)
2. [Chuẩn bị trước khi test](#chuẩn-bị-trước-khi-test)
3. [Flow test chính (bắt buộc theo thứ tự)](#flow-test-chính-bắt-buộc-theo-thứ-tự)
5. [Kịch bản bổ sung (list theo thư mục, upload vào subdirectory)](#kịch-bản-bổ-sung)
6. [Kịch bản lỗi (negative tests)](#kịch-bản-lỗi-negative-tests)
7. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
8. [Troubleshooting](#troubleshooting)
9. [Tài liệu liên quan](#tài-liệu-liên-quan)

---

## Mục đích

- **List**: Xem danh sách video đã upload (toàn bộ hoặc theo thư mục).
- **Upload**: Đưa file video (MP4, AVI, MKV, …) lên server; có thể chỉ định thư mục con (`directory`).
- **Rename**: Đổi tên file video trên server (dùng cho instance input hoặc tham chiếu).
- **Delete**: Xóa file video trên server.

Sau khi test xong flow chính, bạn sẽ đã: list → upload → kiểm tra list có file mới → rename → kiểm tra tên mới → delete → kiểm tra file đã mất.

---

## Chuẩn bị trước khi test

### Bảng chuẩn bị

| Hạng mục | Yêu cầu cụ thể | Cách kiểm tra / chuẩn bị |
|----------|-----------------|---------------------------|
| **1. API server** | Edge AI API đang chạy, có base URL (vd: `http://localhost:8080`). | Mở trình duyệt hoặc `curl -s http://localhost:8080/v1/core/health` → trả 200 và JSON `status`. |
| **2. Công cụ** | **curl** (bắt buộc). **jq** (nên có, để đọc JSON). | Chạy `curl --version` và `jq --version`. Windows: dùng PowerShell hoặc Postman thay curl. |
| **3. File video test** | Ít nhất **một file video** (.mp4, .avi, .mkv, …) trên máy test. | Dùng file có sẵn hoặc tạo file nhỏ (xem bên dưới). |
| **4. Quyền ghi** | Server có quyền ghi thư mục lưu video (thường do config `VIDEOS_DIR` hoặc mặc định). | Upload thành công (201) ở Bước 2 là đủ chứng minh. |

**Lưu ý:** Nếu test từ máy khác (không phải host chạy API), thay `localhost` bằng IP/hostname của server (vd: `http://192.168.1.10:8080`).

### Tạo file video nhỏ (nếu chưa có)

Trên máy có **ffmpeg**:

```bash
# Tạo file test_video_10s.mp4 (10 giây, 320x240, ~50KB)
ffmpeg -f lavfi -i "color=c=blue:s=320x240:d=10" -c:v libx264 -pix_fmt yuv420p test_video_10s.mp4
```

Đặt file vào thư mục thuận tiện (vd: `~/Downloads/test_video_10s.mp4`) và dùng đường dẫn này cho biến `VIDEO_FILE` ở phần [Biến dùng chung](#biến-dùng-chung).

### Cần chuẩn bị sẵn theo từng phase

| Phase | Cần có trước khi bắt đầu |
|-------|---------------------------|
| **Bước 0 – Kiểm tra server** | Chỉ cần server chạy. |
| **Bước 1 – List** | Không cần file video. |
| **Bước 2 – Upload** | File video tồn tại trên máy, đường dẫn đúng trong `VIDEO_FILE`. |
| **Bước 3 – List lại** | Đã upload thành công ở Bước 2 (có tên file trong response). |
| **Bước 4 – Rename** | Tên file gốc chính xác (vd: `test_video_10s.mp4` như đã upload). |
| **Bước 5 – List lại** | Đã rename thành công; nhớ tên mới (vd: `test_video_renamed.mp4`). |
| **Bước 6 – Delete** | Tên file cần xóa đúng (tên sau rename hoặc tên gốc nếu không rename). |
| **Bước 7 – List cuối** | Đã delete thành công; dùng để xác nhận file không còn trong list. |

---

**URL và tên file trong tài liệu:** Mọi lệnh dùng **http://localhost:8080** và **http://localhost:8080/v1/core/video**. Không dùng biến. Ví dụ tên file: **test_video_10s.mp4** (sau rename: **test_video_10s_renamed.mp4**). Bạn **bắt buộc sửa** đường dẫn file upload cho đúng máy (vd: /path/to/your/video.mp4). Nếu server chạy host/port khác, thay http://localhost:8080 trong từng lệnh.

---

## Flow test chính (bắt buộc theo thứ tự)

### Bước 0. Kiểm tra server và endpoint video

**Mục đích:** Đảm bảo API sống và endpoint video phản hồi.

```bash
echo "=== Bước 0: Health + List video ==="
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/v1/core/health
echo ""
curl -s http://localhost:8080/v1/core/video/list | jq .
```

**Cần kiểm tra:**

- Gọi health: HTTP status **200** (số in ra là `200`).
- Gọi `GET /v1/core/video/list`: HTTP status **200**; body JSON có ít nhất một trong các trường: `success`, `videos` (array), `count`. Nếu chưa có video nào, `videos` có thể là `[]`, `count` = 0.

**Nếu lỗi:** Connection refused → kiểm tra server đã start và đúng host/port. 404 → kiểm tra base path (`/v1/core/video`).

---

### Bước 1. List video (baseline)

**Mục đích:** Ghi nhận trạng thái danh sách video trước khi upload; dùng để so sánh sau Bước 2 và Bước 7.

```bash
echo "=== Bước 1: GET /v1/core/video/list (baseline) ==="
curl -s http://localhost:8080/v1/core/video/list | jq .
```

**Cần kiểm tra:**

- HTTP status **200**.
- Response có dạng (hoặc tương đương):
  - `success`: true
  - `videos`: mảng các object (mỗi object có ít nhất `filename`, `path`; có thể có `size`, `modified`)
  - `count`: số nguyên (số phần tử trong `videos`)
- Ghi lại **số lượng** `count` (vd: `count_before`) để sau Bước 2 so sánh: `count` tăng ít nhất 1.

---

### Bước 2. Upload video

**Mục đích:** Upload file video lên server; dùng cho các bước sau (list lại, rename, delete).

**Chuẩn bị:** File video phải tồn tại. Sửa /path/to/your/video.mp4 thành đường dẫn thật.

```bash
echo "=== Bước 2: POST /v1/core/video/upload ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/video/upload -F "file=@/path/to/your/video.mp4" | tee /tmp/upload_resp.json
```

Đọc kết quả (phần JSON):

```bash
jq . /tmp/upload_resp.json
```

**Cần kiểm tra:**

- HTTP status **201** (xem dòng `HTTP_CODE:201`).
- JSON có:
  - `success`: true
  - `count`: >= 1
  - `files`: mảng có ít nhất 1 phần tử; phần tử có `filename` trùng hoặc chứa tên file bạn upload (vd: `test_video_10s.mp4`), có `path` hoặc `url`.
- Ghi lại **`filename`** trong `files[0]` (đây là tên dùng cho rename/delete nếu server trả về đúng tên file). Trong flow này ta dùng `VIDEO_NAME` đã set ở [Biến dùng chung](#biến-dùng-chung).

**Nếu 409 (Conflict):** File cùng tên đã tồn tại. Đổi tên file local rồi upload lại, hoặc chạy Bước 6 (delete) với tên file đó rồi upload lại.

---

### Bước 3. List lại – xác nhận có file vừa upload

**Mục đích:** Xác nhận file vừa upload xuất hiện trong danh sách.

```bash
echo "=== Bước 3: GET /v1/core/video/list (sau upload) ==="
curl -s http://localhost:8080/v1/core/video/list | jq .
```

**Cần kiểm tra:**

- HTTP status **200**.
- Trong `videos` có **ít nhất một phần tử** có `filename` = tên file bạn vừa upload (vd: `test_video_10s.mp4`).
- `count` >= `count_before` + 1 (so với Bước 1).

---

### Bước 4. Rename video

**Mục đích:** Đổi tên file video trên server; dùng tên đã upload ở Bước 2.

```bash
echo "=== Bước 4: PUT /v1/core/video/{videoName} (rename) ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X PUT http://localhost:8080/v1/core/video/test_video_10s.mp4 -H "Content-Type: application/json" \
  -d '{"newName":"test_video_10s_renamed.mp4"}' | tee /tmp/rename_resp.json
jq . /tmp/rename_resp.json
```

**Cần kiểm tra:**

- HTTP status **200**.
- JSON có (hoặc tương đương): `success`: true; `oldName` = tên cũ (`VIDEO_NAME`); `newName` = tên mới (`VIDEO_NAME_RENAMED`); có thể có `path`, `message`.

**Nếu 404:** Tên file cũ sai (hoặc đã bị xóa). Kiểm tra Bước 2/3 và dùng đúng `filename` trả về từ server.  
**Nếu 409:** Tên mới đã tồn tại; chọn tên khác cho `VIDEO_NAME_RENAMED`.

---

### Bước 5. List lại – xác nhận tên mới

**Mục đích:** Xác nhận sau rename chỉ còn tên mới, không còn tên cũ.

```bash
echo "=== Bước 5: GET /v1/core/video/list (sau rename) ==="
curl -s http://localhost:8080/v1/core/video/list | jq '.videos[] | select(.filename != null) | .filename'
```

**Cần kiểm tra:**

- Trong danh sách **có** `filename` = `VIDEO_NAME_RENAMED` (vd: `test_video_10s_renamed.mp4`).
- **Không còn** `filename` = `VIDEO_NAME` (tên cũ).

---

### Bước 6. Delete video

**Mục đích:** Xóa file video trên server (dùng tên sau rename).

```bash
echo "=== Bước 6: DELETE /v1/core/video/{videoName} ==="
curl -s -w "\nHTTP_CODE:%{http_code}" -X DELETE http://localhost:8080/v1/core/video/test_video_10s_renamed.mp4 | tee /tmp/delete_resp.txt
```

**Cần kiểm tra:**

- HTTP status **200** (dòng in ra `HTTP_CODE:200`).
- Body JSON (nếu có): `success`: true; có thể có `message`, `filename` = tên file vừa xóa.

**Nếu 404:** File đã bị xóa trước đó hoặc tên sai; kiểm tra Bước 5 và dùng đúng `VIDEO_NAME_RENAMED`.

---

### Bước 7. List cuối – xác nhận file đã mất

**Mục đích:** Xác nhận sau khi xóa, file không còn trong list.

```bash
echo "=== Bước 7: GET /v1/core/video/list (sau delete) ==="
curl -s http://localhost:8080/v1/core/video/list | jq '.videos[] | select(.filename != null) | .filename'
```

**Cần kiểm tra:**

- Trong `videos` **không còn** phần tử nào có `filename` = `VIDEO_NAME_RENAMED`.
- `count` giảm ít nhất 1 so với Bước 5 (hoặc bằng `count_before` nếu chỉ test một file).

---

## Kịch bản bổ sung

### List theo thư mục (không đệ quy)

Dùng khi đã upload vào subdirectory (vd: Bước 2 dùng `?directory=...`).

```bash
# Thay myproject/videos bằng thư mục bạn đã dùng khi upload
curl -s "http://localhost:8080/v1/core/video/list?directory=projects/myproject/videos" | jq .
```

**Expected:** 200; `videos` chỉ chứa file trong thư mục đó; `directory` có thể trả về đúng path.

### Upload vào thư mục con

Thư mục không tồn tại sẽ được tạo (theo spec).

```bash
curl -s -X POST "http://localhost:8080/v1/core/video/upload?directory=users/test/videos" -F "file=@/path/to/your/video.mp4" | jq .
```

**Expected:** 201; trong `files[0].path` hoặc response có phản ánh đường dẫn chứa `users/test/videos`. Sau đó dùng `GET .../list?directory=users/test/videos` để kiểm tra.

---

## Kịch bản lỗi (negative tests)

### Upload trùng tên (409)

Upload cùng file hai lần (không đổi tên):

```bash
curl -s -X POST http://localhost:8080/v1/core/video/upload -F "file=@/path/to/your/video.mp4" | jq .
# Lần 2 ngay sau đó
curl -s -w "\nHTTP_CODE:%{http_code}" -X POST http://localhost:8080/v1/core/video/upload -F "file=@/path/to/your/video.mp4"
```

**Expected lần 2:** HTTP **409**; body thường có thông báo file đã tồn tại.

### Rename sang tên đã tồn tại (409)

Nếu trên server đã có file `existing.mp4`, rename một file khác thành `existing.mp4`:

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X PUT http://localhost:8080/v1/core/video/test_video_10s.mp4 -H "Content-Type: application/json" -d '{"newName":"existing.mp4"}'
```

**Expected:** HTTP **409** (hoặc 400 tùy implementation).

### Delete file không tồn tại (404)

```bash
curl -s -w "\nHTTP_CODE:%{http_code}" -X DELETE http://localhost:8080/v1/core/video/file_khong_ton_tai_12345.mp4
```

**Expected:** HTTP **404**; body có thể là JSON lỗi với message kiểu "not found".

---

## Bảng tóm tắt API

| Method | Path | Body / Query | Mô tả |
|--------|------|--------------|--------|
| GET | /v1/core/video/list | `?directory=` (tùy chọn) | List video (đệ quy toàn bộ hoặc theo thư mục). |
| POST | /v1/core/video/upload | multipart `file=@...`; `?directory=` (tùy chọn) | Upload video. |
| PUT | /v1/core/video/{videoName} | `{"newName": "..."}`; `?directory=` (tùy chọn) | Đổi tên video. |
| DELETE | /v1/core/video/{videoName} | -; `?directory=` (tùy chọn) | Xóa video. |

---

## Troubleshooting

| Triệu chứng | Nguyên nhân có thể | Cách xử lý |
|-------------|---------------------|------------|
| `Connection refused` khi gọi API | Server chưa chạy hoặc sai host/port. | Start server; kiểm tra `SERVER`; ping/telnet port. |
| Upload trả 400 | Thiếu field `file` hoặc Content-Type không đúng. | Dùng đúng `-F "file=@/đường/dẫn/file.mp4"` (multipart/form-data). |
| Upload trả 409 | File cùng tên đã tồn tại. | Đổi tên file local; hoặc delete file cũ trên server rồi upload lại. |
| Rename/Delete trả 404 | `videoName` sai hoặc file đã bị xóa. | Gọi GET list, lấy đúng `filename` (kể cả extension). Nếu file trong subdirectory, thêm `?directory=...`. |
| Rename trả 409 | Tên mới trùng file khác. | Chọn `newName` chưa tồn tại. |
| List trả 200 nhưng `videos` rỗng | Chưa upload file nào hoặc đang filter sai thư mục. | Upload ít nhất một file; kiểm tra `directory` nếu dùng. |

---

## Tài liệu liên quan

- OpenAPI: `api-specs/openapi/en/paths/video/` (core_video_list, core_video_upload, core_video_videoname).
- INSTANCE_API_MANUAL_TEST.md: Instance có thể dùng video (input file) sau khi upload.
