# Checklist: Sample vs Instance Support

Tài liệu này đối chiếu các sample trong `sample/` với khả năng **tạo và xử lý instance** qua API (solution đăng ký trong project + example instance JSON).

---

## Cách đọc bảng

| Cột | Ý nghĩa |
|-----|--------|
| **Sample** | File `.cpp` trong `sample/` |
| **Solution tương ứng** | Solution ID đăng ký trong `SolutionRegistry` (default hoặc load từ thư mục solutions) |
| **Có example instance** | Có ít nhất một file JSON trong `examples/instances/` dùng đúng solution đó để tạo instance |
| **Ghi chú** | Pipeline đặc thù, custom, hoặc chưa hỗ trợ |

**Chuẩn "đã hỗ trợ"**: Có solution trong project **và** (có example instance dùng solution đó, hoặc có thể tạo instance bằng API với solution tương ứng).

---

## Danh sách Solutions đăng ký mặc định (SolutionRegistry)

Các solution sau được đăng ký trong `src/solutions/solution_registry.cpp` (initializeDefaultSolutions):

- `face_detection`, `face_detection_file_default`, `face_detection_default`, `face_detection_rtmp`, `face_detection_rtmp_default`, `face_detection_rtsp_default`, `rtsp_face_detection_default`
- `object_detection`, `object_detection_yolo_default`
- `yolov11_detection`
- `ba_crossline`, `ba_crossline_default`, `ba_crossline_mqtt_default`
- `ba_stop`, `ba_stop_default`, `ba_stop_mqtt_default`
- `ba_jam`, `ba_jam_default`, `ba_jam_mqtt_default`
- `ba_loitering`, `ba_area_enter_exit`, `ba_line_counting`
- `securt`
- `face_swap`, `insightface_recognition`, `trt_insightface_recognition` (TRT)
- `mllm_analysis`
- `mask_rcnn_detection_default`, `mask_rcnn_rtmp_default`
- `fire_smoke_detection`, `obstacle_detection`, `wrong_way_detection`
- `minimal_default`
- `rknn_yolov11_detection` (RKNN), `trt_insightface_recognition` (TRT)

**Lưu ý**: Example trong `examples/instances/infer_nodes/` dùng một số solution ID **không** có trong danh sách trên (vd: `yolo_detection`, `openpose_detection`, `lane_detection`, `enet_segmentation`, `restoration`, …). Những example đó chỉ chạy được nếu có **custom solution** tương ứng được load từ thư mục solutions.

---

## Checklist theo từng sample

