# Hướng dẫn test thủ công - Global Face Detection Toggle (mọi solution)

> Mục tiêu: xác nhận có thể bật/tắt face detection cho instance thuộc **bất kỳ solution** thông qua `AdditionalParams.ENABLE_FACE_DETECTION` sau khi instance đã được tạo và start.

---

## 1) Chuẩn bị

- API server chạy tại `http://localhost:8080`.
- Có model face detector (YuNet), ví dụ:
  - `/opt/edgeos-api/models/face/face_detection_yunet_2023mar.onnx`
- Cài `curl`, `jq`.

---

## 2) Tạo instance `ba_crossline` (payload mẫu)

> Dùng đúng body bạn cung cấp để tái hiện thực tế.

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "ba_crossline_rtmp_mqtt_rtmp",
    "group": "demo",
    "solution": "ba_crossline",
    "autoStart": false,
    "additionalParams": {
      "input": {
        "FILE_PATH": "rtsp://192.168.1.128:554/live/camera_demo_sang_vehicle",
        "MODEL_PATH": "/opt/edgeos-api/models/yolov11/tensorrt/yolo11n.engine",
        "LABELS_PATH": "/opt/edgeos-api/models/yolov11/tensorrt/labels.txt",
        "RESIZE_RATIO": "0.5",
        "SKIP_INTERVAL": "1",
        "RTSP_TRANSPORT": "tcp",
        "GST_DECODER_NAME": "nvh264dec"
      },
      "output": {
        "RTMP_ENCODER": "nvvideoconvert ! nvv4l2h264enc",
        "RTMP_BITRATE_KBPS": "2500",
        "RTMP_URL": "rtmp://192.168.1.128:1935/live/ba_crossing_stream_1",
        "RTMP_DES_URL": "rtmp://192.168.1.128:1935/live/ba_crossing_stream_1",
        "ENABLE_SCREEN_DES": "false",
        "MQTT_BROKER_URL": "mqtt.goads.com.vn",
        "MQTT_PORT": "1883",
        "MQTT_TOPIC": "ba_crossline/events",
        "MQTT_USERNAME": "",
        "MQTT_PASSWORD": ""
      },
      "CrossingLines": "[{\"id\":\"line1\",\"name\":\"RTMP Stream Crossline\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Both\",\"classes\":[\"Vehicle\"],\"color\":[255,0,0,255]},{\"id\":\"line2\",\"name\":\"RTMP Stream Crossline 2\",\"coordinates\":[{\"x\":100,\"y\":400},{\"x\":800,\"y\":380}],\"direction\":\"Both\",\"classes\":[\"Vehicle\"],\"color\":[0,255,0,255]}]"
    }
  }' | jq .
```

**Expected**
- HTTP `201`
- Có `instanceId` trong response.

---

## 3) Start instance

```bash
curl -s -X POST "http://localhost:8080/v1/core/instance/<INSTANCE_ID>/start" | jq .
```

**Expected**
- HTTP `200`
- Instance chuyển trạng thái running.

---

## 4) Bật face detection khi instance đang chạy

```bash
curl -s -X PUT "http://localhost:8080/v1/core/instance/<INSTANCE_ID>/config" \
  -H "Content-Type: application/json" \
  -d '{
    "AdditionalParams": {
      "ENABLE_FACE_DETECTION": "true",
      "FACE_DETECTION_MODEL_PATH": "/opt/edgeos-api/models/face/face_detection_yunet_2023mar.onnx"
    }
  }' | jq .
```

**Expected**
- HTTP `200` (hoặc status thành công tương đương API hiện tại).
- Instance tự apply cấu hình mới (với instance đang running sẽ có rebuild/restart pipeline để áp dụng).

---

## 5) Xác minh sau khi bật

### 5.1 Kiểm tra config đã lưu

```bash
curl -s "http://localhost:8080/v1/core/instance/<INSTANCE_ID>/config" | jq '.AdditionalParams'
```

**Expected**
- Có:
  - `"ENABLE_FACE_DETECTION": "true"`
  - `"FACE_DETECTION_MODEL_PATH": "...yunet...onnx"`

### 5.2 Kiểm tra instance vẫn chạy

```bash
curl -s "http://localhost:8080/v1/core/instance/<INSTANCE_ID>/state" | jq .
```

**Expected**
- Instance ở trạng thái hoạt động bình thường sau khi apply.

### 5.3 (Khuyến nghị) kiểm tra log

- Tìm dòng tương tự:
  - `Injected optional_face_detector node (ENABLE_FACE_DETECTION=true)`

---

## 6) Tắt lại face detection (rollback)

```bash
curl -s -X PUT "http://localhost:8080/v1/core/instance/<INSTANCE_ID>/config" \
  -H "Content-Type: application/json" \
  -d '{
    "AdditionalParams": {
      "ENABLE_FACE_DETECTION": "false"
    }
  }' | jq .
```

**Expected**
- HTTP thành công.
- Pipeline áp dụng lại cấu hình không có optional face detector.

---

## 7) Kết quả PASS/FAIL

- **PASS** khi:
  - Tạo + start instance `ba_crossline` thành công.
  - Bật được `ENABLE_FACE_DETECTION` bằng config API sau khi đang chạy.
  - Config phản ánh đúng trạng thái bật/tắt.
  - Instance vẫn hoạt động ổn định trước/sau khi toggle.

- **FAIL** khi:
  - Không apply được update config.
  - Bật cờ nhưng pipeline không inject face detector (không có dấu hiệu trong log).
  - Instance lỗi sau khi toggle.

---

## 8) Troubleshooting nhanh

- `FACE_DETECTION_MODEL_PATH` sai hoặc file không tồn tại:
  - Kiểm tra đường dẫn model.
- Update config trả lỗi:
  - Kiểm tra `instanceId`, trạng thái instance, payload JSON.
- Không thấy hiệu lực:
  - Kiểm tra lại `GET .../config` xem `ENABLE_FACE_DETECTION` đã được lưu chưa.
