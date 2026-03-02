# Hướng Dẫn Sử Dụng edgeos-api

## Tổng Quan

edgeos-api là REST API server cho phép điều khiển và giám sát các AI processing instances trên thiết bị biên. API cung cấp các chức năng quản lý instance, nhận diện khuôn mặt, cấu hình hệ thống, và nhiều tính năng khác.

**Base URL**: `http://localhost:8080`

**Swagger UI**: `http://localhost:8080/swagger` (dùng để test API trực tiếp)

---

## 1. Core API - Kiểm Tra Hệ Thống

### Health Check
Kiểm tra trạng thái hoạt động của API server.

```bash
GET /v1/core/health
```

**Ví dụ:**
```bash
curl http://localhost:8080/v1/core/health
```

**Kết quả:** Trả về `"status": "healthy"` nếu server đang hoạt động tốt.

### Version Information
Lấy thông tin phiên bản API.

```bash
GET /v1/core/version
```

### System Info
Lấy thông tin phần cứng hệ thống (CPU, RAM, GPU, Disk).

```bash
GET /v1/core/system/info
```

### System Status
Lấy trạng thái runtime (CPU/RAM usage, uptime).

```bash
GET /v1/core/system/status
```

### Metrics
Lấy metrics theo định dạng Prometheus.

```bash
GET /v1/core/metrics
```

---

## 2. Instances API - Quản Lý AI Instance

AI Instance là một pipeline xử lý AI (ví dụ: nhận diện khuôn mặt, phát hiện vật thể).

### Liệt Kê Tất Cả Instances
```bash
GET /v1/core/instance
```

### Tạo Instance Mới
Tạo một AI instance mới với cấu hình cụ thể.

```bash
POST /v1/core/instance
Content-Type: application/json

{
  "name": "Camera 1",
  "solution": "face_detection",
  "autoStart": true,
  "persistent": true,
  "additionalParams": {
    "RTSP_URL": "rtsp://192.168.1.100:8554/stream"
  }
}
```

**Tham số Request Body (CreateInstanceRequest):**

**Tham số bắt buộc:**
- `name` (string, required): Tên instance. Pattern: `^[A-Za-z0-9 -_]+$` (chỉ chấp nhận chữ, số, khoảng trắng, dấu gạch ngang và gạch dưới)
  - Ví dụ: `"Camera 1"`, `"Face_Detection_Camera"`

**Tham số tùy chọn:**
- `group` (string, optional): Tên group để nhóm các instances. Pattern: `^[A-Za-z0-9 -_]+$`
  - Ví dụ: `"cameras"`, `"floor_1_cameras"`

- `solution` (string, optional): ID của solution template (phải khớp với solution đã tồn tại)
  - Ví dụ: `"face_detection"`, `"object_detection"`, `"ba_crossline"`

- `persistent` (boolean, default: `false`): Lưu instance vào file JSON để tự động load khi server restart
  - `true`: Instance sẽ được lưu và tự động khởi động lại khi server khởi động lại
  - `false`: Instance chỉ tồn tại trong memory, mất đi khi server restart

- `frameRateLimit` (integer, default: `0`): Giới hạn frame rate (FPS)
  - `0`: Không giới hạn
  - Giá trị dương: Giới hạn số FPS xử lý (ví dụ: `30` = tối đa 30 FPS)

- `metadataMode` (boolean, default: `false`): Bật chế độ metadata
  - `true`: Instance sẽ xuất metadata về các object được phát hiện
  - `false`: Chỉ xử lý video, không xuất metadata

- `statisticsMode` (boolean, default: `false`): Bật chế độ thống kê
  - `true`: Thu thập và lưu thống kê (FPS, latency, số lượng objects, ...)
  - `false`: Không thu thập thống kê

- `diagnosticsMode` (boolean, default: `false`): Bật chế độ chẩn đoán
  - `true`: Ghi log chi tiết để debug
  - `false`: Log thông thường

- `debugMode` (boolean, default: `false`): Bật chế độ debug
  - `true`: In ra các thông tin debug chi tiết
  - `false`: Chế độ production

- `detectorMode` (string, default: `"SmartDetection"`): Chế độ detector
  - Giá trị: `"SmartDetection"`

- `detectionSensitivity` (string, default: `"Low"`): Độ nhạy phát hiện
  - Giá trị: `"Low"`, `"Medium"`, `"High"`

- `movementSensitivity` (string, default: `"Low"`): Độ nhạy chuyển động
  - Giá trị: `"Low"`, `"Medium"`, `"High"`

- `sensorModality` (string, default: `"RGB"`): Loại cảm biến
  - Giá trị: `"RGB"`, `"Thermal"`

- `autoStart` (boolean, default: `false`): Tự động khởi động instance sau khi tạo
  - `true`: Instance sẽ tự động start ngay sau khi được tạo
  - `false`: Instance được tạo nhưng ở trạng thái stopped, cần gọi `/start` để khởi động

- `autoRestart` (boolean, default: `false`): Tự động khởi động lại khi gặp lỗi
  - `true`: Tự động restart instance nếu bị crash hoặc lỗi
  - `false`: Dừng instance khi gặp lỗi

