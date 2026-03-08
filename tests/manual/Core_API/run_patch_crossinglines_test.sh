#!/usr/bin/env bash
# Test PATCH CrossingLines (line-only update) với log ghi ra file.
#
# Cách 1 - Script tự start API và ghi log vào file:
#   LOG_FILE=/tmp/edgeos-api.log ./run_patch_crossinglines_test.sh
#
# Cách 2 - Bạn start API trước, log ra file (để instance chạy ít in ra console):
#   EDGE_AI_EXECUTION_MODE=subprocess ./build/bin/edgeos-api >> /tmp/edgeos-api.log 2>&1
#   Rồi chạy test (không start lại API):
#   START_SERVER=0 LOG_FILE=/tmp/edgeos-api.log ./run_patch_crossinglines_test.sh
#
# Chạy từ repo root hoặc từ build/: LOG_FILE và API URL có thể tùy chỉnh.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build}"
LOG_FILE="${LOG_FILE:-/tmp/edgeos-hotreload-test.log}"
SERVER="${SERVER:-http://localhost:8080}"
BASE_URL="${SERVER}/v1/core/instance"
START_SERVER="${START_SERVER:-1}"   # 1 = start API trong script; 0 = API đã chạy sẵn, chỉ cần LOG_FILE trỏ tới log

# Binary (từ build)
if [[ -x "${BUILD_DIR}/bin/edgeos-api" ]]; then
  API_BIN="${BUILD_DIR}/bin/edgeos-api"
else
  API_BIN=""
fi

cleanup() {
  if [[ -n "${API_PID}" ]] && kill -0 "${API_PID}" 2>/dev/null; then
    echo "Stopping API (PID ${API_PID})..."
    kill "${API_PID}" 2>/dev/null || true
    wait "${API_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

echo "=== PATCH CrossingLines test (log -> ${LOG_FILE}) ==="

if [[ "${START_SERVER}" = "1" ]] && [[ -n "${API_BIN}" ]]; then
  echo "Starting API with log to ${LOG_FILE} (subprocess mode for worker line-only test) ..."
  : > "${LOG_FILE}"
  EDGE_AI_EXECUTION_MODE=subprocess "${API_BIN}" >> "${LOG_FILE}" 2>&1 &
  API_PID=$!
  echo "API PID: ${API_PID}"
  # Subprocess mode can load many instances and spawn workers — wait up to 60s
  for i in $(seq 1 60); do
    if curl -s -o /dev/null -w "%{http_code}" "${SERVER}/v1/core/health" | grep -q 200; then
      echo "API ready."
      break
    fi
    if ! kill -0 "${API_PID}" 2>/dev/null; then
      echo "API process exited. Last lines of log:"
      tail -50 "${LOG_FILE}"
      exit 1
    fi
    sleep 1
  done
  if ! curl -s -o /dev/null -w "%{http_code}" "${SERVER}/v1/core/health" | grep -q 200; then
    echo "API did not become ready. Log:"
    tail -100 "${LOG_FILE}"
    exit 1
  fi
elif [[ "${START_SERVER}" = "1" ]]; then
  echo "Build not found at ${BUILD_DIR}/bin/edgeos-api. Set BUILD_DIR or run API manually with log to file:"
  echo "  ${REPO_ROOT}/build/bin/edgeos-api >> ${LOG_FILE} 2>&1"
  exit 1
else
  echo "Assuming API is already running at ${SERVER}. Log file (if server was started with redirect): ${LOG_FILE}"
  API_PID=""
fi

# Tạo instance ba_crossline (dùng name để dễ xóa sau)
INSTANCE_NAME="ba_crossline_patch_test_$$"
echo "Creating instance: ${INSTANCE_NAME}"
CREATE_RESP=$(curl -s -X POST "${BASE_URL}" \
  -H "Content-Type: application/json" \
  -d "{
    \"name\": \"${INSTANCE_NAME}\",
    \"group\": \"test\",
    \"solution\": \"ba_crossline\",
    \"persistent\": false,
    \"autoStart\": false,
    \"detectionSensitivity\": \"Medium\",
    \"additionalParams\": {
      \"input\": {
        \"RTMP_SRC_URL\": \"rtmp://192.168.1.128:1935/live/camera_demo_sang_vehicle\",
        \"WEIGHTS_PATH\": \"/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721_best.weights\",
        \"CONFIG_PATH\": \"/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721.cfg\",
        \"LABELS_PATH\": \"/opt/edgeos-api/models/det_cls/yolov3_tiny_5classes.txt\",
        \"CROSSLINE_START_X\": \"0\",
        \"CROSSLINE_START_Y\": \"250\",
        \"CROSSLINE_END_X\": \"700\",
        \"CROSSLINE_END_Y\": \"220\",
        \"RESIZE_RATIO\": \"1.0\",
        \"GST_DECODER_NAME\": \"avdec_h264\",
        \"SKIP_INTERVAL\": \"0\",
        \"CODEC_TYPE\": \"h264\"
      },
      \"output\": {
        \"RTMP_DES_URL\": \"rtmp://192.168.1.128:1935/live/ba_crossing_stream_test_$$\",
        \"ENABLE_SCREEN_DES\": \"false\"
      }
    }
  }")
