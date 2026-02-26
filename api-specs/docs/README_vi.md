# Nguồn gốc tài liệu API tiếng Việt

## Luồng tạo tài liệu

1. **Nguồn dữ liệu:** Các file trong `api-specs/openapi/vi/`:
   - `openapi.yaml` (phần info, servers, v.v.)
   - `paths/**/*.yaml` (từng endpoint, merge vào spec)

2. **Merge OpenAPI:** Script `scripts/merge_openapi.py`:
   - **Option 1:** Gộp tất cả path files vào một file `api-specs/openapi/vi/openapi.yaml`
   - **Option 2:** Gộp và xuất Markdown → `api-specs/docs/vi/API.md`

3. **Lệnh tái sinh tài liệu:**
   ```bash
   python scripts/merge_openapi.py api-specs/openapi/vi 1   # cập nhật openapi.yaml
   python scripts/merge_openapi.py api-specs/openapi/vi 2   # cập nhật docs/vi/API.md
   ```

## Vì sao từng có mô tả bằng tiếng Anh?

- Nhiều file trong `api-specs/openapi/vi/paths/` ban đầu được tạo/copy từ bản tiếng Anh (en) và **chưa được dịch** summary/description sang tiếng Việt.
- Chỉ phần **info** trong `openapi.yaml` và một số path (AI, models, onvif, …) đã có nội dung tiếng Việt.
- Để toàn bộ mô tả trong `API.md` và `openapi.yaml` (vi) là tiếng Việt, cần **chỉnh sửa trực tiếp** các file trong `api-specs/openapi/vi/paths/` (summary, description, parameter/response description). Các từ bắt buộc giữ tiếng Anh: tên kỹ thuật (operationId, ROI, UUID, CORS, schema refs, v.v.).

## Đã dịch

- **area_core:** jams, jams_jamid, stops, stops_stopid (đầy đủ summary + description).
- **area_securt:** areas, area_areaid, và tất cả file tạo vùng (crossing, intrusion, loitering, crowding, occupancy, crowd estimation, dwelling, armed person, object left/removed/enter-exit, fallen person, vehicle guard, face covered) — summary tiếng Việt.
- **video:** core_video_upload, core_video_videoname (tải lên, đổi tên, xóa).
- **fonts:** core_font_upload, core_font_fontname (tải lên, đổi tên, xóa).
- **core:** core_license_info, hls_instanceid_stream_m3u8, hls_instanceid_segment_segmentid_ts.
- **instances:** core_instance_instanceid_jams_batch.
- **Chung:** Script `scripts/translate_vi_paths_common.py` đã thay thế hàng loạt trong `vi/paths`: description "Instance ID", "Instance not found", "Invalid request", "Internal server error", "Server error", "CORS preflight response", "Area ID" → bản tiếng Việt (96 file được cập nhật).

## Trạng thái dịch (đã hoàn thành summary)

Đã dịch **summary** (và phần lớn description) sang tiếng Việt cho toàn bộ path trong `api-specs/openapi/vi/paths/`:

- **instances:** core_instance, core_instance_instanceid_*, core_instance_quick, core_instance_status_summary, api_v1_instances_instance_id_fps, batch start/stop/restart, load/unload, jams_batch, stops_batch, lines_batch, v.v.
- **groups:** core_groups, core_groups_groupid, core_groups_groupid_instances.
- **solutions:** core_solution, core_solution_solutionid*, core_solution_defaults*.
- **node:** core_node, core_node_nodeid, core_node_template*, core_node_stats, core_node_preconfigured*, core_node_build_solution.
- **lines_core / lines_securt:** crossing lines, counting line, tailgating line, line by ID.
- **recognition:** recognition_faces*, recognition_face-database_connection, recognition_search, recognition_recognize, recognition_subjects_subject.
- **securt_instance:** securt_instance, securt_instance_instanceid_* (stats, exclusion_areas, feature_extraction, performance_profile, pip, lpr, attributes_extraction, analytics_entities, face_detection, input, masking_areas, motion_area, output), securt_experimental_*.

Một số **description** dài hoặc ví dụ (example summary) trong schema vẫn có thể còn tiếng Anh; có thể bổ sung dịch theo nhu cầu. Sau mỗi lần sửa path, chạy: `python3 scripts/merge_openapi.py api-specs/openapi/vi 1` và `python3 scripts/merge_openapi.py api-specs/openapi/vi 2`.
