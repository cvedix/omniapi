# RKNN YOLOv11 Detection Solution

Solution phát hiện đối tượng sử dụng YOLOv11 với RKNN optimization cho Rockchip NPU.

## Ví dụ

- `example_rknn_yolov11_detection.json` - YOLOv11 detection với RKNN model

## Tham số

- `MODEL_PATH`: Đường dẫn đến RKNN model file (`.rknn`)
- `SAVE_DIR`: Thư mục lưu output (optional)

## Yêu cầu

- Rockchip NPU hardware
- RKNN toolkit đã được cài đặt
- CVEDIX_WITH_RKNN flag được enable khi build

