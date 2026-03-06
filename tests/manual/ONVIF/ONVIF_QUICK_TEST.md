# ONVIF Quick Test Commands

Copy-paste các lệnh sau để test nhanh ONVIF với camera Tapo.

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
CAMERA_IP="192.168.1.122"
USERNAME="admincvedix"
PASSWORD="12345678"
```

---

## Test Sequence

### 1. Discover Cameras
```bash
curl -X POST "${SERVER}/v1/onvif/discover?timeout=10"
sleep 10
```

### 2. List Discovered Cameras
```bash
curl "${SERVER}/v1/onvif/cameras" | jq .
```

### 3. Set Credentials
```bash
curl -X POST "${SERVER}/v1/onvif/camera/${CAMERA_IP}/credentials" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}"
```

### 4. Get Streams
```bash
curl "${SERVER}/v1/onvif/streams/${CAMERA_IP}" | jq .
```

### 5. Get Stream URIs Only
```bash
curl -s "${SERVER}/v1/onvif/streams/${CAMERA_IP}" | jq -r '.[].uri'
```

### 6. Get First Stream URI
```bash
curl -s "${SERVER}/v1/onvif/streams/${CAMERA_IP}" | jq -r '.[0].uri'
```

---

## One-Liner Full Test

```bash
SERVER="http://localhost:8080" && \
CAMERA_IP="192.168.1.122" && \
USERNAME="admincvedix" && \
PASSWORD="12345678" && \
echo "=== Step 1: Discover ===" && \
curl -X POST "${SERVER}/v1/onvif/discover?timeout=10" && \
echo -e "\n\nWaiting 10 seconds..." && \
sleep 10 && \
echo -e "\n=== Step 2: Get Cameras ===" && \
curl -s "${SERVER}/v1/onvif/cameras" | jq . && \
echo -e "\n=== Step 3: Set Credentials ===" && \
curl -X POST "${SERVER}/v1/onvif/camera/${CAMERA_IP}/credentials" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}" && \
echo -e "\n\n=== Step 4: Get Streams ===" && \
curl -s "${SERVER}/v1/onvif/streams/${CAMERA_IP}" | jq .
```

---

## Test RTSP Stream

Sau khi có stream URI từ bước 4:

```bash
# Lấy stream URI đầu tiên
STREAM_URI=$(curl -s "${SERVER}/v1/onvif/streams/${CAMERA_IP}" | jq -r '.[0].uri')
echo "Stream URI: ${STREAM_URI}"

# Test với ffplay
ffplay "${STREAM_URI}"

# Hoặc test với VLC
vlc "${STREAM_URI}"

# Hoặc record 10 giây với ffmpeg
ffmpeg -i "${STREAM_URI}" -t 10 -c copy test_output.mp4
```

---

## Check Logs

Xem log để theo dõi authentication:

```bash
# Nếu log file có sẵn
tail -f /path/to/logfile | grep ONVIF

# Hoặc nếu log ra console
# Xem trong terminal đang chạy server
```

**Log patterns cần tìm**:
- `✓ Method 2 succeeded!` - PasswordDigest thành công
- `Caching for future requests` - Đã cache phương án thành công
- `Trying cached auth method` - Đang dùng cache
- `parseStreamUri: Found stream URI` - Đã parse được stream URI

---

## Troubleshooting Commands

### Check if camera exists
```bash
curl -s "${SERVER}/v1/onvif/cameras" | jq '.[] | select(.ip == "'${CAMERA_IP}'")'
```

### Check credentials were set
```bash
# Credentials được lưu trong memory, không có endpoint để check
# Nhưng có thể test bằng cách get streams - nếu fail thì credentials sai
curl "${SERVER}/v1/onvif/streams/${CAMERA_IP}"
```

### Test with verbose curl
```bash
curl -v -X POST "${SERVER}/v1/onvif/camera/${CAMERA_IP}/credentials" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}"
```

### Test with wrong credentials (should fail)
```bash
curl -X POST "${SERVER}/v1/onvif/camera/${CAMERA_IP}/credentials" \
  -H "Content-Type: application/json" \
  -d '{"username":"wrong","password":"wrong"}' && \
curl "${SERVER}/v1/onvif/streams/${CAMERA_IP}"
```

---

## Expected Output Examples

### Step 2: Get Cameras
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

### Step 4: Get Streams
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
  }
]
```

---

## Performance Test (Caching)

Test xem caching có hoạt động không:

```bash
# Request 1: Sẽ thử tất cả methods (chậm)
time curl -s "${SERVER}/v1/onvif/streams/${CAMERA_IP}" > /dev/null

# Request 2: Sẽ dùng cache (nhanh hơn)
time curl -s "${SERVER}/v1/onvif/streams/${CAMERA_IP}" > /dev/null
```

Request 2 sẽ nhanh hơn vì đã cache phương án authentication thành công.

