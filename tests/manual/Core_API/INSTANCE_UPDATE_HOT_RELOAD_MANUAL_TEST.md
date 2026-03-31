# Hướng Dẫn Test Thủ Công - Instance Update Không Restart (Hot-Reload Lines)

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Test Cases](#test-cases)
5. [Expected Results](#expected-results)
6. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công tính năng **cập nhật instance không dừng chạy** (hot-reload) cho solution **ba_crossline**:
- Khi **PATCH** hoặc **PUT** instance với thay đổi **CrossingLines** hoặc **CROSSLINE_*** (vị trí line), instance **không bị restart**.
- Pipeline tiếp tục chạy, line mới được áp dụng tại runtime qua SDK `set_lines()`.
- Mục tiêu: **uptime 99%** khi chỉ cập nhật line (thêm line, sửa vị trí line).

### Base URL
```
http://localhost:8080/v1/core/instance
```

### Quy tắc cập nhật (PATCH)
- **Body gửi gì thì update cái đó, còn lại giữ nguyên.** Chỉ các key có trong body mới được cập nhật; toàn bộ config khác (output, RtmpUrl, Zone, …) giữ nguyên. Gửi PATCH chỉ với line (CrossingLines hoặc CROSSLINE_*) thì chỉ line thay đổi, output RTMP và mọi thứ khác không đổi.

### Phạm vi test
- **PATCH** `/v1/core/instance/{instanceId}` với `additionalParams.CrossingLines` hoặc `additionalParams.input.CROSSLINE_*`.
- Kiểm tra instance vẫn **running**, **không restart**, và line mới hiển thị/đếm đúng.

---

## Prerequisites

1. **API Server đang chạy**. **Hot-reload line (không restart)** hoạt động đầy đủ khi dùng **chế độ subprocess** (mỗi instance chạy trong worker riêng). Chế độ in-process (legacy) có thể xử lý PATCH/PUT khác.
   ```bash
   curl http://localhost:8080/v1/core/health
   ```

2. **Công cụ**:
   - `curl`, `jq` (optional)

3. **Instance ba_crossline**:
   - Tạo mới theo Test Case 0 hoặc dùng instance có sẵn. **Lưu ý**: Sau POST tạo instance, dùng `instanceId` trong response (thường là UUID) cho mọi request tiếp theo, không dùng `name`.

### Chạy API với log ra file (tránh log instance tràn console)

Khi instance chạy, worker in rất nhiều log ra console. Để ghi log vào file:

**Test hot-swap với delay 5s (mô phỏng gap giữa stop pipeline cũ và build/start pipeline mới):**
```bash
# Chỉ khi cần test: set EDGE_AI_HOTSWAP_DELAY_SEC=5. Chạy từ thư mục gốc project:
EDGE_AI_HOTSWAP_DELAY_SEC=5 EDGE_AI_EXECUTION_MODE=subprocess ./build/bin/omniapi
# Hoặc nếu đang ở trong build/: EDGE_AI_HOTSWAP_DELAY_SEC=5 EDGE_AI_EXECUTION_MODE=subprocess ./bin/omniapi
```
**Lưu ý EDGE_AI_HOTSWAP_DELAY_SEC và output stream:**
- **Legacy path** (pipeline không dùng persistent output leg): delay nằm **sau** khi stop pipeline cũ, **trước** khi build pipeline mới. Trong khoảng delay không có stream; sau đó build + start pipeline mới → RTMP kết nối lại khi start → stream hoạt động lại. Instance chỉ được coi là `running` sau khi start pipeline mới thành công.
- **Zero-downtime path** (có frame_router_ + output_leg_): delay (nếu có) nằm **sau khi** đã start pipeline mới, để tránh khoảng 5s chỉ bơm last frame khiến server RTMP đóng kết nối và mất output. Thứ tự: drain → start pipeline mới ngay → (sau đó mới delay cho test).
- Nếu sau update **instance vẫn chạy nhưng output stream mất**: kiểm tra log worker có "Zero-downtime pipeline swap" hay "Pipeline swap (stop old → build new → start new)"; có "Hot-swap: preserved RTMP output in tempConfig" không; và `/tmp/runtime_update.log` xem `result=hot_swap_ok` hay `runtime_ok`.

**Cách 1 – Chạy API và ghi mọi output (API + worker) ra file:**
```bash
# Subprocess mode (cần cho hot-reload line). Chạy từ thư mục gốc project (omniapi):
EDGE_AI_EXECUTION_MODE=subprocess ./build/bin/omniapi >> /tmp/omniapi.log 2>&1
# Xem log: tail -f /tmp/omniapi.log
```

**Cách 2 – Script test vừa start API vừa ghi log:**
```bash
LOG_FILE=/tmp/omniapi.log ./tests/manual/Core_API/run_patch_crossinglines_test.sh
```

**Cách 3 – API đã chạy sẵn (bạn đã start với redirect ở Cách 1), chỉ chạy test:**
```bash
START_SERVER=0 LOG_FILE=/tmp/omniapi.log ./tests/manual/Core_API/run_patch_crossinglines_test.sh
```

Script `run_patch_crossinglines_test.sh` kiểm tra trong log có dòng `Line-only update: applying CrossingLines at runtime (no hot-swap)` và không có hot swap khi PATCH chỉ CrossingLines.

**Dọn sạch worker cũ sau khi tắt API (tránh "Worker not ready" / mutex timeout khi restart):**
```bash
# Từ thư mục gốc project
./scripts/clean_workers.sh
```
Script sẽ: (1) kill toàn bộ process `edgeos-worker`, (2) xóa socket `/tmp/edgeos_worker_*.sock` và `/opt/omniapi/run/edgeos_worker_*.sock`. Chạy sau khi đã Ctrl+C tắt omniapi.

**Tại sao mỗi lần start API có rất nhiều worker (Exited cleanly)?**  
Khi API khởi động, nó gọi `loadPersistentInstances()`: đọc **toàn bộ instance** trong storage (ví dụ `/opt/omniapi/instances/instances.json`), với mỗi instance có **`persistent: true`** thì **spawn một worker**. Do đó nếu trong storage có nhiều instance (tạo trước đó, hoặc nhiều máy dùng chung storage) và đều đánh dấu persistent → mỗi lần start sẽ có hàng chục worker. Khi tắt API (Ctrl+C), tất cả worker nhận tín hiệu thoát → log "[Worker:uuid] Exited cleanly" và "Accept thread join timeout" là bình thường.  
**Cách giảm số worker khi start:** (1) Tạo instance test với **`persistent: false`**; (2) Xóa instance không dùng qua API `DELETE /v1/core/instance/{id}` hoặc chỉnh/xóa bớt trong file storage; (3) Trước khi start lại, chạy `./scripts/clean_workers.sh` để dọn process/socket cũ.

---

## Setup Variables

```bash
SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/instance"
# Dùng instanceId từ response POST tạo instance (dạng UUID), không dùng name
INSTANCE_ID="ba_crossline_rtsp_rtmp_test"   # hoặc UUID ví dụ: 31a5210e-ac47-4923-a9df-3cbcabe0b7d5
```

---

## Test Cases

### 0. Tạo Instance và Start (Chuẩn bị)

Tạo instance ba_crossline với line ban đầu, sau đó start để có pipeline đang chạy.

```bash
echo "=== 0. Tạo instance ba_crossline ==="
curl -X POST "${BASE_URL}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "ba_crossline_rtsp_rtmp_test",
    "group": "test",
    "solution": "ba_crossline",
    "persistent": false,
    "autoStart": false,
    "detectionSensitivity": "Medium",
    "additionalParams": {
      "input": {
        "RTMP_SRC_URL": "rtmp://192.168.1.128:1935/live/camera_demo_sang_vehicle",
        "WEIGHTS_PATH": "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights",
        "CONFIG_PATH": "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg",
        "LABELS_PATH": "/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt",
        "CROSSLINE_START_X": "0",
        "CROSSLINE_START_Y": "250",
        "CROSSLINE_END_X": "700",
        "CROSSLINE_END_Y": "220",
        "RESIZE_RATIO": "0.6",
        "GST_DECODER_NAME": "avdec_h264",
        "SKIP_INTERVAL": "0",
        "CODEC_TYPE": "h264"
      },
      "output": {
        "RTMP_DES_URL": "rtmp://192.168.1.128:1935/live/ba_crossing_stream_1",
        "ENABLE_SCREEN_DES": "false"
      }
    }
  }' | jq .
```

**Expected**: Status 201, response có `instanceId` (trùng `name` nếu dùng name làm id).

```bash
echo "=== Start instance ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/start" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200. Đợi vài giây cho pipeline chạy ổn định.

```bash
echo "=== Kiểm tra instance đang chạy ==="
curl -s "${BASE_URL}/${INSTANCE_ID}" | jq '.running, .fps'
```

**Expected**: `true` và giá trị fps (số).

---

### 1. PATCH – Cập nhật line (CROSSLINE_*) – Không restart

Sửa vị trí line qua `additionalParams.input` (legacy CROSSLINE_*). Instance phải **vẫn running**, không restart.

**Bước 1**: Ghi nhận thời điểm / trạng thái trước khi PATCH (optional, để so sánh uptime).

```bash
echo "=== 1. Trước PATCH - trạng thái instance ==="
curl -s "${BASE_URL}/${INSTANCE_ID}" | jq '{ running, fps, displayName }'
```

**Bước 2**: PATCH với line mới (ví dụ đổi tọa độ).

```bash
echo "=== 1. PATCH instance - đổi vị trí line (CROSSLINE_*) ==="
curl -X PATCH "${BASE_URL}/${INSTANCE_ID}" \
  -H "Content-Type: application/json" \
  -d '{
    "additionalParams": {
      "input": {
        "CROSSLINE_START_X": "50",
        "CROSSLINE_START_Y": "260",
        "CROSSLINE_END_X": "750",
        "CROSSLINE_END_Y": "230"
      }
    }
  }' -w "\nHTTP_STATUS:%{http_code}\n" -o /tmp/patch_response.json

cat /tmp/patch_response.json | jq . 2>/dev/null || cat /tmp/patch_response.json
```

**Expected**:
- HTTP status **200** hoặc **204** (tùy API).
- Response (nếu có body): success, không lỗi.

**Bước 3**: Kiểm tra instance **vẫn đang chạy**, không bị stop/restart.

```bash
echo "=== 1. Sau PATCH - instance vẫn running ==="
curl -s "${BASE_URL}/${INSTANCE_ID}" | jq '{ running, fps, displayName }'
```

**Expected**:
- `running`: **true** (instance không bị dừng).
- `fps`: vẫn có giá trị (pipeline vẫn xử lý frame).

**Pass criteria**: Instance vẫn `running`, không có khoảng downtime (restart).

---

### 2. PATCH – Cập nhật nhiều line (CrossingLines JSON) – Không restart

Cập nhật nhiều line qua `additionalParams.CrossingLines` (chuỗi JSON array). Instance không được restart.

```bash
echo "=== 2. PATCH instance - CrossingLines (nhiều line) ==="
# CrossingLines trong API thường là JSON string (mảng line được serialize thành string)
curl -X PATCH "${BASE_URL}/${INSTANCE_ID}" \
  -H "Content-Type: application/json" \
  -d '{
    "additionalParams": {
      "CrossingLines": "[{\"id\":\"line1\",\"name\":\"Entry Line\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Up\",\"classes\":[\"Vehicle\",\"Person\"],\"color\":[255,0,0,255]},{\"id\":\"line2\",\"name\":\"Exit Line\",\"coordinates\":[{\"x\":100,\"y\":400},{\"x\":800,\"y\":380}],\"direction\":\"Down\",\"classes\":[\"Vehicle\"],\"color\":[0,255,0,255]}]"
    }
  }' | jq .
