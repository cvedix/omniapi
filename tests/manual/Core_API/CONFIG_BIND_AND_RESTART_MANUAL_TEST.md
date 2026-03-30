# Hướng dẫn: Cấu hình Bind (chỉ local hay public) và Restart server

> Tài liệu dành cho **người vận hành / không cần biết lập trình**: chỉ cần copy lệnh vào terminal (hoặc dùng Postman/Swagger) và làm theo từng bước.

---

## Mục đích

- **Bind “chỉ local”** (`127.0.0.1`): Chỉ máy đang chạy API mới truy cập được (bảo mật hơn).
- **Bind “public”** (`0.0.0.0`): Thiết bị khác trong mạng (điện thoại, máy tính khác) có thể truy cập API.
- **Restart server**: Sau khi đổi host/port, server cần khởi động lại thì cấu hình mới mới có hiệu lực. Có thể dùng API với `auto_restart=true` để server tự restart sau vài giây.

---

## Chuẩn bị

1. **API đang chạy** (ví dụ: `http://localhost:8080`).
2. **Terminal** (Linux/Mac) hoặc **Command Prompt / PowerShell** (Windows), hoặc **Postman** / **Swagger** (`http://<máy-chạy-api>:8080/v1/swagger`).
3. Nếu dùng **curl**: Mở terminal và thay `http://localhost:8080` bằng địa chỉ API thực tế nếu khác.

---

## Bước 1: Xem cấu hình hiện tại

Gửi request **GET** (không cần body):

```bash
curl -s "http://localhost:8080/v1/core/config?path=system/web_server"
```

Hoặc xem toàn bộ config:

```bash
curl -s "http://localhost:8080/v1/core/config"
```

Trong kết quả, tìm phần **`system.web_server`** (hoặc **`system` → `web_server`**). Các trường quan trọng:

| Trường         | Ý nghĩa                                                                 | Ví dụ        |
|----------------|-------------------------------------------------------------------------|--------------|
| **ip_address** | Địa chỉ bind: `127.0.0.1` = chỉ local, `0.0.0.0` = public               | `"0.0.0.0"`   |
| **bind_mode**  | Cách gọn: `"local"` = chỉ local, `"public"` = public                    | `"public"`    |
| **port**       | Cổng HTTP server                                                        | `8080`        |

---

## Bước 2: Đổi cấu hình (chỉ local hoặc public)

Chỉ cần gửi **những trường bạn muốn đổi**. Các trường không gửi sẽ giữ nguyên.

### Kịch bản A: Chỉ cho máy local truy cập (bảo mật hơn)

**Cách 1 – Dùng `bind_mode` (dễ nhớ):**

```bash
curl -s -X POST "http://localhost:8080/v1/core/config?auto_restart=true" \
  -H "Content-Type: application/json" \
  -d '{
    "system": {
      "web_server": {
        "bind_mode": "local",
        "port": 8080
      }
    }
  }'
```

**Cách 2 – Dùng `ip_address` trực tiếp:**

```bash
curl -s -X POST "http://localhost:8080/v1/core/config?auto_restart=true" \
  -H "Content-Type: application/json" \
  -d '{
    "system": {
      "web_server": {
        "ip_address": "127.0.0.1",
        "port": 8080
      }
    }
  }'
```

- **`auto_restart=true`** trong URL: Server sẽ **tự khởi động lại sau khoảng 3 giây** để áp dụng cấu hình mới.  
- Sau khi restart, API chỉ nghe trên `127.0.0.1`; máy khác trong mạng không truy cập được.

---

### Kịch bản B: Cho mọi thiết bị trong mạng truy cập (public)

**Cách 1 – Dùng `bind_mode`:**

```bash
curl -s -X POST "http://localhost:8080/v1/core/config?auto_restart=true" \
  -H "Content-Type: application/json" \
  -d '{
    "system": {
      "web_server": {
        "bind_mode": "public",
        "port": 8080
      }
    }
  }'
```

**Cách 2 – Dùng `ip_address`:**

```bash
curl -s -X POST "http://localhost:8080/v1/core/config?auto_restart=true" \
  -H "Content-Type: application/json" \
  -d '{
    "system": {
      "web_server": {
        "ip_address": "0.0.0.0",
        "port": 8080
      }
    }
  }'
```

Sau khi server restart, thiết bị khác có thể truy cập API qua `http://<IP-máy-chạy-API>:8080`.

---

### Kịch bản C: Chỉ đổi port (ví dụ 9000), giữ bind như cũ