- `blockingReadaheadQueue` (boolean, default: `false`): Sử dụng blocking readahead queue
  - `true`: Sử dụng blocking queue để điều khiển tốc độ xử lý
  - `false`: Non-blocking queue

- `inputOrientation` (integer, default: `0`): Hướng xoay input (0-3)
  - `0`: Không xoay
  - `1`: Xoay 90 độ
  - `2`: Xoay 180 độ
  - `3`: Xoay 270 độ

- `inputPixelLimit` (integer, default: `0`): Giới hạn số pixel input
  - `0`: Không giới hạn
  - Giá trị dương: Giới hạn số pixel (ví dụ: `1920` = giới hạn 1920 pixels)

**Tham số trong `additionalParams` (object, optional):**
Các tham số đặc biệt cho từng loại solution, được truyền dưới dạng key-value:

- `RTSP_URL` (string): URL của RTSP stream (bắt buộc cho `face_detection`, `object_detection`)
  - Ví dụ: `"rtsp://192.168.1.100:8554/stream"`

- `MODEL_NAME` (string): Tên model để sử dụng (ưu tiên hơn MODEL_PATH)
  - Ví dụ: `"yunet_2023mar"`, `"face:yunet_2023mar"` (có thể chỉ định category:modelname)
  - Hệ thống sẽ tự động tìm file model trong các thư mục hệ thống

- `MODEL_PATH` (string): Đường dẫn trực tiếp đến file model
  - Ví dụ: `"./models/yunet.onnx"`, `"/usr/share/cvedix/cvedix_data/models/face/yunet.onnx"`
  - Chỉ được dùng nếu MODEL_NAME không được cung cấp hoặc không tìm thấy

- `FILE_PATH` (string): Đường dẫn file video (cho solution `face_detection_file`)
  - Ví dụ: `"/path/to/video.mp4"`

- `RTMP_URL` (string): URL RTMP để streaming output (cho solution `face_detection_rtmp`)
  - Ví dụ: `"rtmp://localhost:1935/live/stream"`

- `CrossingLines` (string, JSON): Cấu hình crossing lines cho solution `ba_crossline` (dạng JSON string)
  - Format: Mảng JSON string chứa các line objects
  - Ví dụ: `"[{\"id\":\"uuid\",\"name\":\"Line Name\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Both\",\"classes\":[\"Vehicle\"],\"color\":[255,0,0,255]}]"`
  - Có thể quản lý qua API `/v1/core/instance/{instanceId}/lines`

### Lấy Thông Tin Instance
```bash
GET /v1/core/instance/{instanceId}
```

### Cập Nhật Instance
```bash
PUT /v1/core/instance/{instanceId}
Content-Type: application/json

{
  "name": "Camera 1 Updated",
  "additionalParams": {
    "RTSP_URL": "rtsp://192.168.1.101:8554/stream"
  }
}
```

**Tham số Request Body (UpdateInstanceRequest):**

**Path Parameters:**
- `instanceId` (string, required): ID của instance cần cập nhật (UUID)

**Request Body - Tất cả các tham số đều optional**, chỉ các field được cung cấp sẽ được cập nhật:

- `name` (string, optional): Tên mới cho instance. Pattern: `^[A-Za-z0-9 -_]+$`

- `group` (string, optional): Tên group mới. Pattern: `^[A-Za-z0-9 -_]+$`

- `persistent` (boolean, optional): Cập nhật chế độ persistent

- `frameRateLimit` (integer, optional): Cập nhật giới hạn frame rate (minimum: 0)

- `metadataMode` (boolean, optional): Bật/tắt metadata mode

- `statisticsMode` (boolean, optional): Bật/tắt statistics mode

- `diagnosticsMode` (boolean, optional): Bật/tắt diagnostics mode

- `debugMode` (boolean, optional): Bật/tắt debug mode

- `detectorMode` (string, optional): Cập nhật detector mode (enum: `"SmartDetection"`)

- `detectionSensitivity` (string, optional): Cập nhật detection sensitivity (enum: `"Low"`, `"Medium"`, `"High"`)

- `movementSensitivity` (string, optional): Cập nhật movement sensitivity (enum: `"Low"`, `"Medium"`, `"High"`)

- `sensorModality` (string, optional): Cập nhật sensor modality (enum: `"RGB"`, `"Thermal"`)

- `autoStart` (boolean, optional): Cập nhật auto start

- `autoRestart` (boolean, optional): Cập nhật auto restart

- `inputOrientation` (integer, optional): Cập nhật input orientation (0-3)

- `inputPixelLimit` (integer, optional): Cập nhật input pixel limit (minimum: 0)

**Tham số trong `additionalParams` (object, optional):**
Chỉ các key được cung cấp sẽ được cập nhật (merge với existing params):

- `RTSP_URL` (string): Cập nhật RTSP URL

- `MODEL_NAME` (string): Cập nhật model name

- `MODEL_PATH` (string): Cập nhật model path

- `FILE_PATH` (string): Cập nhật file path (cho file source)

- `RTMP_URL` (string): Cập nhật RTMP URL (cho streaming destination)

**Lưu ý:** Không thể thay đổi `solution` sau khi instance đã được tạo. Để sử dụng solution khác, cần xóa và tạo instance mới.