INSTANCE_ID=$(echo "${CREATE_RESP}" | jq -r '.instanceId // .data.instanceId // empty')
if [[ -z "${INSTANCE_ID}" ]]; then
  echo "Create failed. Response: ${CREATE_RESP}"
  exit 1
fi
echo "Instance ID: ${INSTANCE_ID}"

# Đợi building xong (nếu có)
for i in {1..60}; do
  STATUS=$(curl -s "${BASE_URL}/${INSTANCE_ID}" | jq -r '.status // .building // empty')
  if [[ "${STATUS}" = "ready" ]] || [[ "${STATUS}" = "false" ]]; then
    break
  fi
  sleep 1
done

echo "Starting instance..."
curl -s -X POST "${BASE_URL}/${INSTANCE_ID}/start" -H "Content-Type: application/json" | jq . || true
sleep 3

# PATCH chỉ CrossingLines (line-only)
PATCH_BODY='{
  "additionalParams": {
    "CrossingLines": "[{\"id\":\"line1\",\"name\":\"Entry Line\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Up\",\"classes\":[\"Vehicle\",\"Person\"],\"color\":[255,0,0,255]},{\"id\":\"line2\",\"name\":\"Exit Line\",\"coordinates\":[{\"x\":100,\"y\":400},{\"x\":800,\"y\":380}],\"direction\":\"Down\",\"classes\":[\"Vehicle\"],\"color\":[0,255,0,255]}]"
  }
}'
echo "Sending PATCH (CrossingLines only)..."
PATCH_START=$(date +%s)
PATCH_RESP=$(curl -s -w "\n%{http_code}" -X PATCH "${BASE_URL}/${INSTANCE_ID}" \
  -H "Content-Type: application/json" \
  -d "${PATCH_BODY}")
PATCH_END=$(date +%s)
HTTP_BODY=$(echo "${PATCH_RESP}" | head -n -1)
HTTP_CODE=$(echo "${PATCH_RESP}" | tail -n 1)
echo "PATCH HTTP status: ${HTTP_CODE}"
echo "PATCH response body: ${HTTP_BODY}"
echo "PATCH duration: $((PATCH_END - PATCH_START))s"

# Kiểm tra log (nếu có file)
if [[ -f "${LOG_FILE}" ]]; then
  if grep -q "Line-only update: applying CrossingLines at runtime (no hot-swap)" "${LOG_FILE}"; then
    echo "PASS: Log contains 'Line-only update' (runtime update, no hot-swap)."
  else
    echo "CHECK: Log may not contain 'Line-only update' (search in ${LOG_FILE})."
  fi
  if grep -q "Atomic pipeline swap\|hot swap\|hot-swap pipeline" "${LOG_FILE}"; then
    echo "WARN: Log contains hot swap / atomic swap (line-only PATCH should NOT trigger this)."
  else
    echo "PASS: No hot swap in log for this run."
  fi
  echo "Log file: ${LOG_FILE} (last 20 lines below)"
  tail -20 "${LOG_FILE}"
else
  echo "Log file not found (${LOG_FILE}). Start API with: ... >> ${LOG_FILE} 2>&1"
fi

# Kết quả
if [[ "${HTTP_CODE}" = "200" ]]; then
  echo "PATCH test: HTTP 200 OK."
else
  echo "PATCH test: HTTP ${HTTP_CODE} (expected 200)."
  exit 1
fi

echo "Done. Instance ${INSTANCE_ID} left running; stop with: curl -X POST ${BASE_URL}/${INSTANCE_ID}/stop"
