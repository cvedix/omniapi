# Behavior Analysis Area Enter/Exit Instance - Hướng Dẫn

## 📋 Tổng Quan

Instance này thực hiện phân tích hành vi phát hiện đối tượng vào/ra khỏi các vùng (areas) sử dụng YOLO detector và ByteTrack tracker.

## 🎯 Tính Năng

- ✅ Phát hiện đối tượng với YOLO detector
- ✅ Tracking đối tượng với ByteTrack tracker
- ✅ Phát hiện đối tượng vào/ra khỏi các vùng (areas)
- ✅ Cảnh báo khi đối tượng vào/ra vùng (có thể cấu hình riêng)
- ✅ RTMP streaming output (tùy chọn)
- ✅ Screen display với OSD hiển thị thông tin areas và alerts
- ✅ Hỗ trợ nhiều areas trên cùng một channel

## 📁 Cấu Trúc Files

```
ba_area_enter_exit/
├── README.md                                    # File này
├── api_examples.md                              # Ví dụ sử dụng API
├── example_ba_area_file.json                    # Example với file source
├── example_ba_area_rtmp.json                   # Example với RTMP output
├── example_ba_area_multiple_areas.json         # Example với nhiều areas
├── example_ba_area_custom_config.json          # Example với cấu hình tùy chỉnh
├── example_create_area.json                     # Example tạo area qua API
├── example_create_area_restricted.json         # Example tạo restricted area
└── example_create_area_polygon.json            # Example tạo polygon area
```

## 🔧 Solution Config

### Solution ID: `ba_area_enter_exit`

**Pipeline:**
```
File/RTSP Source → YOLO Detector → ByteTrack Tracker → BA Area Enter/Exit → OSD → [Screen | RTMP]
```

**Tham số quan trọng:**
- `WEIGHTS_PATH`, `CONFIG_PATH`, `LABELS_PATH`: YOLO model paths
- `FILE_PATH`: Đường dẫn file video (nếu dùng file source)
- `RTSP_URL`: RTSP URL (nếu dùng RTSP source)
- `RTMP_DES_URL`: RTMP streaming URL (nếu có)
- `Areas`: JSON string định nghĩa các vùng (rectangles)
- `AreaConfigs`: JSON string cấu hình cho từng vùng

## 📐 Cấu Hình Areas

### Format Areas

Areas được định nghĩa là JSON string trong `additionalParams`:

```json
{
  "additionalParams": {
    "Areas": "[{\"x\":50,\"y\":150,\"width\":200,\"height\":200},{\"x\":350,\"y\":160,\"width\":200,\"height\":200}]"
  }
}
```

**Mỗi area là một rectangle với:**
- `x`: Tọa độ X của góc trên bên trái (pixels)
- `y`: Tọa độ Y của góc trên bên trái (pixels)
- `width`: Chiều rộng của vùng (pixels)
- `height`: Chiều cao của vùng (pixels)

**Ví dụ:**
```json
[
  {"x": 50, "y": 150, "width": 200, "height": 200},   // Area 0: Entrance
  {"x": 350, "y": 160, "width": 200, "height": 200}   // Area 1: Restricted
]
```

### Format AreaConfigs

AreaConfigs được định nghĩa là JSON string, số lượng phải khớp với số lượng Areas:

```json
{
  "additionalParams": {
    "AreaConfigs": "[{\"alertOnEnter\":true,\"alertOnExit\":true,\"name\":\"Entrance\",\"color\":[0,220,0]},{\"alertOnEnter\":true,\"alertOnExit\":true,\"name\":\"Restricted\",\"color\":[0,0,220]}]"
  }
}
```

**Mỗi config có các thuộc tính:**
- `alertOnEnter`: `true` nếu muốn cảnh báo khi đối tượng vào vùng, `false` nếu không
- `alertOnExit`: `true` nếu muốn cảnh báo khi đối tượng ra khỏi vùng, `false` nếu không
- `name`: Tên mô tả vùng (hiển thị trên OSD)
- `color`: Màu hiển thị vùng `[R, G, B]` (0-255), ví dụ: `[0, 220, 0]` là màu xanh lá