### Xóa Instance
```bash
DELETE /v1/core/instance/{instanceId}
```

### Khởi Động Instance
```bash
POST /v1/core/instance/{instanceId}/start
```

### Dừng Instance
```bash
POST /v1/core/instance/{instanceId}/stop
```

### Khởi Động Lại Instance
```bash
POST /v1/core/instance/{instanceId}/restart
```

### Batch Operations
Khởi động/dừng/khởi động lại nhiều instances cùng lúc.

```bash
POST /v1/core/instance/batch/start
POST /v1/core/instance/batch/stop
POST /v1/core/instance/batch/restart

Content-Type: application/json
{
  "instanceIds": ["id1", "id2", "id3"]
}
```

**Tham số Request Body:**
- `instanceIds` (array of strings, required): Mảng các instance IDs cần thực hiện batch operation
  - Tối thiểu 1 ID, có thể nhiều IDs
  - Ví dụ: `["550e8400-e29b-41d4-a716-446655440000", "660e8400-e29b-41d4-a716-446655440001"]`

### Lấy Thống Kê Instance
Lấy thông tin FPS, latency, số lượng object được phát hiện, ...

```bash
GET /v1/core/instance/{instanceId}/statistics
```

### Lấy Frame Cuối Cùng
Lấy frame cuối cùng đã được xử lý (dạng base64 JPEG).

```bash
GET /v1/core/instance/{instanceId}/frame
```

### Lấy Preview Frame
Lấy preview frame với các annotation (bounding boxes, labels).

```bash
GET /v1/core/instance/{instanceId}/preview
```

### Lấy Output
Lấy output từ instance (FILE/RTMP/RTSP stream).

```bash
GET /v1/core/instance/{instanceId}/output
```

### Cấu Hình Input Source
Thay đổi nguồn input (RTSP URL, file path, ...).

```bash
POST /v1/core/instance/{instanceId}/input
Content-Type: application/json

{
  "source": "rtsp://192.168.1.100:8554/new_stream"
}
```

**Tham số Request Body:**
- `source` (string, required): Nguồn input mới
  - Có thể là RTSP URL: `"rtsp://192.168.1.100:8554/stream"`
  - Hoặc file path: `"/path/to/video.mp4"`
  - Hoặc các nguồn khác tùy theo solution type

---

## 3. Solutions API - Quản Lý Solution Templates

Solution là template định nghĩa pipeline xử lý AI (các nodes và cách kết nối).

### Liệt Kê Tất Cả Solutions
```bash
GET /v1/core/solution
```

### Lấy Thông Tin Solution
```bash
GET /v1/core/solution/{solutionId}
```

### Tạo Solution Mới
Tạo custom solution với pipeline tùy chỉnh.

```bash
POST /v1/core/solution
Content-Type: application/json

{
  "solutionId": "my_custom_solution",
  "solutionName": "My Custom Solution",
  "solutionType": "face_detection",
  "pipeline": [
    {
      "nodeType": "rtsp_src",
      "nodeName": "source_{instanceId}",
      "parameters": {
        "url": "rtsp://localhost/stream"
      }
    },
    {
      "nodeType": "yunet_face_detector",
      "nodeName": "detector_{instanceId}",
      "parameters": {
        "model_path": "models/face/yunet.onnx"
      }
    }
  ]
}
```

**Tham số Request Body:**

**Tham số bắt buộc:**
- `solutionId` (string, required): ID duy nhất của solution (dùng để reference khi tạo instance)
  - Pattern: `^[A-Za-z0-9_-]+$`
  - Ví dụ: `"my_custom_solution"`, `"face_detection_v2"`

- `pipeline` (array, required): Mảng các nodes trong pipeline, định nghĩa luồng xử lý
  - Mỗi node là một object với các tham số:
    - `nodeType` (string, required): Loại node (ví dụ: `"rtsp_src"`, `"yunet_face_detector"`, `"file_src"`)
    - `nodeName` (string, required): Tên node (có thể dùng `{instanceId}` placeholder)
    - `parameters` (object, required): Tham số cấu hình cho node
      - Tùy theo nodeType, các parameters sẽ khác nhau
      - Ví dụ: RTSP source cần `url`, detector cần `model_path`

**Tham số tùy chọn:**
- `solutionName` (string, optional): Tên hiển thị của solution
  - Ví dụ: `"My Custom Solution"`, `"Face Detection v2"`

- `solutionType` (string, optional): Loại solution (để phân loại)
  - Ví dụ: `"face_detection"`, `"object_detection"`, `"custom"`

### Cập Nhật Solution
```bash
PUT /v1/core/solution/{solutionId}
```

### Xóa Solution
```bash
DELETE /v1/core/solution/{solutionId}
```

**Lưu ý:** Không thể xóa default solutions, chỉ xóa được custom solutions.

---

## 4. Groups API - Quản Lý Nhóm Instances

Groups giúp tổ chức và quản lý nhiều instances cùng lúc.

### Liệt Kê Tất Cả Groups
```bash
GET /v1/core/groups
```

### Tạo Group Mới
```bash
POST /v1/core/groups
Content-Type: application/json

{
  "groupId": "camera_group_1",
  "groupName": "Camera Group 1",
  "description": "Nhóm camera tầng 1"
}
```