```bash
curl -s -X POST "http://localhost:8080/v1/core/config?auto_restart=true" \
  -H "Content-Type: application/json" \
  -d '{
    "system": {
      "web_server": {
        "port": 9000
      }
    }
  }'
```

Sau khi restart, truy cập API tại `http://localhost:9000` (hoặc `http://<IP>:9000` nếu đang dùng public).

---

### Kịch bản D: Đổi cấu hình nhưng **không** tự động restart

Bỏ `auto_restart=true` (hoặc đặt `auto_restart=false`). Config vẫn được **lưu**, nhưng server **không** tự restart. Bạn cần **restart thủ công** (ví dụ: `systemctl restart edgeos-api` hoặc tắt/bật lại ứng dụng) thì thay đổi mới có hiệu lực.

Ví dụ – chỉ lưu, không restart:

```bash
curl -s -X POST "http://localhost:8080/v1/core/config" \
  -H "Content-Type: application/json" \
  -d '{
    "system": {
      "web_server": {
        "bind_mode": "local",
        "port": 8080
      }
    }
  }'
```

Response có thể có `"restart_required": true` và nhắn “Please restart server manually…”. Khi đó hãy restart server bằng cách của bạn.

---

## Bước 3: Kiểm tra sau khi restart

1. **Đợi khoảng 5–10 giây** sau khi gọi API có `auto_restart=true`.
2. Gọi health check:

   ```bash
   curl -s "http://localhost:8080/v1/core/health"
   ```

   (Nếu bạn đổi port, thay `8080` bằng port mới, ví dụ `9000`.)

3. Xem lại cấu hình:

   ```bash
   curl -s "http://localhost:8080/v1/core/config?path=system/web_server"
   ```

   Kiểm tra `ip_address` / `bind_mode` và `port` đúng như đã đặt.

---

## Tóm tắt nhanh

| Muốn làm gì              | API / Thao tác                                                                 | Ghi chú                          |
|--------------------------|---------------------------------------------------------------------------------|----------------------------------|
| Xem cấu hình web_server  | **GET** ` /v1/core/config?path=system/web_server`                              | Không cần body                   |
| Đổi bind + port + restart| **POST** `/v1/core/config?auto_restart=true` + body JSON (system.web_server)   | Server tự restart sau ~3 giây    |
| Chỉ local                | `bind_mode: "local"` hoặc `ip_address: "127.0.0.1"`                            | Chỉ máy chạy API truy cập được  |
| Public                   | `bind_mode: "public"` hoặc `ip_address: "0.0.0.0"`                              | Mọi thiết bị trong mạng truy cập |
| Chỉ lưu, không restart   | **POST** `/v1/core/config` (không có `auto_restart=true`)                       | Cần restart thủ công sau         |

---

## Lỗi thường gặp

| Tình huống                         | Cách xử lý                                                                 |
|------------------------------------|----------------------------------------------------------------------------|
| Gọi API trả **Connection refused**| Kiểm tra API đã chạy chưa; nếu vừa dùng `auto_restart=true` thì đợi 5–10s rồi thử lại. |
| Response **400**                  | Body phải là JSON đúng (dấu ngoặc, dấu phẩy, ngoặc kép). Kiểm tra lại hoặc dùng Postman/Swagger. |
| Đổi sang **local** rồi không vào được từ máy khác | Đúng: bind local chỉ cho máy chạy API. Muốn máy khác vào thì đổi lại `bind_mode: "public"` (và restart). |
| Muốn **chỉ đổi port**             | Chỉ gửi `"port": <số_port>` trong `system.web_server`, kèm `auto_restart=true` nếu muốn tự restart. |

---

## Dùng Swagger / Scalar thay vì curl

1. Mở trình duyệt: `http://<máy-chạy-API>:8080/v1/swagger` (Swagger) hoặc mở file `api-specs/scalar/index.html` (Scalar, nếu đã cấu hình đúng server).
2. Tìm **Config** → **POST /v1/core/config** (hoặc tương đương).
3. Nhấn **Try it out**.
4. Trong **Query**: thêm tham số `auto_restart` = `true` nếu muốn server tự restart.
5. Trong **Request body**: điền JSON (ví dụ như các đoạn `system.web_server` ở trên).
6. Nhấn **Execute**.

Kết quả tương tự như gọi curl: nếu thành công và có `auto_restart=true`, đợi vài giây rồi kiểm tra lại health/config.
