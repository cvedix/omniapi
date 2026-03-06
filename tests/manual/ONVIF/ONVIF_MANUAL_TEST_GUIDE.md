# ONVIF Manual Test Guide - Tapo Camera

Hướng dẫn test thủ công các chức năng ONVIF với camera Tapo, từng bước một.

## Prerequisites

- Server đang chạy tại `http://localhost:8080` (hoặc URL khác)
- Camera Tapo đã bật và kết nối mạng
- Biết IP của camera (ví dụ: `192.168.1.122`)
- Biết username/password của camera (ví dụ: `admincvedix` / `12345678`)

---

## Bước 1: Khởi động Discovery

**Mục đích**: Tìm camera ONVIF trên mạng

**Lệnh**:
```bash
curl -X POST "http://localhost:8080/v1/onvif/discover?timeout=10"
```

**Kết quả mong đợi**:
- HTTP Status: `204 No Content` (không có body)
- Log sẽ hiển thị: `[ONVIFDiscovery] Discovery started`

**Chờ**: Đợi 10 giây để discovery hoàn tất

---

## Bước 2: Lấy danh sách cameras đã discover

**Mục đích**: Xem các camera đã được tìm thấy

**Lệnh**:
```bash
curl "http://localhost:8080/v1/onvif/cameras"
```

**Kết quả mong đợi**:
```json
[
  {
    "ip": "192.168.1.122",
    "manufacturer": "",
    "model": "",
    "serialNumber": "",
    "uuid": ""
  }
]
```

**Ghi chú**: 
- Lưu lại `ip` của camera (ví dụ: `192.168.1.122`)
- Đây sẽ là `cameraId` cho các bước tiếp theo

---

## Bước 3: Set credentials cho camera

**Mục đích**: Cung cấp username/password để authenticate với camera

**Lệnh**:
```bash
curl -X POST "http://localhost:8080/v1/onvif/camera/192.168.1.122/credentials" \
  -H "Content-Type: application/json" \
  -d '{
    "username": "admincvedix",
    "password": "12345678"
  }'
```

**Thay thế**:
- `192.168.1.122` → IP của camera bạn
- `admincvedix` → Username của camera
- `12345678` → Password của camera

**Kết quả mong đợi**:
- HTTP Status: `200 OK`
- Response body: `{"success": true}` hoặc tương tự
- Log sẽ hiển thị: `[ONVIFCredentialsManager] Credentials set for camera`

---

## Bước 4: Lấy streams từ camera

**Mục đích**: Lấy danh sách RTSP stream URIs từ camera

**Lệnh**:
```bash
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122"
```

**Thay thế**: `192.168.1.122` → IP của camera bạn

**Kết quả mong đợi**:
```json
[
  {
    "token": "profile_1",
    "uri": "rtsp://192.168.1.122:554/stream1",
    "width": 1920,
    "height": 1080,
    "fps": 30
  },
  {
    "token": "profile_2",
    "uri": "rtsp://192.168.1.122:554/stream2",
    "width": 1280,
    "height": 720,
    "fps": 30
  },
  {
    "token": "profile_3",
    "uri": "rtsp://192.168.1.122:554/stream8",
    "width": 640,
    "height": 360,
    "fps": 15
  }
]
```

**Kiểm tra**:
- ✓ Có ít nhất 1 stream URI
- ✓ URI có format: `rtsp://IP:PORT/streamX`
- ✓ width/height/fps có giá trị hợp lệ (không phải số âm hoặc quá lớn)

---

## Bước 5: Test RTSP stream (Optional)

**Mục đích**: Xác minh stream URI có thể phát được

**Lệnh**:
```bash
# Sử dụng ffplay
ffplay rtsp://192.168.1.122:554/stream1

# Hoặc sử dụng VLC
vlc rtsp://192.168.1.122:554/stream1

# Hoặc sử dụng ffmpeg để record
ffmpeg -i rtsp://192.168.1.122:554/stream1 -t 10 -c copy test_output.mp4
```

**Thay thế**: `rtsp://192.168.1.122:554/stream1` → URI từ bước 4

---

## Kiểm tra Logs

**Xem log của server** để theo dõi quá trình authentication:

### Log quan trọng cần kiểm tra:

1. **Camera Detection**:
   ```
   [ONVIFTapoHandler] ✓ Detected Tapo by port 2020
   ```

2. **Authentication Method**:
   ```
   [ONVIFTapoHandler] ✓ Method 2 succeeded! Caching for future requests
   ```
   - Method 2 = WS-Security PasswordDigest (phổ biến nhất cho Tapo)

3. **GetProfiles Success**:
   ```
   [ONVIFTapoHandler] ✓ Successfully parsed 3 profile(s)
   ```

4. **GetStreamUri Success**:
   ```
   [ONVIFTapoHandler] ✓ Method 2 succeeded for getStreamUri!
   [ONVIFXmlParser] parseStreamUri: Found stream URI: rtsp://...
   ```

