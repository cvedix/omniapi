# SecuRT Solution

Solution SecuRT - Security Real-Time với full configuration.

## Ví dụ

- `example_full_config.json` - Full configuration example với TensorRT models
- `example_securt_rtmp_areas_lines.json` - RTMP input với areas và lines configuration
- `example_securt_rtmp_no_model_path.json` - RTMP input không cần MODEL_PATH (dùng model mặc định)

## Tham số

Xem trong file example để biết chi tiết các tham số cấu hình.

## Model Configuration

**QUAN TRỌNG**: SecuRT solution **BẮT BUỘC** cần YOLO models để start instance:
- `WEIGHTS_PATH`: `/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721_best.weights`
- `CONFIG_PATH`: `/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721.cfg`
- `LABELS_PATH`: `/opt/edgeos-api/models/det_cls/yolov3_tiny_5classes.txt`

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