```

**Expected**: HTTP 200/204, instance vẫn running.

```bash
echo "=== 2. Sau PATCH CrossingLines - kiểm tra running ==="
curl -s "${BASE_URL}/${INSTANCE_ID}" | jq '{ running, fps }'
```

**Expected**: `running`: **true**, fps vẫn có.

---

### 3. PUT – Update instance (camelCase) với additionalParams – Không restart

Dùng **PUT** với body camelCase, chỉ gửi phần cần cập nhật (ví dụ additionalParams với line). Instance không restart.

```bash
echo "=== 3. PUT instance - additionalParams (line) ==="
curl -X PUT "${BASE_URL}/${INSTANCE_ID}" \
  -H "Content-Type: application/json" \
  -d '{
    "additionalParams": {
      "input": {
        "CROSSLINE_START_X": "100",
        "CROSSLINE_START_Y": "240",
        "CROSSLINE_END_X": "720",
        "CROSSLINE_END_Y": "210"
      }
    }
  }' | jq .
```

**Expected**: Status 200, body có message kiểu "Instance updated successfully". Instance vẫn running.

```bash
curl -s "${BASE_URL}/${INSTANCE_ID}" | jq '{ running }'
```

**Expected**: `"running": true`.

---

### 4. Liên tiếp nhiều lần PATCH – Ổn định uptime

Thực hiện nhiều lần PATCH liên tiếp (đổi line nhiều lần) để xác nhận không có restart giữa chừng.

```bash
echo "=== 4. Nhiều lần PATCH liên tiếp ==="
for i in 1 2 3 4 5; do
  echo "--- PATCH lần $i ---"
  curl -s -X PATCH "${BASE_URL}/${INSTANCE_ID}" \
    -H "Content-Type: application/json" \
    -d "{
      \"additionalParams\": {
        \"input\": {
          \"CROSSLINE_START_X\": \"$((i * 20))\", \"CROSSLINE_START_Y\": \"250\",
          \"CROSSLINE_END_X\": \"$((700 + i * 10))\", \"CROSSLINE_END_Y\": \"220\"
        }
      }
    }" | jq -r '.message // .error // "ok"'
  sleep 1
  RUNNING=$(curl -s "${BASE_URL}/${INSTANCE_ID}" | jq -r '.running')
  echo "  running=$RUNNING"
  [ "$RUNNING" != "true" ] && echo "FAIL: instance stopped at iteration $i" && break