**Ví dụ:**
```json
[
  {
    "alertOnEnter": true,
    "alertOnExit": true,
    "name": "Entrance",
    "color": [0, 220, 0]    // Màu xanh lá
  },
  {
    "alertOnEnter": true,
    "alertOnExit": false,
    "name": "Restricted",
    "color": [0, 0, 220]    // Màu đỏ
  }
]
```

## 🚀 Cách Sử Dụng

### 1. Tạo Instance qua SecuRT API (Khuyến nghị)

```bash
curl -X POST http://localhost:8080/v1/securt/instance \
  -H "Content-Type: application/json" \
  -d @example_ba_area_file.json
```

**Lưu ý:** SecuRT API hỗ trợ solution `ba_area_enter_exit`. Bạn có thể tạo instance với solution này và quản lý qua SecuRT API.

### 2. Tạo Instance qua Core API

```bash
curl -X POST http://localhost:8080/v1/core/instance \
  -H "Content-Type: application/json" \
  -d '{
    "name": "BA Area Instance",
    "solution": "ba_area_enter_exit",
    "autoStart": false,
    "additionalParams": {
      "input": {
        "FILE_PATH": "/path/to/video.mp4",
        "WEIGHTS_PATH": "/path/to/weights.weights",
        "CONFIG_PATH": "/path/to/config.cfg",
        "LABELS_PATH": "/path/to/labels.txt",
        "RESIZE_RATIO": "0.6"
      },
      "Areas": "[{\"x\":50,\"y\":150,\"width\":200,\"height\":200}]",
      "AreaConfigs": "[{\"alertOnEnter\":true,\"alertOnExit\":true,\"name\":\"Entrance\",\"color\":[0,220,0]}]"
    }
  }'
```

### 3. Start Instance

```bash
INSTANCE_ID="your-instance-id"

curl -X POST http://localhost:8080/v1/core/instance/${INSTANCE_ID}/start
```

### 4. Kiểm tra Statistics

```bash
curl http://localhost:8080/v1/securt/instance/${INSTANCE_ID}/stats
```

### 5. Stop Instance

```bash
curl -X POST http://localhost:8080/v1/core/instance/${INSTANCE_ID}/stop
```

### 6. Xóa Instance

```bash
curl -X DELETE http://localhost:8080/v1/securt/instance/${INSTANCE_ID}
```

### 7. Quản Lý Areas qua API

#### Tạo Object Enter/Exit Area

```bash
curl -X POST http://localhost:8080/v1/securt/instance/${INSTANCE_ID}/area/objectEnterExit \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Entrance Area",
    "coordinates": [
      {"x": 50, "y": 150},
      {"x": 250, "y": 150},
      {"x": 250, "y": 350},
      {"x": 50, "y": 350}
    ],
    "classes": ["Person", "Vehicle"],
    "color": [0, 220, 0, 255],
    "alertOnEnter": true,
    "alertOnExit": true
  }'
```

#### Lấy Tất Cả Areas

```bash
curl http://localhost:8080/v1/securt/instance/${INSTANCE_ID}/areas
```

#### Xóa Area

```bash
curl -X DELETE http://localhost:8080/v1/securt/instance/${INSTANCE_ID}/area/{areaId}
```

## 📝 Example Files

### example_ba_area_file.json
Instance cơ bản với file source, 2 areas (Entrance và Restricted).

### example_ba_area_rtmp.json
Instance với RTMP streaming output.

### example_ba_area_multiple_areas.json
Instance với nhiều areas (3+ areas).

### example_ba_area_custom_config.json
Instance với cấu hình tùy chỉnh (different alert settings, colors).

### example_create_area.json
Example JSON để tạo area qua API (POST /v1/securt/instance/{instanceId}/area/objectEnterExit).

### example_create_area_restricted.json
Example tạo restricted area với chỉ alertOnEnter.