**Tham số Request Body:**

**Tham số bắt buộc:**
- `groupId` (string, required): ID duy nhất của group
  - Pattern: `^[A-Za-z0-9_-]+$`
  - Ví dụ: `"camera_group_1"`, `"floor_1_cameras"`

- `groupName` (string, required): Tên hiển thị của group
  - Ví dụ: `"Camera Group 1"`, `"Tầng 1 - Cameras"`

**Tham số tùy chọn:**
- `description` (string, optional): Mô tả về group
  - Ví dụ: `"Nhóm camera tầng 1"`, `"All cameras on floor 1"`

### Lấy Thông Tin Group
```bash
GET /v1/core/groups/{groupId}
```

### Lấy Instances Trong Group
```bash
GET /v1/core/groups/{groupId}/instances
```

### Cập Nhật Group
```bash
PUT /v1/core/groups/{groupId}
```

### Xóa Group
```bash
DELETE /v1/core/groups/{groupId}
```

---

## 5. Lines API - Quản Lý Crossing Lines

Lines dùng cho solution `ba_crossline` để đếm số lượng object vượt qua đường thẳng.

### Liệt Kê Tất Cả Lines
```bash
GET /v1/core/instance/{instanceId}/lines
```

### Tạo Line Mới
```bash
POST /v1/core/instance/{instanceId}/lines
Content-Type: application/json

{
  "name": "Counting Line 1",
  "coordinates": [
    {"x": 100, "y": 300},
    {"x": 600, "y": 300}
  ],
  "direction": "Both",
  "classes": ["Person", "Vehicle"],
  "color": [255, 0, 0, 255]
}
```

**Tham số Path:**
- `instanceId` (string, required): ID của instance (phải là instance dùng solution `ba_crossline`)

**Tham số Request Body (CreateLineRequest):**
API hỗ trợ cả single object và array of objects (để tạo nhiều lines cùng lúc).

**Tham số bắt buộc:**
- `coordinates` (array, required): Mảng các điểm tọa độ định nghĩa đường thẳng (tối thiểu 2 điểm)
  - Mỗi điểm là object với `x` (float) và `y` (float)
  - Ví dụ: `[{"x": 100.0, "y": 300.0}, {"x": 600.0, "y": 300.0}]`

**Tham số tùy chọn:**
- `name` (string, optional): Tên mô tả cho line
  - Ví dụ: `"Counting Line 1"`, `"Entry Line"`, `"Exit Line"`
  - Nếu không cung cấp, hệ thống sẽ tự động tạo tên

- `direction` (string, optional, default: `"Both"`): Hướng đếm object vượt qua line
  - Giá trị: `"Up"` (chỉ đếm object đi lên), `"Down"` (chỉ đếm object đi xuống), `"Both"` (đếm cả hai hướng)

- `classes` (array of strings, optional, default: `[]`): Các loại object cần đếm
  - Giá trị có thể: `"Person"`, `"Animal"`, `"Vehicle"`, `"Face"`, `"Unknown"`
  - Nếu mảng rỗng, sẽ đếm tất cả các loại object
  - Ví dụ: `["Person", "Vehicle"]` - chỉ đếm Person và Vehicle

- `color` (array of integers, optional, default: `[255, 0, 0, 255]`): Màu sắc của line (RGBA format)
  - Mảng 4 số nguyên từ 0-255: `[R, G, B, A]`
  - Mặc định là màu đỏ: `[255, 0, 0, 255]`
  - Ví dụ: `[0, 255, 0, 255]` - màu xanh lá, `[255, 0, 0, 255]` - màu đỏ

**Ví dụ tạo nhiều lines cùng lúc (array):**
```json
[
  {
    "name": "Entry Line",
    "coordinates": [{"x": 0, "y": 250}, {"x": 700, "y": 220}],
    "direction": "Up",
    "classes": ["Vehicle"],
    "color": [255, 0, 0, 255]
  },
  {
    "name": "Exit Line",
    "coordinates": [{"x": 100, "y": 400}, {"x": 800, "y": 380}],
    "direction": "Down",
    "classes": ["Vehicle"],
    "color": [0, 255, 0, 255]
  }
]
```

### Cập Nhật Line
```bash
PUT /v1/core/instance/{instanceId}/lines/{lineId}
```

### Xóa Line
```bash
DELETE /v1/core/instance/{instanceId}/lines/{lineId}
```

### Xóa Tất Cả Lines
```bash
DELETE /v1/core/instance/{instanceId}/lines
```

---

## 6. Recognition API - Nhận Diện Khuôn Mặt

### Nhận Diện Khuôn Mặt Từ Ảnh
Upload ảnh và nhận diện khuôn mặt trong ảnh đó.

```bash
POST /v1/recognition/recognize
Content-Type: multipart/form-data

file: [ảnh file]
limit: 0
prediction_count: 1
det_prob_threshold: 0.5
```

**Ví dụ với curl:**
```bash
curl -X POST http://localhost:8080/v1/recognition/recognize \
  -F "file=@image.jpg" \
  -F "limit=5" \
  -F "prediction_count=1" \
  -F "det_prob_threshold=0.5"
```

**Tham số Request (multipart/form-data):**

