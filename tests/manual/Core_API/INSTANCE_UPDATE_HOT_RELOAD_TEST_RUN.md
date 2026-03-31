# Kết quả chạy test – Instance Update Hot-Reload

**Ngày chạy**: 2026-03-06  
**Kịch bản**: [INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md](./INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md)

## Cơ chế Zero-Downtime (Worker / Subprocess)

Khi chạy ở **subprocess mode**, worker dùng **Atomic Pipeline Swap**:

- **PipelineSnapshot**: Mỗi pipeline là một snapshot bất biến (danh sách node). Runtime luôn đọc pipeline đang active qua `getActivePipeline()` (shared lock).
- **Swap O(1)**: Cập nhật = build pipeline mới → start source của pipeline mới → `setActivePipeline(new)` (đổi con trỏ) → stop source của pipeline cũ → giải phóng pipeline cũ. Vòng lặp worker **không bị block** bởi swap.
- **Hot swap**: `hotSwapPipeline()` thực hiện: pre-build → tạo snapshot mới → start source mới → atomic swap → setup hooks → stop source cũ. Mục tiêu **zero downtime** (99–99.99% uptime).

## Môi trường

- **Server**: `./bin/omniapi` (từ `build/`), chạy nền với log ghi ra `/tmp/omniapi-test.log` và terminal.
- **Chế độ**: **In-process (legacy)** – log có dòng `[Main] Execution mode: in-process (legacy)`.
- **Lưu ý**: Hot-reload line **không restart** được implement trong **worker** (subprocess mode). Ở in-process, PATCH/PUT update instance có thể hành xử khác (xem bên dưới).

## Kết quả từng bước

| Bước | Mô tả | Kết quả | Ghi chú |
|------|--------|---------|--------|
| **0** | Tạo instance ba_crossline (POST) | **HTTP 201** | Instance tạo thành công. Response có `instanceId` dạng UUID (vd: `31a5210e-ac47-4923-a9df-3cbcabe0b7d5`), không dùng `name` làm ID. |
| **0** | Start instance (POST .../start) | **HTTP 202** | Start được chấp nhận, pipeline build/start trong nền. |
| **0** | GET instance (trạng thái) | **OK** | `status: "ready"`, `running: null` (API có thể không trả `running` trong một số response). Pipeline đang chạy (log có rtmp_src, yolo_detector, ba_crossline). |
| **1** | PATCH đổi CROSSLINE_* | **Timeout** | Request PATCH treo >30s, không trả về. Server in-process; có thể `updateInstance` (registry) block lâu hoặc lock. |
| **5** | PATCH instance không tồn tại | **HTTP 404** | Đúng kỳ vọng: `{"error":"Instance not found","message":"Instance not found: instance_not_exist_12345"}`. |
| **6** | Stop instance | **HTTP 202** | Stop được chấp nhận, instance dừng trong nền. |

## Log server (trích)

- Server khởi động bình thường, listen `0.0.0.0:8080`.
- Instance `31a5210e-ac47-4923-a9df-3cbcabe0b7d5`: pipeline có `rtmp_src`, `yolo_detector`, `sort_tracker`, `ba_crossline`; có log “queue full, dropping meta!” ở ba_crossline (bình thường khi downstream xử lý chậm).
- Không thấy log lỗi khi nhận PATCH; request PATCH đến instance có vẻ bị block ở xử lý in-process.

## Kết luận và khuyến nghị

1. **Test 0, 5, 6**: Chạy đúng – tạo instance, start, 404 negative, stop.
2. **Test 1 (PATCH line)**: Trên bản chạy này (in-process), PATCH body chỉ có `additionalParams.input.CROSSLINE_*` bị **timeout**. Cần kiểm tra thêm:
   - InprocessInstanceManager::updateInstance / InstanceRegistry::updateInstance có block lâu (lock, I/O) không.
   - Chạy lại với **subprocess mode** (cấu hình dùng SubprocessInstanceManager) để xác nhận hot-reload line không restart theo đúng thiết kế.
3. **Kịch bản manual**: Nên bổ sung trong [INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md](./INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md):
   - **Prerequisites**: Chạy API ở **subprocess mode** để test hot-reload line (không restart).
   - **Instance ID**: Sau POST tạo instance, lấy `instanceId` từ response (UUID) dùng cho mọi request tiếp theo thay vì dùng `name`.

## Lệnh đã chạy (tham khảo)

```bash
# Variables
SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/instance"
INSTANCE_ID="31a5210e-ac47-4923-a9df-3cbcabe0b7d5"   # từ response POST

# 0. Tạo instance (body như trong kịch bản)
curl -s -X POST "${BASE_URL}" -H "Content-Type: application/json" -d '{ ... }'

# 0. Start
curl -s -X POST "${BASE_URL}/${INSTANCE_ID}/start" -H "Content-Type: application/json"

# 5. Negative
curl -s -X PATCH "${BASE_URL}/instance_not_exist_12345" \
  -H "Content-Type: application/json" \
  -d '{"additionalParams":{"input":{"CROSSLINE_START_X":"0"}}}'

# 6. Stop
curl -s -X POST "${BASE_URL}/${INSTANCE_ID}/stop" -H "Content-Type: application/json"
```
