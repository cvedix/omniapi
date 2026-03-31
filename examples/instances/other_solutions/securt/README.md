# SecuRT Solution

Solution SecuRT - Security Real-Time với full configuration.

## Cấu trúc thư mục

Các ví dụ được tổ chức theo loại input/output:

```
securt/
├── rtsp_mqtt/          # Input RTSP, Output MQTT
├── rtmp_rtmp/          # Input RTMP, Output RTMP (demo streaming)
├── legacy/             # Các ví dụ cũ (legacy)
└── README.md
```

## Ví dụ

### RTSP Input + MQTT Output (`rtsp_mqtt/`)

**Lines (3 bài toán):**
- `flexible_securt_rtsp_mqtt_crossing.json` - Crossing line detection (vượt đường)
- `flexible_securt_rtsp_mqtt_counting.json` - Counting line detection (đếm đối tượng)
- `flexible_securt_rtsp_mqtt_tailgating.json` - Tailgating line detection (bám đuôi)

**Areas (14 bài toán):**
- `flexible_securt_rtsp_mqtt_crossingarea.json` - Crossing area detection (vượt vùng)
- `flexible_securt_rtsp_mqtt_intrusion.json` - Intrusion area detection (xâm nhập)
- `flexible_securt_rtsp_mqtt_loitering.json` - Loitering area detection (lượn vòng)
- `flexible_securt_rtsp_mqtt_crowding.json` - Crowding area detection (tụ tập)
- `flexible_securt_rtsp_mqtt_occupancy.json` - Occupancy area detection (chiếm dụng)
- `flexible_securt_rtsp_mqtt_crowdestimation.json` - Crowd estimation area detection (ước lượng đám đông)
- `flexible_securt_rtsp_mqtt_dwelling.json` - Dwelling area detection (cư trú lâu)
- `flexible_securt_rtsp_mqtt_armedperson.json` - Armed person area detection (người mang vũ khí)
- `flexible_securt_rtsp_mqtt_objectleft.json` - Object left area detection (vật bỏ quên)
- `flexible_securt_rtsp_mqtt_objectremoved.json` - Object removed area detection (vật bị dời)
- `flexible_securt_rtsp_mqtt_fallenperson.json` - Fallen person area detection (người ngã)
- `flexible_securt_rtsp_mqtt_vehicleguard.json` - Vehicle guard area detection (bảo vệ xe - thử nghiệm)
- `flexible_securt_rtsp_mqtt_facecovered.json` - Face covered area detection (che mặt - thử nghiệm)
- `flexible_securt_rtsp_mqtt_objectenterexit.json` - Object enter/exit area detection (vật vào/ra)
- `flexible_securt_rtsp_mqtt_mixed.json` - Mixed analytics (nhiều bài toán cùng lúc)

### RTMP Input + RTMP Output (`rtmp_rtmp/`)

**Lines (3 bài toán):**
- `flexible_securt_rtmp_rtmp_crossing.json` - Crossing line với RTMP in/out
- `flexible_securt_rtmp_rtmp_counting.json` - Counting line với RTMP in/out
- `flexible_securt_rtmp_rtmp_tailgating.json` - Tailgating line với RTMP in/out

**Areas (14 bài toán):**
- `flexible_securt_rtmp_rtmp_crossingarea.json` - Crossing area với RTMP in/out
- `flexible_securt_rtmp_rtmp_intrusion.json` - Intrusion area với RTMP in/out
- `flexible_securt_rtmp_rtmp_loitering.json` - Loitering area với RTMP in/out
- `flexible_securt_rtmp_rtmp_crowding.json` - Crowding area với RTMP in/out
- `flexible_securt_rtmp_rtmp_occupancy.json` - Occupancy area với RTMP in/out
- `flexible_securt_rtmp_rtmp_crowdestimation.json` - Crowd estimation area với RTMP in/out
- `flexible_securt_rtmp_rtmp_dwelling.json` - Dwelling area với RTMP in/out
- `flexible_securt_rtmp_rtmp_armedperson.json` - Armed person area với RTMP in/out
- `flexible_securt_rtmp_rtmp_objectleft.json` - Object left area với RTMP in/out
- `flexible_securt_rtmp_rtmp_objectremoved.json` - Object removed area với RTMP in/out
- `flexible_securt_rtmp_rtmp_fallenperson.json` - Fallen person area với RTMP in/out
- `flexible_securt_rtmp_rtmp_vehicleguard.json` - Vehicle guard area với RTMP in/out (thử nghiệm)
- `flexible_securt_rtmp_rtmp_facecovered.json` - Face covered area với RTMP in/out (thử nghiệm)
- `flexible_securt_rtmp_rtmp_objectenterexit.json` - Object enter/exit area với RTMP in/out
- `flexible_securt_rtmp_rtmp_mixed.json` - Mixed analytics với RTMP in/out

### Legacy Examples (`legacy/`)

Các ví dụ cũ (không khuyến nghị sử dụng, chỉ để tham khảo):
- `example_full_config.json` - Full configuration example với TensorRT models
- `example_securt_rtmp_areas_lines.json` - RTMP input với areas và lines configuration
- `example_securt_rtmp_no_model_path.json` - RTMP input không cần MODEL_PATH (dùng model mặc định)

## Tổng quan các bài toán SecuRT

SecuRT hỗ trợ **17 bài toán analytics**:

### Lines (3 bài toán):
1. **Crossing Lines** - Phát hiện đối tượng vượt qua đường
2. **Counting Lines** - Đếm số lượng đối tượng qua đường
3. **Tailgating Lines** - Phát hiện bám đuôi (nhiều đối tượng qua cùng lúc)

### Areas (14 bài toán):
1. **Crossing Areas** - Phát hiện đối tượng vượt qua vùng
2. **Intrusion Areas** - Phát hiện xâm nhập vào vùng cấm
3. **Loitering Areas** - Phát hiện lượn vòng trong vùng
4. **Crowding Areas** - Phát hiện tụ tập đông người
5. **Occupancy Areas** - Phát hiện chiếm dụng vùng
6. **Crowd Estimation Areas** - Ước lượng số lượng đám đông
7. **Dwelling Areas** - Phát hiện cư trú lâu trong vùng
8. **Armed Person Areas** - Phát hiện người mang vũ khí
9. **Object Left Areas** - Phát hiện vật bỏ quên
10. **Object Removed Areas** - Phát hiện vật bị dời
11. **Fallen Person Areas** - Phát hiện người ngã
12. **Vehicle Guard Areas** - Bảo vệ xe (thử nghiệm)
13. **Face Covered Areas** - Phát hiện che mặt (thử nghiệm)
14. **Object Enter Exit Areas** - Phát hiện vật vào/ra vùng

## Tham số

Xem trong file example để biết chi tiết các tham số cấu hình.

## Model Configuration

**QUAN TRỌNG**: SecuRT solution **BẮT BUỘC** cần YOLO models để start instance:
- `WEIGHTS_PATH`: `/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights`
- `CONFIG_PATH`: `/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg`
- `LABELS_PATH`: `/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt`

Xem file `SECURT_SOLUTION_ANALYSIS.md` để biết:
- Cách SecuRT solution hoạt động
- Pipeline structure và requirements
- So sánh với ba_crossline solution
- Troubleshooting guide

## Test Guides

- `TEST_GUIDE_RTMP.md` - Hướng dẫn test với RTMP stream
- `MANUAL_TEST_SCENARIO.md` - Kịch bản manual test chi tiết
- `TEST_CHECKLIST.md` - Checklist test đơn giản
- `QUICK_REFERENCE.md` - Tham khảo nhanh các lệnh API