### example_create_area_polygon.json
Example tạo polygon area với nhiều điểm (5 điểm).

## 🔍 Monitoring

### Xem OSD Output
- Nếu `ENABLE_SCREEN_DES = "true"`: Mở cửa sổ hiển thị video với OSD
- Nếu có RTMP output: Xem stream qua VLC hoặc player khác

### Xem Logs
```bash
# Xem logs của instance
tail -f /var/log/omniapi/instance-${INSTANCE_ID}.log
```

## ⚙️ Cấu Hình Nâng Cao

### Tham số Input

| Tham số | Mô tả | Mặc định |
|---------|-------|----------|
| `FILE_PATH` | Đường dẫn file video | Required |
| `RTSP_URL` | RTSP stream URL | - |
| `WEIGHTS_PATH` | Đường dẫn YOLO weights file | Required |
| `CONFIG_PATH` | Đường dẫn YOLO config file | Required |
| `LABELS_PATH` | Đường dẫn labels file | Required |
| `RESIZE_RATIO` | Tỷ lệ resize video (0.0-1.0) | 0.6 |

### Tham số Output

| Tham số | Mô tả | Mặc định |
|---------|-------|----------|
| `ENABLE_SCREEN_DES` | Bật/tắt screen display | "false" |
| `RTMP_DES_URL` | RTMP streaming URL | - |

### Tham số Areas

Có 2 cách để quản lý areas:

#### Cách 1: Legacy Format (trong additionalParams)

| Tham số | Mô tả | Format |
|---------|-------|--------|
| `Areas` | JSON string định nghĩa các vùng | `[{"x":int,"y":int,"width":int,"height":int},...]` |
| `AreaConfigs` | JSON string cấu hình vùng | `[{"alertOnEnter":bool,"alertOnExit":bool,"name":str,"color":[int,int,int]},...]` |

#### Cách 2: API Quản Lý Areas (Khuyến nghị)

Sử dụng API `/v1/securt/instance/{instanceId}/area/objectEnterExit` để quản lý areas động:

**Tạo Area:**
```bash
POST /v1/securt/instance/{instanceId}/area/objectEnterExit
{
  "name": "Area Name",
  "coordinates": [{"x": 50, "y": 150}, {"x": 250, "y": 150}, {"x": 250, "y": 350}, {"x": 50, "y": 350}],
  "classes": ["Person", "Vehicle"],
  "color": [0, 220, 0, 255],
  "alertOnEnter": true,
  "alertOnExit": true
}
```

**Lấy Tất Cả Areas:**
```bash
GET /v1/securt/instance/{instanceId}/areas
```

**Xóa Area:**
```bash
DELETE /v1/securt/instance/{instanceId}/area/{areaId}
```

**Ưu điểm của API:**
- ✅ Quản lý areas động, không cần restart instance
- ✅ Hỗ trợ polygon (nhiều điểm), không chỉ rectangle
- ✅ Có thể thêm/xóa/sửa areas real-time
- ✅ Dễ dàng tích hợp vào UI/automation

## 🐛 Troubleshooting

### Instance không start được
- Kiểm tra đường dẫn file video và model paths
- Kiểm tra format JSON của Areas và AreaConfigs
- Xem logs để biết lỗi chi tiết

### Areas không hoạt động
- Đảm bảo số lượng AreaConfigs khớp với số lượng Areas
- Kiểm tra format JSON (phải là string, không phải object)
- Kiểm tra tọa độ areas có nằm trong frame không

### Không có alerts
- Kiểm tra `alertOnEnter` và `alertOnExit` đã được set đúng chưa
- Kiểm tra detector có phát hiện được đối tượng không
- Kiểm tra tracker có track được đối tượng không

## 📚 Tài Liệu Tham Khảo

- [BA Area Enter/Exit API Examples](api_examples.md)
- [Manual Test Guide](../../../tests/manual/Analytics/BA_AREA_ENTER_EXIT_API_TEST.md)
- [Core API Documentation](../../../docs/API_document.md)

