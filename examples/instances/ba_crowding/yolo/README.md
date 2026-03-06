# BA Crowding (YOLO)

Solution **ba_crowding**: phát hiện tụ tập (số object trong ROI vượt ngưỡng và duy trì trong khoảng thời gian cấu hình).

## Pipeline

- file_src → yolo_detector → sort_track → ba_crowding → ba_crowding_osd → file_des

## Ví dụ

- `example_ba_crowding_file.json` – Nguồn file video, vùng crowding (CrowdingZones) dạng polygon, ngưỡng và alarm_seconds.

## Tham số (additionalParams)

- **input**: `FILE_PATH`, `WEIGHTS_PATH`, `CONFIG_PATH`, `LABELS_PATH`, `RESIZE_RATIO`.
- **CrowdingZones**: JSON array. Mỗi phần tử: `channel`, `coordinates` (array `{x, y}`), `threshold` (số object), `alarm_seconds`, `name`.
- **CROWDING_CHECK_INTERVAL**: (tùy chọn) Chu kỳ kiểm tra (mặc định 30).

## Tạo instance qua API

Dùng solution `ba_crowding` với body tham chiếu từ file example trên.