| # | Sample | Solution tương ứng | Có example instance | Ghi chú |
|---|--------|--------------------|----------------------|--------|
| 1 | `1-1-1_sample.cpp` | `face_detection`, `face_detection_rtmp_default` | Có (face_detection/onnx) | 1 src, 1 infer, 1 des – tương đương face detection cơ bản |
| 2 | `1-1-N_sample.cpp` | `face_detection` + nhiều des | Có (face_detection) | Nhiều destination |
| 3 | `1-N-1_sample.cpp` | Phụ thuộc pipeline | Một phần | Nhiều infer, 1 des – cần custom/minimal |
| 4 | `1-N-1_sample2.cpp` | Phụ thuộc pipeline | Một phần | Tương tự 1-N-1 |
| 5 | `1-N-1_sample3.cpp` | Phụ thuộc pipeline | Một phần | Tương tự 1-N-1 |
| 6 | `1-N-N_sample.cpp` | Phụ thuộc pipeline | Một phần | Nhiều infer, nhiều des |
| 7 | `app_des_sample.cpp` | Không (app_des) | Có (custom) | Chỉ hỗ trợ qua custom solution (app_des) |
| 8 | `app_src_des_sample.cpp` | Không (app_src + app_des) | Có (custom: example_app_des, example_app_src) | Custom solution |
| 9 | `ba_area_enter_exit_sample.cpp` | `ba_area_enter_exit` | Có (ba_area_enter_exit) | Đủ solution + example |
| 10 | `ba_crossline_mqtt_sample.cpp` | `ba_crossline`, `ba_crossline_mqtt_default` | Có (ba_crossline/yolo, MQTT) | Đủ solution + example |
| 11 | `ba_crossline_sample.cpp` | `ba_crossline`, `ba_crossline_default` | Có (ba_crossline/yolo) | Đủ solution + example |
| 12 | `ba_crowding_sample.cpp` | Không | Không | Chưa có solution ba_crowding |
| 13 | `ba_jam_sample.cpp` | `ba_jam`, `ba_jam_default` | Có (ba_jam) | Đủ solution + example |
| 14 | `ba_loitering_sample.cpp` | `ba_loitering` | Có (ba_loitering) | Đủ solution + example |
| 15 | `ba_movement_sample.cpp` | Không | Không | Chưa có solution ba_movement |
| 16 | `ba_multiline_crossline_test.cpp` | `ba_crossline` | Có (ba_crossline) | Nhiều line, vẫn dùng ba_crossline |
| 17 | `ba_multiple_crossline_counting_sample.cpp` | `ba_crossline`, `ba_line_counting` | Có (ba_crossline) | Có thể map ba_line_counting |
| 18 | `ba_speed_estimation_sample.cpp` | Không | Không | Chưa có solution ba_speed |
| 19 | `ba_stop_sample.cpp` | `ba_stop`, `ba_stop_default` | Có (ba_stop) | Đủ solution + example |
| 20 | `body_scan_and_plate_detect_sample.cpp` | Custom (nhiều node) | Có (custom: vehicle scanner, plate) | Chỉ qua custom solution |
| 21 | `cvedix_logger_sample.cpp` | Không | Không | Chỉ demo logger, không pipeline |
| 22 | `cvedix_test.cpp` | Không | Không | Test utility |
| 23 | `dynamic_pipeline_sample.cpp` | Không | Không | Pipeline động, không map 1-1 solution |
| 24 | `dynamic_pipeline_sample2.cpp` | Không | Không | Tương tự |
| 25 | `enet_seg_sample.cpp` | Không | Có (infer_nodes: enet_segmentation – custom) | Chỉ custom solution |
| 26 | `face_recognition_sample.cpp` | `insightface_recognition` | Có (insightface_recognition) | Logic embedding + DB; API dùng insightface |
| 27 | `face_swap_sample.cpp` | `face_swap` | Có (face_swap) | Đủ solution + example |
| 28 | `face_tracking_sample.cpp` | `face_detection` (không track trong solution) | Một phần | Face + sort_track; solution face không bắt buộc có track |
| 29 | `face_tracking_bytetrack_sample.cpp` | `face_detection` | Một phần | Face + bytetrack |
| 30 | `face_tracking_ocsort_sample.cpp` | `face_detection` | Một phần | Face + ocsort |
| 31 | `face_tracking_rtsp_sample.cpp` | `face_detection_rtsp_default`, `rtsp_face_detection_default` | Có (face_detection/onnx rtsp) | RTSP + face |
| 32 | `face_yunet_int8_sample.cpp` | `face_detection` | Có (face_detection) | YuNet int8 |
| 33 | `ffmpeg_src_des_sample.cpp` | Không (ff_src, ff_des) | Có (custom: example_ff_src, example_ff_des) | Chỉ custom |
| 34 | `ffmpeg_transcode_sample.cpp` | Không | Không | Transcode thuần |
| 35 | `firesmoke_detect_sample.cpp` | `fire_smoke_detection` | Có (fire_smoke_detection/yolo) | Đủ solution + example |
| 36 | `frame_fusion_sample.cpp` | Không | Có (custom: example_frame_fusion) | Chỉ custom |
| 37 | `image_des_sample.cpp` | Không | Có (custom: example_image_des) | Chỉ custom |
| 38 | `image_src_sample.cpp` | Không (image_src) | Có (infer_nodes: image_source_detection – custom) | Chỉ custom solution |
| 39 | `interaction_with_pipe_sample.cpp` | Không | Không | Tương tác pipeline, không map solution |
| 40 | `lane_detect_sample.cpp` | Không | Có (infer_nodes: lane_detection – custom) | Chỉ custom solution |
| 41 | `license_check_sample.cpp` | Không | Không | License, không pipeline |
| 42 | `license_info_sample.cpp` | Không | Không | License info |
| 43 | `mask_rcnn_sample.cpp` | `mask_rcnn_detection_default`, `mask_rcnn_rtmp_default` | Có (mask_rcnn/tensorflow) | Đủ solution + example |
| 44 | `message_broker_sample.cpp` | Face + broker (json_mqtt_broker, …) | Có (infer_nodes: json_mqtt_broker, …) | Solution face_detection + broker nodes |
| 45 | `message_broker_sample2.cpp` | Tương tự | Có | Broker variants |
| 46 | `message_broker_kafka_sample.cpp` | Face + kafka | Có (infer_nodes: json_kafka_broker) | Custom/cấu hình broker |
| 47 | `mllm_analyse_sample.cpp` | `mllm_analysis` | Có (other_solutions/mllm_analysis) | Đủ solution + example |
| 48 | `mllm_analyse_sample_openai.cpp` | `mllm_analysis` | Có (mllm_analysis) | Backend OpenAI |
| 49 | `multi_detectors_sample.cpp` | Nhiều detector | Một phần (object_detection_yolo, custom) | Cần custom hoặc nhiều instance |
| 50 | `multi_detectors_and_classifiers_sample.cpp` | Custom | Có (custom: classifier, vehicle type/color) | Chỉ custom |
| 51 | `multi_trt_infer_nodes_sample.cpp` | Custom / TRT | Có (custom: trt vehicle) | Chỉ custom |
| 52 | `mqtt_json_receiver_sample.cpp` | Broker / MQTT | Có (json_mqtt_broker, ba_crossline_mqtt) | MQTT receiver |
| 53 | `N-1-N_sample.cpp` | Phụ thuộc pipeline | Một phần | N src, 1 infer, N des |
| 54 | `N-N_sample.cpp` | Phụ thuộc pipeline | Một phần | N src, N des |
| 55 | `nv_hard_codec_sample.cpp` | Không | Không | Hard codec, không solution |
| 56 | `obstacle_detect_sample.cpp` | `obstacle_detection` | Có (obstacle_detection/yolo) | Đủ solution + example |
| 57 | `openpose_sample.cpp` | Không | Có (infer_nodes: openpose_detection – **không** trong registry) | Chỉ custom solution |
| 58 | `plate_recognize_sample.cpp` | Custom (plate) | Có (custom: yolov11_plate, trt_plate) | Chỉ custom |
| 59 | `record_sample.cpp` | Không (record node) | Có (custom: example_record) | Chỉ custom |
| 60 | `rtsp_ba_crossline_sample.cpp` | `ba_crossline`, `ba_crossline_default` | Có (ba_crossline rtsp) | RTSP + BA crossline |
| 61 | `rtsp_des_sample.cpp` | Có (rtsp_des) | Có (custom: example_rtsp_des) | Custom solution có rtsp_des |
| 62 | `rtsp_src_sample.cpp` | `face_detection_rtsp_default`, `rtsp_face_detection_default` | Có (face_detection rtsp) | RTSP + face |
| 63 | `sse_broker_sample.cpp` | Không | Không | SSE broker, chưa thấy solution |
| 64 | `skip_sample.cpp` | Không | Không | Demo skip, không pipeline instance |
| 65 | `src_des_sample.cpp` | `minimal_default` / file_src + file_des | Có (face_detection file) | Có thể dùng face_detection file |
| 66 | `trt_infer_sample.cpp` | TRT (custom/object) | Có (custom: trt vehicle/plate) | Chỉ custom |
| 67 | `vehicle_body_scan_sample.cpp` | Custom | Có (custom: trt_vehicle_scanner) | Chỉ custom |
| 68 | `vehicle_cluster_based_on_classify_encoding_sample.cpp` | Custom | Có (custom: example_cluster) | Chỉ custom |
| 69 | `vehicle_tracking_sample.cpp` | `object_detection_yolo_default` / custom | Có (custom: vehicle detector) | Có thể map object/yolo |
| 70 | `video_restoration_sample.cpp` | Không | Có (infer_nodes: restoration – custom) | Chỉ custom solution |
| 71 | `wrong_way_detection_sample.cpp` | `wrong_way_detection` | Có (wrong_way_detection/yolo) | Đủ solution + example |
| 72 | `yolov11_face_bytetrack_sample.cpp` | `face_detection` + track | Một phần | YOLOv11 face + bytetrack |
| 73 | `yolov11_face_detector_trt_sample.cpp` | Face/TRT | Có (custom: trt_yolov11_face_detector) | Chỉ custom |
| 74 | `yolov11_face_detector_video_output_sample.cpp` | `face_detection` / yolov11 | Có (face_detection, custom) | Có thể dùng face hoặc custom |
| 75 | `yolov11_onnx_detector_sample.cpp` | `yolov11_detection`, `object_detection_yolo_default` | Có (yolov11_detection) | Đủ solution + example |
| 76 | `yolov11_plate_bytetrack_sample.cpp` | Custom (plate + bytetrack) | Có (custom: plate) | Chỉ custom |
| 77 | `yolov11_plate_detector_sample.cpp` | Custom (plate) | Có (custom: yolov11_plate_detector) | Chỉ custom |
| 78 | `yolov11_plate_detector_trt_sample.cpp` | Custom (TRT plate) | Có (custom: trt_yolov11_plate_detector) | Chỉ custom |

