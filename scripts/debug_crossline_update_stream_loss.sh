#!/bin/bash
# Reproduce: create ba_crossline instance with RTMP output, start, then PATCH only CrossingLines (2 lines).
# Expected: line-only update (no hot-swap), stream stays. If stream is lost, debug logs in .cursor/debug-408f41.log.
set -e
BASE_URL="${1:-http://localhost:8080}"
API_BASE="${BASE_URL}/v1/core"
INSTANCE_NAME="ba_crossline_rtsp_rtmp_test"

echo "=== 1. Create instance (ba_crossline, RTMP output, CROSSLINE_* in input) ==="
CREATE_RESP=$(curl -s -X POST "${API_BASE}/instance" -H 'Content-Type: application/json' -d '{
  "name": "ba_crossline_rtsp_rtmp_test",
  "group": "test",
  "solution": "ba_crossline",
  "persistent": false,
  "autoStart": false,
  "detectionSensitivity": "Medium",
  "additionalParams": {
    "input": {
      "RTMP_SRC_URL": "rtmp://192.168.1.128:1935/live/camera_demo_sang_vehicle",
      "WEIGHTS_PATH": "/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721_best.weights",
      "CONFIG_PATH": "/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721.cfg",
      "LABELS_PATH": "/opt/edgeos-api/models/det_cls/yolov3_tiny_5classes.txt",
      "CROSSLINE_START_X": "0",
      "CROSSLINE_START_Y": "250",
      "CROSSLINE_END_X": "700",
      "CROSSLINE_END_Y": "220",
      "RESIZE_RATIO": "1.0",
      "GST_DECODER_NAME": "avdec_h264",
      "SKIP_INTERVAL": "0",
      "CODEC_TYPE": "h264"
    },
    "output": {
      "RTMP_DES_URL": "rtmp://192.168.1.128:1935/live/ba_crossing_stream_1",
      "ENABLE_SCREEN_DES": "false"
    }
  }
}')
echo "$CREATE_RESP" | jq '.' 2>/dev/null || echo "$CREATE_RESP"
INSTANCE_ID=$(echo "$CREATE_RESP" | jq -r '.instanceId // empty')
if [ -z "$INSTANCE_ID" ] || [ "$INSTANCE_ID" = "null" ]; then
  echo "Failed to get instanceId"
  exit 1
fi
echo "InstanceId: $INSTANCE_ID"

echo ""
echo "=== 2. Start instance ==="
curl -s -X POST "${API_BASE}/instance/${INSTANCE_ID}/start" -H 'Content-Type: application/json' | jq '.' 2>/dev/null || true
sleep 3
curl -s -X GET "${API_BASE}/instance/${INSTANCE_ID}" | jq '{instanceId, running, fps}' 2>/dev/null || true

echo ""
echo "=== 3. PATCH: update only CrossingLines (2 lines) - no other params ==="
PATCH_RESP=$(curl -s -X PATCH "${API_BASE}/instance/${INSTANCE_ID}" -H 'Content-Type: application/json' -d '{
  "additionalParams": {
    "CrossingLines": "[{\"id\":\"line1\",\"name\":\"Entry Line\",\"coordinates\":[{\"x\":0,\"y\":250},{\"x\":700,\"y\":220}],\"direction\":\"Up\",\"classes\":[\"Vehicle\",\"Person\"],\"color\":[255,0,0,255]},{\"id\":\"line2\",\"name\":\"Exit Line\",\"coordinates\":[{\"x\":100,\"y\":400},{\"x\":800,\"y\":380}],\"direction\":\"Down\",\"classes\":[\"Vehicle\"],\"color\":[0,255,0,255]}]"
  }
}')
echo "$PATCH_RESP" | jq '.' 2>/dev/null || echo "$PATCH_RESP"
echo "PATCH completed. Check RTMP stream - if lost, see .cursor/debug-408f41.log"
echo ""
echo "=== 4. Instance status after PATCH ==="
curl -s -X GET "${API_BASE}/instance/${INSTANCE_ID}" | jq '{instanceId, running, fps}' 2>/dev/null || true
