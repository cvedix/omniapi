# Behavior Analysis Crossline Instance - Hướng Dẫn Test

## 📋 Tổng Quan

Instance này thực hiện phân tích hành vi đếm phương tiện/phương tiện đi qua đường line (crossline) sử dụng YOLO detector và SORT tracker.

## 🎯 Tính Năng

- ✅ Phát hiện phương tiện với YOLO detector
- ✅ Tracking phương tiện với SORT tracker
- ✅ Đếm phương tiện đi qua đường line (crossline)
- ✅ RTMP streaming output (tùy chọn)
- ✅ MQTT event publishing khi có phương tiện đi qua line
- ✅ Screen display với OSD hiển thị số lượng đếm được

## 📁 Cấu Trúc Files

```
ba_crossline/
├── README.md                                      # File này
├── example_ba_crossline_with_crossing_lines.json  # Example với CrossingLines format (RTMP)
├── example_ba_crossline_mqtt_with_crossing_lines.json  # Example với CrossingLines format (MQTT, 2 lines)
├── example_ba_crossline_rtsp_with_crossing_lines.json  # Example với CrossingLines format (RTSP)
├── test_file_source_mqtt.json                      # Test với file source + MQTT (legacy format)
├── test_rtsp_source_rtmp_mqtt.json                 # Test với RTSP source + RTMP + MQTT
├── test_rtsp_source_mqtt_only.json                 # Test với RTSP source + MQTT only
├── test_rtsp_source_rtmp_only.json                 # Test với RTSP source + RTMP only
├── test_rtmp_output_only.json                      # Test với RTMP output only
└── report_body_example.json                        # Ví dụ report body từ MQTT
```

## 🔧 Solution Config

### Solution ID: `ba_crossline_with_mqtt`

**Pipeline:**
```
File/RTSP Source → YOLO Detector → SORT Tracker → BA Crossline → MQTT Broker → OSD → [Screen | RTMP]
```

**Tham số quan trọng:**
- `WEIGHTS_PATH`, `CONFIG_PATH`, `LABELS_PATH`: YOLO model paths
- `MQTT_BROKER_URL`, `MQTT_PORT`, `MQTT_TOPIC`: MQTT configuration
- `RTMP_URL`: RTMP streaming URL (nếu có)

### 📐 Cấu Hình Crossing Lines

Có **2 cách** để cấu hình crossing lines:

#### Cách 1: Sử dụng `CrossingLines` (Format Mới - Khuyến Nghị) ✅

Sử dụng `CrossingLines` trong `additionalParams` để định nghĩa nhiều lines với đầy đủ thông tin:

```json
{
  "additionalParams": {
    "CrossingLines": "[{\"id\":\"line1\",\"name\":\"Main Line\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Both\",\"classes\":[\"Vehicle\"],\"color\":[255,0,0,255]}]"
  }
}
```

**Ưu điểm:**
- ✅ Hỗ trợ nhiều lines (multiple lines)
- ✅ Có thể quản lý qua API (`/v1/core/instance/{instanceId}/lines`)
- ✅ Hỗ trợ đầy đủ: name, direction, classes, color
- ✅ Real-time update không cần restart

**Format chi tiết:**
- `id`: UUID của line (tự động generate khi tạo qua API)
- `name`: Tên mô tả line (optional)
- `coordinates`: Array các điểm `[{"x": 0, "y": 250}, {"x": 700, "y": 220}]` (tối thiểu 2 điểm)
- `direction`: `"Up"`, `"Down"`, hoặc `"Both"` (mặc định: `"Both"`)
- `classes`: Array các class cần đếm: `["Person", "Vehicle", "Animal", "Face", "Unknown"]`
- `color`: RGBA array `[R, G, B, A]` (mặc định: `[255, 0, 0, 255]` - đỏ)

**Ví dụ với nhiều lines:**
```json
{
  "CrossingLines": "[{\"id\":\"line1\",\"name\":\"Entry\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Up\",\"classes\":[\"Vehicle\"]},{\"id\":\"line2\",\"name\":\"Exit\",\"coordinates\":[{\"x\":100,\"y\":400},{\"x\":800,\"y\":380}],\"direction\":\"Down\",\"classes\":[\"Vehicle\"]}]"
}
```

**Example files:**
- `example_ba_crossline_with_crossing_lines.json` - Basic với 1 line
- `example_ba_crossline_mqtt_with_crossing_lines.json` - MQTT với 2 lines
- `example_ba_crossline_rtsp_with_crossing_lines.json` - RTSP với 1 line

