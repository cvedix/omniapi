# Wrong Way Detection (YOLO)

Solution `wrong_way_detection`: phát hiện xe đi ngược chiều (vehicle detection + tracking).

## Pipeline

- file_src → yolo_detector → sort_track → osd_v3 → file_des

## Ví dụ

- `example_wrong_way_detection_file.json` – Nguồn file video, ghi kết quả ra thư mục output.

## Tham số (additionalParams.input)

- `FILE_PATH`: Đường dẫn file video.
- `MODEL_PATH` hoặc `WEIGHTS_PATH`: Đường dẫn model (vehicle detector).
- `CONFIG_PATH`: Đường dẫn config Darknet (khi dùng .weights/.cfg).
- `LABELS_PATH`: Đường dẫn file nhãn lớp.
- `RESIZE_RATIO`: Tỉ lệ resize (mặc định 1.0).

## Tạo instance qua API

Dùng solution `wrong_way_detection` với body tham chiếu từ file example trên.
