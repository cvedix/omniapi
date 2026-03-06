# Manual Tests - Edge AI API

Thư mục này chứa các tài liệu hướng dẫn test thủ công cho từng tính năng lớn của Edge AI API.

## Cấu Trúc

```
manual/
├── ONVIF/              # Manual tests cho tính năng ONVIF
│   ├── ONVIF_MANUAL_TEST_GUIDE.md
│   ├── ONVIF_QUICK_TEST.md
│   └── ONVIF_TESTING_GUIDE.md
│
├── Recognition/        # Manual tests cho tính năng Recognition
│   └── RECOGNITION_API_GUIDE.md
│
├── Instance_Management/ # Manual tests cho Instance Management
│
├── Core_API/           # Manual tests cho Core API
│   ├── SYSTEM_CONFIG_MANUAL_TEST.md
│   ├── SYSTEM_CONFIG_QUICK_TEST.md
│   └── EVENTS_OUTPUT_MANUAL_TEST.md
│
├── Solutions/          # Manual tests cho Solutions
│
├── Groups/             # Manual tests cho Groups
│
├── Nodes/              # Manual tests cho Nodes
│
├── Analytics/          # Manual tests cho Analytics (Lines, Jams, Stops, Area, SecuRT, Loitering)
│   ├── SECURT_INSTANCE_WORKFLOW_TEST.md
│   ├── LOITERING_CORE_API_TEST.md
│   └── LOITERING_SECURT_API_TEST.md
│
└── Config/             # Manual tests cho Config
```

## Mục Đích

Manual tests được thiết kế để:
- Hướng dẫn tester thực hiện test thủ công các tính năng
- Cung cấp các test cases chi tiết với expected results
- Hướng dẫn troubleshooting khi gặp vấn đề
- Document các edge cases và scenarios phức tạp

## Cách Sử Dụng

1. Chọn tính năng cần test từ danh sách trên
2. Đọc các file markdown trong thư mục tính năng đó
3. Làm theo hướng dẫn từng bước
4. Ghi lại kết quả và báo cáo nếu có vấn đề

## Tính Năng Hiện Có

### ONVIF
- **ONVIF_MANUAL_TEST_GUIDE.md**: Hướng dẫn test chi tiết với camera Tapo
- **ONVIF_QUICK_TEST.md**: Các lệnh nhanh để test ONVIF
- **ONVIF_TESTING_GUIDE.md**: Hướng dẫn test với camera thật

### Recognition
- **RECOGNITION_API_GUIDE.md**: Hướng dẫn đầy đủ về Recognition API, bao gồm:
  - Đăng ký khuôn mặt
  - Nhận diện khuôn mặt
  - Tìm kiếm khuôn mặt
  - Quản lý subjects
  - Cấu hình database

### Core API - System Config
- **SYSTEM_CONFIG_MANUAL_TEST.md**: Hướng dẫn test chi tiết System Config & Preferences API, bao gồm:
  - Get/Update System Configuration
  - Get System Preferences từ rtconfig.json
  - Get System Decoders (NVIDIA/Intel)
  - Get Registry Key Value
  - System Shutdown
- **SYSTEM_CONFIG_QUICK_TEST.md**: Các lệnh nhanh để test System Config API

### Core API - Events & Output
- **EVENTS_OUTPUT_MANUAL_TEST.md**: Hướng dẫn test chi tiết Events & Output Configuration API, bao gồm:
  - Consume Events từ instance event queue
  - Configure HLS (HTTP Live Streaming) output
  - Configure RTSP (Real-Time Streaming Protocol) output
  - Integration tests với multiple output formats

### Analytics - SecuRT Instance
- **SECURT_INSTANCE_WORKFLOW_TEST.md**: Hướng dẫn test chi tiết SecuRT Instance Workflow, bao gồm:
  - Tạo SecuRT instance
  - Cấu hình input (File, RTSP, RTMP, HLS)
  - Cấu hình output (MQTT, RTMP, RTSP, HLS)
  - Thêm bài toán analytics (Lines, Areas)
  - Start instance và xử lý video
  - End-to-end workflow test

### Analytics - Loitering Detection
- **LOITERING_CORE_API_TEST.md**: Hướng dẫn test chi tiết Loitering Detection qua Core API, bao gồm:
  - Tạo instance với loitering areas qua POST /v1/core/instance
  - Định nghĩa loitering areas trong request body
  - Multiple loitering areas với các threshold khác nhau
  - Workflow hoàn chỉnh từ tạo instance đến xử lý video
  - Troubleshooting và best practices

- **LOITERING_SECURT_API_TEST.md**: Hướng dẫn test chi tiết Loitering Detection qua SecuRT Area API, bao gồm:
  - Tạo SecuRT instance
  - Thêm loitering areas qua POST /v1/securt/instance/{instanceId}/area/loitering
  - Quản lý loitering areas (thêm, xóa, lấy danh sách)
  - Auto-restart khi thêm areas
  - Workflow hoàn chỉnh với SecuRT API
  - Troubleshooting và best practices

## Thêm Manual Test Mới

Khi thêm manual test mới:

1. Xác định tính năng lớn mà test thuộc về
2. Tạo file markdown trong thư mục tính năng tương ứng
3. Bao gồm:
   - Mục đích test
   - Prerequisites
   - Các bước test chi tiết
   - Expected results
   - Troubleshooting guide
   - Test cases examples

## Best Practices

1. **Rõ ràng và chi tiết**: Mỗi bước test nên có hướng dẫn rõ ràng
2. **Có ví dụ**: Cung cấp ví dụ cụ thể với curl commands hoặc code
3. **Expected results**: Luôn mô tả kết quả mong đợi
4. **Troubleshooting**: Bao gồm phần troubleshooting cho các vấn đề thường gặp
5. **Cập nhật**: Giữ tài liệu cập nhật khi API thay đổi