**Tham số bắt buộc:**
- `file` (file, required): File ảnh chứa khuôn mặt cần nhận diện
  - Định dạng hỗ trợ: JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP
  - Kích thước tối đa: 5MB
  - Có thể upload file trực tiếp hoặc gửi base64-encoded image (với Content-Type: application/json)

**Tham số Query (optional):**
- `limit` (integer, optional, default: `0`): Số lượng khuôn mặt tối đa để nhận diện
  - `0`: Không giới hạn, nhận diện tất cả khuôn mặt trong ảnh
  - Giá trị dương: Nhận diện tối đa N khuôn mặt (ưu tiên các khuôn mặt lớn nhất trước)
  - Ví dụ: `5` - chỉ nhận diện 5 khuôn mặt lớn nhất

- `prediction_count` (integer, optional, default: `1`): Số lượng predictions (kết quả nhận diện) trả về cho mỗi khuôn mặt
  - `1`: Trả về 1 kết quả nhận diện tốt nhất cho mỗi khuôn mặt
  - Giá trị lớn hơn: Trả về N kết quả nhận diện (sắp xếp theo độ tương đồng giảm dần)
  - Ví dụ: `3` - trả về 3 khuôn mặt có độ tương đồng cao nhất cho mỗi khuôn mặt được phát hiện

- `det_prob_threshold` (float, optional, default: `0.5`): Ngưỡng xác suất phát hiện khuôn mặt (0.0 - 1.0)
  - Chỉ các khuôn mặt có độ tin cậy phát hiện >= threshold mới được xử lý
  - `0.0`: Phát hiện tất cả khuôn mặt (kể cả không chắc chắn)
  - `1.0`: Chỉ phát hiện khuôn mặt rất chắc chắn
  - Khuyến nghị: `0.5` - cân bằng giữa độ chính xác và số lượng phát hiện

- `face_plugins` (string, optional): Các face plugins cần enable (comma-separated)
  - Ví dụ: `"age,gender,landmarks"`

- `status` (string, optional): Filter theo status

- `detect_faces` (boolean, optional, default: `true`): Có phát hiện khuôn mặt hay không
  - `true`: Phát hiện khuôn mặt mới và nhận diện
  - `false`: Chỉ nhận diện khuôn mặt đã được phát hiện sẵn (không detect)

### Đăng Ký Khuôn Mặt Mới
Lưu khuôn mặt vào database để training.

```bash
POST /v1/recognition/faces
Content-Type: multipart/form-data

file: [ảnh file]
subject: "ten_nguoi"
```

**Tham số Request (multipart/form-data):**

**Tham số bắt buộc:**
- `file` (file, required): File ảnh chứa khuôn mặt cần đăng ký
  - Định dạng hỗ trợ: JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP
  - Kích thước tối đa: 5MB
  - Ảnh phải chứa ít nhất 1 khuôn mặt rõ ràng

- `subject` (string, required): Tên của người/chủ thể (subject name)
  - Dùng để nhóm các khuôn mặt cùng một người
  - Ví dụ: `"John Doe"`, `"Nguyen Van A"`, `"employee_001"`
  - Có thể đăng ký nhiều ảnh cho cùng một subject để cải thiện độ chính xác nhận diện

### Liệt Kê Tất Cả Subjects
```bash
GET /v1/recognition/faces?page=0&size=20&subject=
```

**Tham số Query:**
- `page` (integer, optional, default: `0`): Số trang (bắt đầu từ 0)
  - Dùng cho phân trang kết quả
  - Ví dụ: `0` - trang đầu tiên, `1` - trang thứ hai

- `size` (integer, optional, default: `20`): Số lượng kết quả mỗi trang
  - Ví dụ: `20` - hiển thị 20 subjects mỗi trang, `50` - hiển thị 50 subjects

- `subject` (string, optional): Lọc theo tên subject (tìm kiếm/từ khóa)
  - Nếu để trống: trả về tất cả subjects
  - Nếu có giá trị: lọc subjects có tên chứa chuỗi này (case-sensitive)
  - Ví dụ: `?subject=John` - tìm các subjects có tên chứa "John"

### Xóa Subject
```bash
DELETE /v1/recognition/faces/{imageId}
```

hoặc xóa theo tên subject:

```bash
DELETE /v1/recognition/faces?subject=ten_nguoi
```

### Xóa Nhiều Subjects
```bash
POST /v1/recognition/faces/delete
Content-Type: application/json

{
  "imageIds": ["id1", "id2"],
  "subjects": ["subject1", "subject2"]
}
```

**Tham số Request Body:**
- `imageIds` (array of strings, optional): Mảng các image IDs cần xóa
  - Mỗi image ID là UUID của một face registration
  - Ví dụ: `["6b135f5b-a365-4522-b1f1-4c9ac2dd0728", "7c246g6c-b476-5633-c2g2-5d9bd3ee1839"]`

- `subjects` (array of strings, optional): Mảng các subject names cần xóa
  - Xóa tất cả faces thuộc về các subjects này
  - Ví dụ: `["John Doe", "Jane Smith"]` - xóa tất cả faces của "John Doe" và "Jane Smith"

**Lưu ý:** Có thể chỉ cung cấp `imageIds`, chỉ cung cấp `subjects`, hoặc cả hai. Ít nhất một trong hai phải được cung cấp.