5. **Caching**:
   ```
   [ONVIFTapoHandler] Trying cached auth method: PasswordDigest=true, HTTP Basic Auth=false
   [ONVIFTapoHandler] ✓ Cached auth method succeeded
   ```

---

## Troubleshooting

### Vấn đề 1: Không tìm thấy camera

**Triệu chứng**: Bước 2 trả về mảng rỗng `[]`

**Giải pháp**:
- Kiểm tra camera đã bật ONVIF chưa
- Kiểm tra camera và server cùng mạng
- Tăng timeout: `curl -X POST "http://localhost:8080/v1/onvif/discover?timeout=30"`
- Kiểm tra firewall không block UDP port 3702

### Vấn đề 2: Set credentials fail

**Triệu chứng**: HTTP 400 hoặc 404

**Giải pháp**:
- Kiểm tra cameraId (IP) đúng chưa
- Kiểm tra JSON format đúng
- Kiểm tra camera đã được discover chưa

### Vấn đề 3: Get streams fail hoặc trả về rỗng

**Triệu chứng**: HTTP 200 nhưng mảng rỗng `[]` hoặc HTTP 500

**Giải pháp**:
- Kiểm tra credentials đúng chưa
- Xem log để biết phương án authentication nào được dùng
- Kiểm tra camera có hỗ trợ ONVIF Media Service không

### Vấn đề 4: Stream URI không phát được

**Triệu chứng**: ffplay/vlc không thể kết nối

**Giải pháp**:
- Kiểm tra RTSP port (thường là 554)
- Kiểm tra firewall không block RTSP
- Thử stream URI khác từ cùng camera
- Kiểm tra credentials trong RTSP URL (nếu cần)

---

## Test Cases Chi Tiết

### Test Case 1: Full Workflow
```bash
# 1. Discover
curl -X POST "http://localhost:8080/v1/onvif/discover?timeout=10"
sleep 10

# 2. Get cameras
curl "http://localhost:8080/v1/onvif/cameras"

# 3. Set credentials (thay IP và credentials)
curl -X POST "http://localhost:8080/v1/onvif/camera/192.168.1.122/credentials" \
  -H "Content-Type: application/json" \
  -d '{"username":"admincvedix","password":"12345678"}'

# 4. Get streams
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122"
```

### Test Case 2: Test Caching
```bash
# Lần 1: Sẽ thử tất cả methods và cache phương án thành công
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122"

# Lần 2: Sẽ dùng cached method (nhanh hơn)
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122"

# Kiểm tra log: Lần 2 sẽ thấy "Trying cached auth method"
```

### Test Case 3: Test Multiple Profiles
```bash
# Get streams để xem có bao nhiêu profiles
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122" | jq '.[].token'

# Mỗi profile sẽ có stream URI riêng
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122" | jq '.[].uri'
```

### Test Case 4: Test Error Handling
```bash
# Test với credentials sai
curl -X POST "http://localhost:8080/v1/onvif/camera/192.168.1.122/credentials" \
  -H "Content-Type: application/json" \
  -d '{"username":"wrong","password":"wrong"}'

# Sau đó thử get streams - sẽ fail
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122"

# Set lại credentials đúng
curl -X POST "http://localhost:8080/v1/onvif/camera/192.168.1.122/credentials" \
  -H "Content-Type: application/json" \
  -d '{"username":"admincvedix","password":"12345678"}'

# Get streams lại - sẽ thành công
curl "http://localhost:8080/v1/onvif/streams/192.168.1.122"
```

---

## Expected Results Summary

| Bước | Endpoint | Method | Expected Status | Expected Result |
|------|----------|--------|----------------|-----------------|
| 1 | `/v1/onvif/discover` | POST | 204 | Discovery started |
| 2 | `/v1/onvif/cameras` | GET | 200 | Array of cameras |
| 3 | `/v1/onvif/camera/{id}/credentials` | POST | 200 | Credentials set |
| 4 | `/v1/onvif/streams/{id}` | GET | 200 | Array of streams |

---

## Notes

- **Caching**: Sau khi method authentication thành công, nó sẽ được cache để các request tiếp theo nhanh hơn
- **Fallback**: Hệ thống tự động thử 4 phương án authentication:
  1. WS-Security PasswordText (no HTTP Basic Auth)
  2. WS-Security PasswordDigest (no HTTP Basic Auth) ← **Phổ biến nhất cho Tapo**
  3. WS-Security PasswordText + HTTP Basic Auth
  4. WS-Security PasswordDigest + HTTP Basic Auth
- **Logs**: Luôn kiểm tra log để hiểu rõ quá trình authentication
- **Performance**: Request đầu tiên sẽ chậm hơn (thử nhiều methods), request sau sẽ nhanh hơn (dùng cache)

