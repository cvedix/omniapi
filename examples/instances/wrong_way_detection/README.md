# Wrong Way Detection

Solution **wrong_way_detection**: phát hiện xe đi ngược chiều (vehicle detection + SORT tracking).

- Pipeline: file_src → yolo_detector → sort_track → osd_v3 → file_des
- Chi tiết và ví dụ JSON: [yolo/](./yolo/)