### Xóa Tất Cả Subjects
```bash
DELETE /v1/recognition/faces/all
```

### Tìm Kiếm Khuôn Mặt
Tìm khuôn mặt tương tự trong database.

```bash
POST /v1/recognition/search
Content-Type: multipart/form-data

file: [ảnh file]
limit: 5
threshold: 0.5
det_prob_threshold: 0.5
```

**Tham số Request (multipart/form-data):**

**Tham số bắt buộc:**
- `file` (file, required): File ảnh chứa khuôn mặt cần tìm kiếm
  - Định dạng hỗ trợ: JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP
  - Kích thước tối đa: 5MB

**Tham số Query (optional):**
- `threshold` (float, optional, default: `0.5`): Ngưỡng độ tương đồng tối thiểu (0.0 - 1.0)
  - Chỉ trả về các khuôn mặt có similarity score >= threshold
  - `0.0`: Trả về tất cả kết quả (kể cả không tương đồng)
  - `1.0`: Chỉ trả về kết quả rất tương đồng
  - Khuyến nghị: `0.5` - `0.7` cho độ chính xác tốt
  - Kết quả được sắp xếp theo similarity giảm dần

- `limit` (integer, optional, default: `0`): Số lượng kết quả tối đa trả về
  - `0`: Không giới hạn, trả về tất cả kết quả có similarity >= threshold
  - Giá trị dương: Trả về tối đa N kết quả tốt nhất
  - Ví dụ: `5` - trả về 5 khuôn mặt tương đồng nhất

- `det_prob_threshold` (float, optional, default: `0.5`): Ngưỡng xác suất phát hiện khuôn mặt (0.0 - 1.0)
  - Tương tự như trong API `/recognize`
  - Chỉ các khuôn mặt có độ tin cậy phát hiện >= threshold mới được xử lý

### Cấu Hình Database
Cấu hình kết nối MySQL/PostgreSQL cho face database.

```bash
POST /v1/recognition/face-database/connection
Content-Type: application/json

{
  "type": "mysql",
  "host": "localhost",
  "port": 3306,
  "database": "face_db",
  "username": "user",
  "password": "pass",
  "charset": "utf8mb4"
}
```

**Tham số Request Body:**

**Tham số bắt buộc:**
- `type` (string, required): Loại database
  - Giá trị: `"mysql"` hoặc `"postgresql"`

- `host` (string, required): Địa chỉ host của database server
  - Ví dụ: `"localhost"`, `"192.168.1.100"`, `"db.example.com"`

- `port` (integer, required): Cổng kết nối database
  - MySQL thường dùng: `3306`
  - PostgreSQL thường dùng: `5432`

- `database` (string, required): Tên database
  - Ví dụ: `"face_db"`, `"recognition_db"`

- `username` (string, required): Tên user để kết nối database

- `password` (string, required): Mật khẩu để kết nối database

**Tham số tùy chọn:**
- `charset` (string, optional, default: `"utf8mb4"`): Character set cho kết nối
  - MySQL: `"utf8mb4"` (khuyến nghị để hỗ trợ emoji và ký tự đặc biệt)
  - PostgreSQL: Thường không cần, nhưng có thể dùng `"UTF8"`

---

## 7. Models API - Quản Lý Model Files

### Upload Model File
```bash
POST /v1/core/models
Content-Type: multipart/form-data

file: [model file]
```

### Liệt Kê Model Files
```bash
GET /v1/core/models
```

### Đổi Tên Model File
```bash
PUT /v1/core/models/{fileName}?newName=new_name.onnx
```

**Tham số Path:**
- `fileName` (string, required): Tên file model hiện tại cần đổi tên
  - Ví dụ: `"old_model.onnx"`, `"face_detector_v1.onnx"`

**Tham số Query:**
- `newName` (string, required): Tên mới cho file
  - Ví dụ: `"new_model.onnx"`, `"face_detector_v2.onnx"`
  - Phải là tên file hợp lệ (bao gồm extension)

### Xóa Model File
```bash
DELETE /v1/core/models/{fileName}
```

---

## 8. Video API - Quản Lý Video Files

### Upload Video File
```bash
POST /v1/core/video
Content-Type: multipart/form-data

file: [video file]
```

### Liệt Kê Video Files
```bash
GET /v1/core/video
```

### Đổi Tên Video File
```bash
PUT /v1/core/video/{fileName}?newName=new_name.mp4
```

**Tham số Path:**
- `fileName` (string, required): Tên file video hiện tại cần đổi tên
  - Ví dụ: `"old_video.mp4"`, `"sample_video.mkv"`

**Tham số Query:**
- `newName` (string, required): Tên mới cho file
  - Ví dụ: `"new_video.mp4"`, `"processed_video.mkv"`
  - Phải là tên file hợp lệ (bao gồm extension)

### Xóa Video File
```bash
DELETE /v1/core/video/{fileName}
```

---

## 9. Fonts API - Quản Lý Font Files

### Upload Font File
```bash
POST /v1/core/fonts
Content-Type: multipart/form-data

file: [font file]
```

### Liệt Kê Font Files
```bash
GET /v1/core/fonts
```

