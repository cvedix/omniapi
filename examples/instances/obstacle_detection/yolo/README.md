# Obstacle Detection (YOLO)

Solution `obstacle_detection`: phát hiện chướng ngại vật trên đường bằng mô hình YOLO.

## Pipeline

- file_src → yolo_detector → osd_v3 → file_des

## Ví dụ

- `example_obstacle_detection_file.json` – Nguồn file video, ghi kết quả ra thư mục output.

## Tham số (additionalParams.input)

- `FILE_PATH`: Đường dẫn file video.
- `MODEL_PATH`: Đường dẫn model (`.onnx` hoặc `.weights`).
- `CONFIG_PATH`: (Tùy chọn) Đường dẫn config Darknet (khi dùng .weights/.cfg).
- `LABELS_PATH`: Đường dẫn file nhãn lớp (ví dụ obstacles_2classes.txt).
- `RESIZE_RATIO`: Tỉ lệ resize (mặc định 1.0).

## Tạo instance qua API

Dùng solution `obstacle_detection` với body tham chiếu từ file example trên.