done
```

**Expected**: Mỗi lần `running=true`, không có lỗi "instance stopped".

---

### 5. Instance không tồn tại (Negative)

```bash
echo "=== 5. PATCH instance không tồn tại ==="
curl -X PATCH "${BASE_URL}/instance_not_exist_12345" \
  -H "Content-Type: application/json" \
  -d '{"additionalParams":{"input":{"CROSSLINE_START_X":"0"}}}' | jq .
```

**Expected**: Status **404**, message kiểu "Instance not found".

---

### 6. Dừng instance sau khi test (Cleanup)

```bash
echo "=== 6. Stop instance ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/stop" -H "Content-Type: application/json" | jq .

# Optional: xóa instance
# curl -X DELETE "${BASE_URL}/${INSTANCE_ID}" -H "Content-Type: application/json" | jq .
```

---

## Expected Results – Tóm tắt

| Test Case | Hành động | Kỳ vọng |
|-----------|-----------|---------|
| 0 | Tạo + Start instance ba_crossline | 201/200, instance running |
| 1 | PATCH CROSSLINE_* | 200/204, **running vẫn true**, không restart |
| 2 | PATCH CrossingLines (nhiều line) | 200/204, **running vẫn true** |
| 3 | PUT additionalParams (line) | 200, "Instance updated successfully", **running true** |
| 4 | Nhiều lần PATCH liên tiếp | Mỗi lần sau PATCH vẫn **running=true** |
| 5 | PATCH instance không tồn tại | 404 |
| 6 | Stop instance | 200 |

**Tiêu chí thành công tính năng**: Khi chỉ cập nhật line (CrossingLines hoặc CROSSLINE_*), instance **không bị restart**, `running` luôn **true**, line mới được áp dụng tại runtime (có thể kiểm chứng qua stream/output hoặc event đếm qua line).

---

## Luồng runtime khi update instance (subprocess)

1. **API** PUT/PATCH `/v1/core/instance/{id}` → `InstanceHandler::updateInstance` → `SubprocessInstanceManager::updateInstance(instanceId, configJson)`.
2. **API** gửi IPC **UPDATE_INSTANCE** với payload `config` (có thể là partial: chỉ `additionalParams.input.CROSSLINE_*` hoặc full config).
3. **Worker** `handleUpdateInstance`:
   - **Deep-merge** `config` vào `config_` (merge theo object, không ghi đè cả object khi request chỉ gửi một phần). Nhờ đó partial update không làm mất `AdditionalParams`/Input/Output còn lại và không gây rebuild/hot-swap nhầm.
   - So sánh `oldConfig` vs `config_`: `checkIfNeedsRebuild` (solution, model path, …) và `applyConfigToPipeline` (URL nguồn, CrossingLines/CROSSLINE_*, Zone, …).
   - Chỉ **line** (CrossingLines, CROSSLINE_*): áp dụng runtime qua `applyLinesFromParamsToPipeline` → `set_lines()` → **không restart**.
   - Thay đổi cấu trúc (solution, model, URL nguồn, Zone, …): **hot-swap** hoặc **rebuild** (stop → build → start).

Nếu instance vẫn bị reload/restart sau khi chỉ sửa line: trước đây có thể do merge nông (shallow) khiến cả object `additionalParams`/Input bị thay thế, config thiếu → logic coi là thay đổi lớn. Đã sửa bằng **deep merge** trong worker.

## Log runtime update (dev)

Khi chạy subprocess mode, worker ghi log luồng cập nhật runtime vào **một file riêng** để dễ kiểm tra lỗi khi dev:

- **File:** `runtime_update.log`
- **Thư mục (ưu tiên):** `EDGE_AI_RUNTIME_UPDATE_LOG_DIR` → `LOG_DIR` → **`/tmp`** (mặc định). File mặc định: **`/tmp/runtime_update.log`**.
- File được tạo ngay khi worker ready (trước khi có PATCH), sau đó mỗi lần PATCH sẽ ghi thêm dòng.
- **Nội dung:** Mỗi dòng có dạng `[timestamp][instance_id] message`, ví dụ:
  - `worker ready, runtime_update.log active (...)`
  - `UPDATE_INSTANCE received`
  - `config merged (deep merge)`
  - `checkIfNeedsRebuild=true/false`
  - `applyConfigToPipeline: linesChanged=true, calling applyLinesFromParamsToPipeline`
  - `applyLinesFromParamsToPipeline: set_lines()=ok` hoặc `=failed`
  - `result=runtime_ok (no restart)` hoặc `result=hot_swap_ok` hoặc `decision=hot_swap_or_rebuild`

**Ví dụ xem log khi dev:**
```bash
# Mặc định ghi vào /tmp (không cần set env)
tail -f /tmp/runtime_update.log
```

Xem thêm biến `EDGE_AI_RUNTIME_UPDATE_LOG_DIR` trong [ENVIRONMENT_VARIABLES.md](../../docs/ENVIRONMENT_VARIABLES.md).

## Instance treo / mất stream output sau khi cập nhật

- **Mất output sau update (instance vẫn chạy):** Trước đây khi hot-swap (PATCH thay đổi ngoài line), pipeline mới start trước, pipeline cũ stop sau → rtmp_des mới cố kết nối cùng stream key trong khi cũ vẫn push → server từ chối / connection fail → mất stream. **Đã sửa:** thứ tự là **stop pipeline cũ trước** → build mới → start mới → rtmp_des mới kết nối được. Có gap ngắn (vài giây) không stream trong lúc build+start; sau đó output hoạt động bình thường.
- **“Chỉ thêm line” nhưng vẫn mất stream:** Nếu PATCH gửi kèm key khác (Zone, output rỗng, param khác) → worker coi là **không phải line-only** → chạy **hot-swap**. Trong hot-swap, `tempConfig` dùng để build pipeline mới; nếu thiếu `additionalParams.output` / RTMP URL thì pipeline mới không có FrameRouterSinkNode/rtmp_des → **stream mất**. **Đã bổ sung:** trong `hotSwapPipeline()` luôn **preserve RTMP output** vào `tempConfig` (từ config_ hiện tại) trước khi `preBuildPipeline`, và log "Hot-swap: preserved RTMP output in tempConfig". Để tránh hot-swap khi chỉ thêm line: gửi PATCH **chỉ** với line (CrossingLines hoặc CROSSLINE_*), không gửi kèm Zone/output/param khác; khi đó log sẽ là "Line-only update: applying CrossingLines at runtime (no hot-swap)" và stream giữ nguyên.
- **Nếu instance “treo” rồi chạy lại:** Thường do pipeline bị **rebuild** hoặc **hot-swap** (khi PATCH không chỉ đổi line, hoặc `set_lines()` thất bại trước khi sửa “no restart when line apply fail”). Khi đó pipeline dừng rồi start lại → kết nối RTMP/stream output bị ngắt.
- **Instance chạy bình thường nhưng không thấy stream RTMP output:** Config dùng `additionalParams.input` / `additionalParams.output` (RTMP_SRC_URL, RTMP_DES_URL). Đảm bảo:
  1. **Khi tạo instance:** API đã parse đúng `additionalParams.output.RTMP_DES_URL` vào request (CreateInstanceRequest) và worker config có `RtmpUrl` hoặc `AdditionalParams.RTMP_DES_URL`.
  2. **Khi start instance (worker spawn từ storage):** `buildWorkerConfigFromInstanceInfo` phải set `config["RtmpUrl"]` và `AdditionalParams.RTMP_URL`/`RTMP_DES_URL`; khi load instance từ file, storage phải gán `info.rtmpUrl` từ `AdditionalParams.RTMP_DES_URL`/`RTMP_URL` nếu chưa có. (Đã sửa trong code.)
  3. **Log worker:** Khi build pipeline nên thấy `Found RTMP_URL in top-level RtmpUrl` hoặc `getRTMPUrl: ... RTMP_DES_URL` và `Created persistent output leg + frame router`. Nếu thấy `RTMP URL not provided, RTMP destination node will be skipped` thì URL chưa tới worker.
- **Pipeline chạy, log rất nhiều, rtmp_des in_queue 200+ nhưng khi play RTMP URL vẫn không thấy stream:** Pipeline gửi frame ~30 fps vào `rtmp_des`, trong khi node encode + push RTMP chậm hơn (~25 fps) → hàng đợi input của `rtmp_des` tăng không giới hạn (200+), stream bị trễ rất lớn hoặc không kịp hiển thị. **Đã sửa:** trong `FrameRouter::submitFrame()` thêm **throttle** chỉ inject frame vào output leg tối đa ~20 fps (50 ms giữa hai lần inject). Hàng đợi không còn tăng vô hạn, stream ra RTMP ổn định và có thể xem được. Nếu cần output mượt hơn cần tăng tốc encode/push phía SDK hoặc giảm độ phân giải/bitrate.

- **Vẫn mất output sau hot-swap (zero-downtime path):** Sau khi start pipeline mới, có thể có khoảng ngắn mà pipeline mới chưa kịp gửi frame đầu tiên (source kết nối, decode…) trong khi last-frame pump đã tắt → không có frame ra RTMP → stream đứng hoặc server đóng. **Đã bổ sung:** giữ last-frame pump chạy thêm **500 ms** sau khi start pipeline mới (pump overlap), rồi mới tắt pump; trong 500 ms đó pipeline mới kịp gửi frame → stream liên tục. Log worker: "Pump overlap 500ms (ensure new pipeline feeds output before stopping pump)".
- **Mất output khi dùng EDGE_AI_HOTSWAP_DELAY_SEC=5:** Trước đây trong **legacy path** (stop old → build new → start new), delay 5s nằm **sau** khi build nhưng **trước** khi start pipeline mới, và `pipeline_running_` được set true trước delay → trong 5s instance báo "running" nhưng không có frame ra RTMP; sau 5s khi start pipeline mới, một số môi trường (RTMP server đóng kết nối khi idle) có thể khiến output không hoạt động lại. **Đã sửa:** delay chuyển ra **sau** stop pipeline cũ, **trước** build pipeline mới; và `pipeline_running_` chỉ set true **sau khi** start pipeline mới thành công. Như vậy khoảng "không stream" nằm gọn trong lúc delay + build, và khi start xong thì RTMP kết nối mới ngay lúc có frame.
- **Mất stream key trên server:** Khi pipeline restart, client RTMP (push) ngắt kết nối; server (nginx-rtmp, v.v.) **giải phóng stream key**. Khi pipeline kết nối lại, có thể cần **stream key giống hệt** và server cho phép reconnect, hoặc stream key đã bị release và output không còn. Cách giảm thiểu:
  1. **Chỉ PATCH line** (CrossingLines / CROSSLINE_*) và đảm bảo **runtime update thành công** (xem log `result=runtime_ok (no restart)`) → pipeline không restart → stream không mất.
  2. Tránh PATCH kèm Zone, URL, param khác (dễ gây rebuild).
  3. Nếu bắt buộc phải restart instance: sau khi start lại, đảm bảo config output (RTMP URL + stream key) đúng và server chấp nhận push mới (một số server tự giải phóng key khi disconnect).

## Troubleshooting

1. **Instance restart sau PATCH**
   - Kiểm tra API đang dùng **SubprocessInstanceManager** (mỗi instance một worker). Hot-reload line chỉ áp dụng trong subprocess mode.
   - Worker dùng **deep merge** cho config; nếu request gửi full body với key khác (ví dụ chỉ `Input` mà không có `additionalParams`), vẫn có thể áp dụng đúng nếu format có `Input`/`Output` hoặc `additionalParams.input`/`output`.
   - Xem log worker: "Applied N line(s) at runtime (no restart)" = thành công; "Config changes require pipeline rebuild" hoặc "Hot swap" = có thay đổi ngoài line (URL, model, zone, …).

2. **PATCH trả 400/500**
   - Kiểm tra JSON: `additionalParams` có thể là object với `input`/`output` hoặc key phẳng (CrossingLines, CROSSLINE_*).
   - CrossingLines nếu dùng string thì phải là JSON string (escape đúng).

3. **Line không đổi trên stream**
   - Đảm bảo solution là **ba_crossline** và pipeline có `ba_crossline_node`.
   - Kiểm tra worker log có dòng "Applied ... line(s) at runtime" hay không.

4. **Biến môi trường**
   - Có thể cần `INSTANCE_ID` trùng với instance đã tạo (name hoặc instanceId trả về từ POST).

5. **"Failed to create instance" / "Failed to spawn worker for instance"**
   - **Nguyên nhân thường gặp:** API không tìm thấy `edgeos-worker`, thư mục socket không ghi được, hoặc worker khởi động chậm/lỗi.
   - **Cách xử lý:**
     1. Chạy script chẩn đoán: `./scripts/diagnose_spawn_worker.sh`
     2. Đảm bảo worker cùng thư mục với API hoặc set đường dẫn tuyệt đối:
        ```bash
        export EDGE_AI_WORKER_PATH=/path/to/edgeos-worker   # ví dụ: ./build/bin/edgeos-worker (từ thư mục gốc)
        ```
     3. Nếu không ghi được `/opt/omniapi/run`: `export EDGE_AI_SOCKET_DIR=/tmp` hoặc `sudo chown $USER /opt/omniapi/run`
     4. Xem **log server API (stderr)** khi tạo instance: sẽ có dòng chi tiết như "Worker executable not found", "Fork failed", hoặc "Worker failed to become ready".

6. **PATCH không thấy server response (instance vẫn đang chạy)**
   - **Nguyên nhân thường gặp:** API **block** chờ worker trả lời qua IPC. Timeout mặc định là **5 giây** (`IPC_START_STOP_TIMEOUT_MS`, xem [ENVIRONMENT_VARIABLES.md](../../docs/ENVIRONMENT_VARIABLES.md)). Nếu client (curl, Postman, browser) có timeout **ngắn hơn** (ví dụ 3s), client sẽ báo timeout / "no response" trước khi server trả về.
   - **Với PATCH chỉ CrossingLines (line-only):** Worker đi nhánh "Line-only update" và trả lời **rất nhanh** (vài trăm ms). Nếu vẫn không thấy response thì kiểm tra:
     1. **Body đúng format:** `{"additionalParams": {"CrossingLines": "[...]"}}` — API và worker chấp nhận cả `additionalParams` (camelCase) và `AdditionalParams` (PascalCase).
     2. **Tăng timeout phía client:** Ví dụ curl: `curl -X PATCH ... --max-time 15`.
     3. **Xem log server (API + worker):**
        - API: `[API] PATCH /v1/core/instance/{id} - Patch instance` rồi có thể chờ tới khi có `Success` hoặc `Update failed`.
        - Worker: `[Worker:uuid] UPDATE_INSTANCE received` → nếu line-only sẽ thấy `Line-only update: applying CrossingLines at runtime (no hot-swap)` và response gửi ngay; nếu thấy `Applying config changes to running pipeline` / `hot swap` thì worker có thể mất 5s+ (stop + build + start) → API trả về sau khi worker xong hoặc sau khi hết timeout (5s).
   - **Nếu worker đang xử lý request khác:** API nhận ngay lỗi "Worker is busy", trả 500 — vẫn là "có response". Gửi lại sau vài giây.
   - **Tăng timeout IPC (nếu cần):** `export IPC_START_STOP_TIMEOUT_MS=20000` (20s) rồi restart API, nếu PATCH thường xuyên đi nhánh hot-swap.

---

## Phân tích thuật toán – Đảm bảo runtime update (không restart)

### 1. Luồng tổng thể

```
API PUT/PATCH /v1/core/instance/{id}  (body: chỉ line hoặc full config)
  → InstanceHandler::updateInstance
  → SubprocessInstanceManager::updateInstance(id, configJson)
  → IPC UPDATE_INSTANCE { config }
  → Worker handleUpdateInstance(msg)
       oldConfig = config_
       mergeJsonInto(config_, msg.payload["config"])   // deep merge
       if !pipeline_running → return OK (chỉ lưu config)
       needsRebuild = checkIfNeedsRebuild(oldConfig, config_)
       canApplyRuntime = applyConfigToPipeline(oldConfig, config_)
       if needsRebuild || !canApplyRuntime → hot-swap hoặc rebuild
       else → return OK "Instance updated (runtime)"