#### Cách 2: Sử dụng `CROSSLINE_START_X/Y` và `CROSSLINE_END_X/Y` (Format Cũ - Legacy)

Sử dụng các tham số trong `input` section (chỉ hỗ trợ 1 line):

```json
{
  "additionalParams": {
    "input": {
      "CROSSLINE_START_X": "0",
      "CROSSLINE_START_Y": "250",
      "CROSSLINE_END_X": "700",
      "CROSSLINE_END_Y": "220"
    }
  }
}
```

**Lưu ý:** Format này chỉ hỗ trợ 1 line và không thể quản lý qua API.

## 📝 Manual Testing Guide

### 1. Test với File Source + MQTT

**Bước 1:** Tạo instance
```bash
curl -X POST http://localhost:8080/v1/core/instance \
  -H "Content-Type: application/json" \
  -d @ba_crossline/test_file_source_mqtt.json
```

**Bước 2:** Kiểm tra status
```bash
curl http://localhost:8080/v1/core/instance/{instanceId}
```

**Bước 3:** Start instance
```bash
curl -X POST http://localhost:8080/v1/core/instance/{instanceId}/start
```

**Bước 4:** Subscribe MQTT để nhận events
```bash
mosquitto_sub -h localhost -t ba_crossline/events -v
```

**Bước 5:** Kiểm tra statistics
```bash
curl http://localhost:8080/v1/core/instance/{instanceId}/statistics
```

### 2. Test với RTSP Source + RTMP + MQTT

**Yêu cầu:**
- RTSP camera/stream
- RTMP server
- MQTT broker

**Các bước:**
```bash
# Tạo instance
curl -X POST http://localhost:8080/v1/core/instance \
  -H "Content-Type: application/json" \
  -d @ba_crossline/test_rtsp_source_rtmp_mqtt.json

# Kiểm tra RTMP stream
ffplay rtmp://your-server:1935/live/stream_key

# Subscribe MQTT
mosquitto_sub -h localhost -t ba_crossline/events -v
```

### 3. Cấu Hình Crossline

**Quan trọng:** Cần cấu hình đúng tọa độ line trong frame.

**Cách xác định tọa độ:**
1. Chạy instance với screen display (`ENABLE_SCREEN_DES: "true"`)
2. Xem frame và xác định điểm bắt đầu và kết thúc của line
3. Cập nhật `CrossingLines` trong `additionalParams` hoặc sử dụng API

**Ví dụ với CrossingLines format:**
- Frame size: 1280x720
- Line từ (0, 250) đến (700, 220)
- Direction: Both (đếm cả 2 chiều)
- Classes: Vehicle

```json
{
  "CrossingLines": "[{\"id\":\"line1\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Both\",\"classes\":[\"Vehicle\"]}]"
}
```

**Quản lý Lines qua API:**

Sau khi tạo instance, bạn có thể quản lý lines qua API:

```bash
# Lấy tất cả lines
curl http://localhost:8080/v1/core/instance/{instanceId}/lines

# Lấy một line cụ thể
curl http://localhost:8080/v1/core/instance/{instanceId}/lines/{lineId}

# Tạo line mới
curl -X POST http://localhost:8080/v1/core/instance/{instanceId}/lines \
  -H "Content-Type: application/json" \
  -d '{
    "name": "New Line",
    "coordinates": [{"x": 100, "y": 300}, {"x": 800, "y": 280}],
    "direction": "Up",
    "classes": ["Vehicle", "Person"],
    "color": [0, 255, 0, 255]
  }'

# Cập nhật line
curl -X PUT http://localhost:8080/v1/core/instance/{instanceId}/lines/{lineId} \
  -H "Content-Type: application/json" \
  -d '{
    "coordinates": [{"x": 200, "y": 350}, {"x": 900, "y": 330}],
    "direction": "Both"
  }'

# Xóa line
curl -X DELETE http://localhost:8080/v1/core/instance/{instanceId}/lines/{lineId}
```

## 📊 Kiểm Tra Kết Quả

### 1. Kiểm Tra Screen Display

- Mở cửa sổ hiển thị video
- Kiểm tra line được vẽ trên frame
- Kiểm tra số lượng đếm được hiển thị trên OSD
- Kiểm tra bounding boxes quanh phương tiện

### 2. Kiểm Tra MQTT Events

**Event structure:**
- Xem `report_body_example.json` để biết cấu trúc chi tiết
- Xem `mqtt_event_with_counts_example.json` để biết format mới với dữ liệu đếm