### Đổi Tên Font File
```bash
PUT /v1/core/fonts/{fileName}?newName=new_font.ttf
```

**Tham số Path:**
- `fileName` (string, required): Tên file font hiện tại cần đổi tên
  - Ví dụ: `"arial.ttf"`, `"old_font.otf"`

**Tham số Query:**
- `newName` (string, required): Tên mới cho file
  - Ví dụ: `"new_font.ttf"`, `"custom_font.otf"`
  - Phải là tên file hợp lệ (bao gồm extension)

---

## 10. Node API - Quản Lý Node Pool

Node Pool chứa các nodes đã được cấu hình sẵn để tái sử dụng.

### Liệt Kê Nodes
```bash
GET /v1/core/node?available=true&category=source
```

**Query parameters:**
- `available` (boolean, optional): Lọc nodes theo trạng thái available
  - `true`: Chỉ lấy nodes đang available (có thể sử dụng)
  - `false`: Chỉ lấy nodes không available
  - Không có: Lấy tất cả nodes

- `category` (string, optional): Lọc nodes theo category
  - Giá trị: `"source"`, `"detector"`, `"processor"`, `"destination"`, `"broker"`
  - `source`: Nodes nguồn input (RTSP, file, camera, ...)
  - `detector`: Nodes phát hiện (face detector, object detector, ...)
  - `processor`: Nodes xử lý (filter, transformer, ...)
  - `destination`: Nodes đích (file output, stream output, ...)
  - `broker`: Nodes trung gian (message broker, queue, ...)

### Tạo Node Mới
```bash
POST /v1/core/node
Content-Type: application/json

{
  "templateId": "rtsp_src_template",
  "displayName": "My RTSP Source",
  "parameters": {
    "rtsp_url": "rtsp://192.168.1.100:8554/stream"
  }
}
```

**Tham số Request Body:**

**Tham số bắt buộc:**
- `templateId` (string, required): ID của node template (phải tồn tại trong hệ thống)
  - Lấy danh sách templates từ `GET /v1/core/node/template`
  - Ví dụ: `"rtsp_src_template"`, `"yunet_face_detector_template"`

- `parameters` (object, required): Tham số cấu hình cho node
  - Tùy theo template, các parameters sẽ khác nhau
  - Ví dụ:
    - RTSP source: `{"rtsp_url": "rtsp://..."}`
    - Face detector: `{"model_path": "./models/yunet.onnx"}`

**Tham số tùy chọn:**
- `displayName` (string, optional): Tên hiển thị cho node
  - Ví dụ: `"My RTSP Source"`, `"Camera 1 Input"`
  - Nếu không cung cấp, hệ thống sẽ dùng tên từ template

### Lấy Thông Tin Node
```bash
GET /v1/core/node/{nodeId}
```

### Cập Nhật Node
```bash
PUT /v1/core/node/{nodeId}
```

### Xóa Node
```bash
DELETE /v1/core/node/{nodeId}
```

### Liệt Kê Node Templates
Lấy danh sách các node templates có sẵn.

```bash
GET /v1/core/node/template
```

### Lấy Thông Tin Template
```bash
GET /v1/core/node/template/{templateId}
```

---

## 11. Config API - Quản Lý Cấu Hình Hệ Thống

### Lấy Toàn Bộ Config
```bash
GET /v1/core/config
```

### Lấy Config Section
```bash
GET /v1/core/config/{section}
```

Ví dụ: `GET /v1/core/config/logging`

### Cập Nhật Config (Merge)
Cập nhật một phần config (merge với config hiện tại).

```bash
POST /v1/core/config
Content-Type: application/json

{
  "logging": {
    "level": "DEBUG"
  }
}
```

### Thay Thế Toàn Bộ Config
```bash
PUT /v1/core/config
Content-Type: application/json

{
  // toàn bộ config object
}
```

### Cập Nhật Config Section
```bash
PUT /v1/core/config/{section}
Content-Type: application/json

{
  // section config object
}
```

### Xóa Config Section
```bash
DELETE /v1/core/config/{section}
```

### Reset Config Về Mặc Định
```bash
POST /v1/core/config/reset
```

---

## 12. Logs API - Xem Logs

### Liệt Kê Tất Cả Log Files
```bash
GET /v1/core/log
```

### Lấy Logs Theo Category
```bash
GET /v1/core/log/{category}?level=ERROR&tail=100
```

**Tham số Path:**
- `category` (string, required): Category của log
  - Giá trị: `"api"` (API logs), `"instance"` (Instance logs), `"sdk_output"` (SDK output logs), `"general"` (General logs)

**Query parameters:**
- `level` (string, optional): Lọc theo log level
  - Giá trị: `"INFO"`, `"ERROR"`, `"WARNING"`, `"DEBUG"`, `"TRACE"`
  - Chỉ trả về logs có level >= level chỉ định
  - Ví dụ: `level=ERROR` chỉ trả về ERROR và FATAL logs

- `from` (string, optional): Thời gian bắt đầu (ISO 8601 format)
  - Format: `YYYY-MM-DDTHH:mm:ss` hoặc `YYYY-MM-DDTHH:mm:ssZ`
  - Ví dụ: `"2024-01-01T00:00:00"`, `"2024-01-01T00:00:00Z"`
  - Chỉ lấy logs từ thời điểm này trở đi