```

### 2. Điều kiện để không rebuild (chỉ update runtime)

- **needsRebuild == false**  
  - Solution / SolutionId không đổi.  
  - Các model path (DETECTOR_*, WEIGHTS_PATH, CONFIG_PATH, …) không đổi.  
  - CrossingLines / CROSSLINE_* **không** được coi là “cần rebuild” (đã loại trừ trong `checkIfNeedsRebuild`).

- **canApplyRuntime == true**  
  - URL nguồn (RTSP/RTMP/FILE_PATH) không đổi → `sourceUrlChanged == false`.  
  - Nếu có thay đổi line → gọi `applyLinesFromParamsToPipeline(newParams)`; hàm này trả về `true` khi áp dụng line thành công (hoặc không có line / không có node line thì no-op vẫn `true`).  
  - Zone (nếu có) không đổi.  
  - Mọi param **không** thuộc nhóm “runtime line” và không phải URL được so sánh old vs new; **chỉ khi không có thay đổi** thì `changedParams` rỗng → không trigger rebuild.

### 3. Deep merge – Tránh rebuild nhầm

- **mergeJsonInto(target, source)** merge theo từng key, object merge đệ quy.  
- Request chỉ gửi `additionalParams.input` với `CROSSLINE_*` → chỉ nhánh `input` được cập nhật, các key khác (output, URL, Zone, …) giữ nguyên.  
- Sau merge, `getParamsFromConfig(config_)` so với `getParamsFromConfig(oldConfig)` **chỉ khác nhau ở line params**. Các key khác giữ nguyên → `sourceUrlChanged == false`, `changedParams` rỗng.

### 4. Nhóm key “chỉ áp dụng runtime” (không rebuild)

- Trong code: `runtimeLineKeys = { "CrossingLines", "CROSSLINE_START_X", "CROSSLINE_START_Y", "CROSSLINE_END_X", "CROSSLINE_END_Y" }`.  
- Các key này: không đưa vào `checkIfNeedsRebuild` (không yêu cầu rebuild), không đưa vào `changedParams` (không trigger rebuild trong `applyConfigToPipeline`).  
- Chỉ được áp dụng trong `applyLinesFromParamsToPipeline` → `ba_crossline_node->set_lines(lines)`.

### 5. Khi nào vẫn bị rebuild dù user chỉ muốn đổi line?

- Request gửi **kèm** Zone / URL / model / param khác (dù không đổi) nhưng **giá trị so với old khác** (format, type, key thừa) → `needsRebuild` hoặc `changedParams` có thể dương → rebuild.  
- **Zone** so sánh bằng `toStyledString()`; nếu request gửi Zone với format khác (thứ tự key, space) → coi là thay đổi → rebuild.  
- **applyLinesFromParamsToPipeline** trả về `false` (ví dụ không tìm thấy `ba_crossline_node`, hoặc `set_lines()` lỗi) → `canApplyRuntime == false` → rebuild/hot-swap.

### 6. Đảm bảo trong code (đã làm / nên giữ)

- Deep merge để partial update không xóa cả object.  
- `checkIfNeedsRebuild` **không** coi CrossingLines / CROSSLINE_* là lý do rebuild.  
- `applyConfigToPipeline`: so sánh param dùng giá trị chuẩn hóa (ví dụ so sánh dạng string) để tránh khác type (int vs string) gây `changedParams` nhầm.  
- Chỉ khi **(1) chỉ đổi line** và **(2) pipeline có ba_crossline_node** thì mới đảm bảo “cập nhật tại runtime, không restart”.

---

## Độ chắc chắn: Cập nhật runtime (không restart)

| Điều kiện | Độ chắc chắn | Ghi chú |
|-----------|---------------|--------|
| **Chỉ đổi line** (CrossingLines hoặc CROSSLINE_*), instance **ba_crossline**, request **không** kèm Zone/URL/param khác | **~85–90%** | Code path rõ: deep merge → chỉ line changed → `applyLinesFromParamsToPipeline` → `set_lines()`. Rủi ro: SDK `set_lines()` lỗi, body gửi thêm key (Zone, v.v.), hoặc so sánh params khác type. |
| Cùng điều kiện trên nhưng **đã chạy test thủ công thành công** (log có "Applied N line(s) at runtime (no restart)") | **~95%** | Còn phụ thuộc môi trường (SDK version, stream ổn định). |
| Đổi **bất kỳ thứ gì ngoài line** (URL, model, Zone, tham số khác) | **0% runtime** | Thiết kế hiện tại: các thay đổi đó luôn đi qua rebuild hoặc hot-swap, **không** áp dụng tại runtime. |

**Kết luận:** "Cập nhật tại runtime (không restart)" trong code **chỉ được đảm bảo** khi đồng thời thỏa hai điều kiện:

1. **Chỉ đổi line:** request chỉ thay đổi **CrossingLines** hoặc **CROSSLINE_START_X / CROSSLINE_START_Y / CROSSLINE_END_X / CROSSLINE_END_Y** (không gửi kèm Zone, URL, model, param khác).
2. **Instance là ba_crossline:** pipeline có node **ba_crossline_node** (solution hỗ trợ line).

Các thay đổi khác (URL, model, Zone, …) hoặc solution không có `ba_crossline_node` luôn đi qua rebuild/hot-swap.

**Từ 1 line lên 2 line (hoặc N line):** Có. Cùng một cơ chế runtime:
- **CrossingLines** (chuỗi JSON mảng): hỗ trợ 1, 2, 3, … N line; mỗi phần tử có `coordinates` (start/end). Đổi từ 1 line sang 2 line (thêm phần tử vào mảng) → gửi PATCH với `CrossingLines` mới → áp dụng tại runtime, không restart.
- **CROSSLINE_*** (legacy): chỉ 1 line. Muốn chuyển lên 2 line thì dùng **CrossingLines** với 2 phần tử trong request.

**Nếu PATCH chỉ line mà instance vẫn dừng:** Trước đây khi `set_lines()` thất bại (SDK lỗi hoặc format không hợp lệ), code trigger rebuild → pipeline bị stop. Đã sửa: khi **chỉ** thay đổi line mà áp dụng runtime thất bại thì **không** rebuild — config vẫn được merge và lưu, pipeline tiếp tục chạy với line cũ; line mới sẽ áp dụng khi instance restart. Log worker: "Failed to apply lines at runtime (config saved, will apply on next start)".

## Notes

- Hot-reload **chỉ áp dụng cho thay đổi line** (CrossingLines, CROSSLINE_START_X/Y, CROSSLINE_END_X/Y). Thay đổi source URL, model path, solution, zone... vẫn có thể trigger rebuild/hot-swap.
- API hỗ trợ cả **PATCH** và **PUT** (camelCase) để cập nhật instance; cần gửi đúng format `additionalParams` (và nếu có nested thì `input`/`output`).

---

## Related

- Instance API: `PUT/PATCH /v1/core/instance/{instanceId}`
- Lines API (alternative): endpoints `/lines` cho từng instance nếu có.
- Worker: `applyLinesFromParamsToPipeline`, `getParamsFromConfig` (runtime update không restart).