**Event types:**
- `crossline_enter`: Khi phương tiện đi qua line (từ một phía)
- `crossline_exit`: Khi phương tiện đi qua line (từ phía kia)

**Expected event fields:**
- `type`: "crossline_enter" hoặc "crossline_exit"
- `label`: Mô tả event (ví dụ: "Vehicle crossed line")
- `zone_id`: ID của zone/line
- `zone_name`: Tên của zone/line
- `line_id`: ID của line (tương tự zone_id)
- `extra.track_id`: ID của track
- `extra.class`: Loại phương tiện (car, truck, motorcycle, etc.)
- `extra.current_entries`: Số lượng đếm được hiện tại

**Dữ liệu đếm theo từng line (mới):**
Mỗi MQTT event message giờ đây bao gồm thêm section `line_counts` hiển thị số lượng đã đếm được theo từng line:

```json
{
  "events": [...],
  "frame_id": 34585,
  "frame_time": 1383400.0,
  "system_date": "Sun Dec 28 10:49:10 2025",
  "system_timestamp": "1766893750712",
  "instance_id": "8c6a1534-9dab-4a5a-b968-1d612d4c41a9",
  "instance_name": "ba_crossline_file_mqtt_only",
  "line_counts": [
    {
      "line_id": "default_zone",
      "line_name": "CrosslineZone",
      "count": 15
    },
    {
      "line_id": "line_2",
      "line_name": "Exit Line",
      "count": 8
    }
  ]
}
```

**Lưu ý:**
- Để đếm riêng biệt cho nhiều lines, cần cấu hình `ZONE_ID` khác nhau cho từng line
- Số đếm được tích lũy từ khi instance bắt đầu chạy
- Mỗi event trong `events` array cũng có field `line_id` để xác định line nào đã bị vượt qua
- Field `instance_id`: UUID thực sự của instance (ví dụ: "8c6a1534-9dab-4a5a-b968-1d612d4c41a9")
- Field `instance_name`: Tên instance (từ field `name` trong request, ví dụ: "ba_crossline_file_mqtt_only")

**MQTT Client ID:**
- MQTT Client ID được tự động set theo format: `edgeos-api_{instance_id}`
- Ví dụ: Nếu instance_id là `ba_crossline_file_mqtt`, Client ID sẽ là `edgeos-api_ba_crossline_file_mqtt`
- Điều này giúp dễ dàng nhận biết và quản lý các MQTT connections từ các instances khác nhau

### 3. Kiểm Tra Statistics

```bash
curl http://localhost:8080/v1/core/instance/{instanceId}/statistics
```

**Expected output:**
```json
{
  "frames_processed": 5000,
  "source_framerate": 30.0,
  "current_framerate": 28.5,
  "latency": 150.0,
  "resolution": "1280x720"
}
```

## 🔍 Troubleshooting

### Lỗi: Model không tìm thấy
```bash
# Kiểm tra YOLO model files
ls -la /path/to/models/det_cls/yolov3-tiny-2022-0721_best.weights
ls -la /path/to/models/det_cls/yolov3-tiny-2022-0721.cfg
ls -la /path/to/models/det_cls/yolov3_tiny_5classes.txt
```

### Lỗi: Crossline không hoạt động đúng

**Nguyên nhân có thể:**
1. Tọa độ line không đúng với frame size
2. Line không nằm trong vùng có phương tiện đi qua
3. Tracker không hoạt động tốt

**Giải pháp:**
1. Kiểm tra lại tọa độ line
2. Điều chỉnh line để nằm trong vùng có phương tiện
3. Kiểm tra tracker parameters (nếu có)

### Lỗi: Đếm không chính xác

**Nguyên nhân:**
- Line quá gần hoặc quá xa camera
- Phương tiện đi quá nhanh
- Tracker mất track

**Giải pháp:**
- Điều chỉnh vị trí line
- Tăng frame rate nếu có thể
- Điều chỉnh tracker parameters

### Lỗi: MQTT events không được gửi

```bash
# Kiểm tra MQTT broker
sudo systemctl status mosquitto

# Test connection
mosquitto_sub -h localhost -t ba_crossline/events -v

# Kiểm tra MQTT configuration trong JSON
```

## 📚 Tài Liệu Tham Khảo

- Sample code: `sample/ba_crossline_sample.cpp`
- Sample code: `sample/rtsp_ba_crossline_sample.cpp`
- Testing guide: `sample/README.md` (section: ba_crossline_sample)