- `to` (string, optional): Thời gian kết thúc (ISO 8601 format)
  - Format: `YYYY-MM-DDTHH:mm:ss` hoặc `YYYY-MM-DDTHH:mm:ssZ`
  - Ví dụ: `"2024-01-01T23:59:59"`, `"2024-01-01T23:59:59Z"`
  - Chỉ lấy logs đến thời điểm này

- `tail` (integer, optional): Số dòng cuối cùng cần lấy
  - Ví dụ: `100` - lấy 100 dòng log cuối cùng
  - Hữu ích khi chỉ cần xem logs gần đây nhất
  - Nếu không chỉ định, trả về tất cả logs (có thể rất nhiều)

### Lấy Logs Theo Ngày
```bash
GET /v1/core/log/{category}/{date}
```

Ví dụ: `GET /v1/core/log/api/2024-01-01`

---

## 13. AI API - Xử Lý Ảnh/Frame Đơn

### Xử Lý Ảnh/Frame Đơn
Gửi một ảnh để xử lý qua AI pipeline.

```bash
POST /v1/core/ai/process
Content-Type: application/json

{
  "image": "base64_encoded_image_string",
  "config": "{\"model\": \"yolo\", \"threshold\": 0.5}",
  "priority": "medium"
}
```

**Tham số Request Body:**

**Tham số bắt buộc:**
- `image` (string, required): Ảnh được encode dạng base64
  - Format: Base64-encoded image data (không có data URI prefix như `data:image/jpeg;base64,`)
  - Ví dụ: `"iVBORw0KGgoAAAANSUhEUgAA..."`

- `config` (string, required): Cấu hình xử lý dạng JSON string
  - Phải là JSON string hợp lệ
  - Ví dụ: `"{\"model\": \"yolo\", \"threshold\": 0.5}"`
  - Các key có thể có:
    - `model`: Tên model để sử dụng (ví dụ: `"yolo"`, `"yunet"`)
    - `threshold`: Ngưỡng confidence (0.0 - 1.0)

**Tham số tùy chọn:**
- `priority` (string, optional): Độ ưu tiên xử lý
  - Giá trị: `"low"`, `"medium"`, `"high"`
  - Default: `"medium"`
  - Requests với priority cao sẽ được xử lý trước

### Lấy Trạng Thái AI Processing
```bash
GET /v1/core/ai/status
```

### Lấy Metrics AI Processing
```bash
GET /v1/core/ai/metrics
```

---

## Các Solution Types Phổ Biến

- `face_detection`: Nhận diện khuôn mặt từ RTSP stream
- `face_detection_file`: Nhận diện khuôn mặt từ video file
- `object_detection`: Phát hiện vật thể (YOLO)
- `ba_crossline`: Đếm số lượng object vượt qua đường thẳng
- `face_detection_rtmp`: Nhận diện khuôn mặt với RTMP streaming

---

## Lưu Ý Quan Trọng

1. **Instance ID**: Khi tạo instance mới, server sẽ trả về `instanceId` (UUID). Dùng ID này để điều khiển instance.

2. **Persistent Mode**: Nếu set `persistent: true`, instance sẽ được lưu và tự động load khi server restart.

3. **Auto Start**: Nếu set `autoStart: true`, instance sẽ tự động khởi động sau khi tạo.

4. **RTSP_URL**: Hầu hết solutions cần `RTSP_URL` trong `additionalParams` để chỉ định nguồn video stream.

5. **Swagger UI**: Truy cập `http://localhost:8080/swagger` để xem tất cả endpoints và test trực tiếp.

6. **Error Handling**: API trả về HTTP status codes:
   - `200`: Thành công
   - `400`: Bad request (thiếu tham số, format sai)
   - `404`: Không tìm thấy resource
   - `500`: Server error
   - `503`: Service unavailable

---

## Ví Dụ Workflow Hoàn Chỉnh

### 1. Tạo Instance Nhận Diện Khuôn Mặt
```bash
curl -X POST http://localhost:8080/v1/core/instance \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Camera 1",
    "solution": "face_detection",
    "autoStart": true,
    "persistent": true,
    "additionalParams": {
      "RTSP_URL": "rtsp://192.168.1.100:8554/stream"
    }
  }'
```

### 2. Kiểm Tra Trạng Thái Instance
```bash
curl http://localhost:8080/v1/core/instance/{instanceId}
```

### 3. Xem Thống Kê
```bash
curl http://localhost:8080/v1/core/instance/{instanceId}/statistics
```

### 4. Lấy Frame Preview
```bash
curl http://localhost:8080/v1/core/instance/{instanceId}/preview
```

### 5. Dừng Instance
```bash
curl -X POST http://localhost:8080/v1/core/instance/{instanceId}/stop
```

---

## Tài Liệu Tham Khảo

- **API Document đầy đủ**: `docs/API_document.md`
- **Architecture**: `docs/ARCHITECTURE.md`
- **Recognition API Guide**: `docs/RECOGNITION_API_GUIDE.md`
- **Swagger UI**: http://localhost:8080/swagger
- **OpenAPI Spec**: http://localhost:8080/openapi.yaml