---

## Tóm tắt

- **Đã hỗ trợ tạo/xử lý instance (solution có sẵn + có hoặc có thể dùng example)**  
  - Face: `1-1-1`, `1-1-N`, `face_tracking_rtsp`, `face_yunet_int8`, `rtsp_src`, `src_des` (face_detection), `yolov11_face_detector_video_output`, `yolov11_onnx_detector` (yolov11_detection).
  - BA: `ba_crossline`, `ba_crossline_mqtt`, `ba_jam`, `ba_stop`, `ba_loitering`, `ba_area_enter_exit`, `ba_multiline_crossline_test`, `ba_multiple_crossline_counting_sample`, `rtsp_ba_crossline`.
  - Khác: `mask_rcnn`, `face_swap`, `face_recognition` (insightface), `mllm_analyse` (mllm_analysis), `firesmoke_detect` (fire_smoke_detection – có example), `obstacle_detect` (obstacle_detection – có example), `wrong_way_detection` (có example).

- **Chỉ hỗ trợ qua custom solution**  
  - App src/des, ffmpeg src/des, image src/des, frame_fusion, record, openpose, enet_seg, lane_detect, video_restoration, các broker (kafka, socket, …), plate, vehicle scanner/cluster, TRT vehicle/plate/face, classifier.

- **Chưa hỗ trợ (không có solution tương ứng)**  
  - `ba_crowding`, `ba_movement`, `ba_speed_estimation`, `cvedix_logger`, `cvedix_test`, `dynamic_pipeline*`, `interaction_with_pipe`, `license_*`, `nv_hard_codec`, `skip`, `sse_broker`, `ffmpeg_transcode`.

- **Example instance dùng solution không có trong registry**  
  - Các file trong `examples/instances/infer_nodes/` dùng `yolo_detection`, `openpose_detection`, `lane_detection`, `enet_segmentation`, `restoration`, `image_source_detection`, … chỉ chạy được khi có custom solution tương ứng được load từ thư mục solutions.

---

*Cập nhật theo code tại thời điểm kiểm tra (solution_registry, examples/instances, sample/*.cpp).*
