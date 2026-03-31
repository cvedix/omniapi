#!/bin/bash

# Script test để khởi động instance với jam node và stream output ra RTMP
# Sử dụng: ./test_jam_rtmp.sh [BASE_URL] [RTMP_URL] [SOLUTION_ID]
# Mặc định: BASE_URL=http://localhost:8080, RTMP_URL=rtmp://localhost:1935/live/jam_test, SOLUTION_ID=ba_jam_default

BASE_URL="${1:-http://localhost:8080}"
RTMP_URL="${2:-rtmp://localhost:1935/live/jam_test}"
SOLUTION_ID="${3:-ba_jam_default}"
API_BASE="${BASE_URL}/v1/core"

echo "=========================================="
echo "OmniAPI - Test Jam Node với RTMP Stream"
echo "=========================================="
echo "Base URL: ${BASE_URL}"
echo "RTMP URL: ${RTMP_URL}"
echo "Solution: ${SOLUTION_ID}"
echo ""

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Function to check if API server is running
check_api_server() {
    echo "Đang kiểm tra kết nối đến API server..."
    if ! curl -s --connect-timeout 2 "${BASE_URL}/v1/core/instance/status/summary" >/dev/null 2>&1; then
        echo -e "${RED}Lỗi: Không thể kết nối đến API server tại ${BASE_URL}${NC}"
        echo ""
        echo "Có thể do:"
        echo "  - API server chưa được khởi động"
        echo "  - Sai port (thử: http://localhost:8080 hoặc http://localhost:8848)"
        echo "  - Firewall chặn kết nối"
        echo ""
        echo "Để kiểm tra port đang sử dụng:"
        echo "  ${BLUE}netstat -tlnp | grep -E '8080|8848'${NC}"
        echo ""
        echo "Hoặc thử với port khác:"
        echo "  ${BLUE}./test_jam_rtmp.sh http://localhost:8080${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ Kết nối đến API server thành công${NC}"
    echo ""
}

# Check API server
check_api_server

# Function to print section header
print_section() {
    echo ""
    echo -e "${BLUE}=== $1 ===${NC}"
    echo ""
}

# Function to print command
print_cmd() {
    echo -e "${YELLOW}Command:${NC} $1"
    echo ""
}

# Function to execute and show response
execute_cmd() {
    print_cmd "$1"
    # If jq is not available and the command contains a jq pipe, strip it
    CMD="$1"
    if ! command -v jq >/dev/null 2>&1; then
        # Remove pipe to jq and any subsequent jq arguments
        CMD=$(echo "$1" | sed -E 's/\|[[:space:]]*jq.*$//')
    fi

    RESPONSE=$(eval "$CMD" 2>&1)
    if command -v jq >/dev/null 2>&1; then
        echo "$RESPONSE" | jq '.' 2>/dev/null || echo "$RESPONSE"
    else
        echo "$RESPONSE"
    fi
    echo ""
    echo "---"
    sleep 1
}

# Function to extract instance ID from response
extract_instance_id() {
    # Try with jq first
    if command -v jq >/dev/null 2>&1; then
        echo "$1" | jq -r '.instanceId // empty' 2>/dev/null
    else
        # Fallback: extract using grep/sed
        echo "$1" | grep -o '"instanceId"[[:space:]]*:[[:space:]]*"[^"]*"' | sed 's/.*"instanceId"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/' | head -1
    fi
}

# Function to check if API server is running
check_api_server() {
    if ! curl -s --connect-timeout 2 "${BASE_URL}/v1/core/instance/status/summary" >/dev/null 2>&1; then
        echo -e "${RED}Lỗi: Không thể kết nối đến API server tại ${BASE_URL}${NC}"
        echo "Vui lòng đảm bảo OmniAPI server đang chạy."
        exit 1
    fi
}

# ==========================================
# 1. Tạo instance với ba_jam solution
# ==========================================
print_section "1. Tạo Instance với BA Jam Solution"

echo "Tạo instance với solution ${SOLUTION_ID}..."
INSTANCE_RESPONSE=$(curl -s -X POST ${API_BASE}/instance \
  -H 'Content-Type: application/json' \
  -d "{
    \"name\": \"jam_test_instance\",
    \"group\": \"jam_detection\",
    \"solution\": \"${SOLUTION_ID}\",
    \"persistent\": false,
    \"autoStart\": false,
    \"frameRateLimit\": 30,
    \"metadataMode\": true,
    \"statisticsMode\": false,
    \"debugMode\": false,
    \"detectionSensitivity\": \"Medium\",
    \"additionalParams\": {
      \"RTSP_URL\": \"rtsp://localhost:8554/live/stream1\",
      \"RESIZE_RATIO\": \"1.0\"
    }
  }")

if command -v jq >/dev/null 2>&1; then
    echo "$INSTANCE_RESPONSE" | jq '.' 2>/dev/null || echo "$INSTANCE_RESPONSE"
else
    echo "$INSTANCE_RESPONSE"
fi
echo ""

# Check for error in response
if echo "$INSTANCE_RESPONSE" | grep -q '"error"'; then
    echo -e "${RED}Lỗi từ API server:${NC}"
    ERROR_MSG=""
    if command -v jq >/dev/null 2>&1; then
        ERROR_MSG=$(echo "$INSTANCE_RESPONSE" | jq -r '.message // .error // "Unknown error"' 2>/dev/null)
    else
        ERROR_MSG=$(echo "$INSTANCE_RESPONSE" | grep -o '"message"[^}]*' | head -1)
    fi
    echo "$ERROR_MSG"
    echo ""
    
    # Check if it's "Unknown node type: ba_jam" error
    if echo "$ERROR_MSG" | grep -q "Unknown node type.*ba_jam"; then
        echo -e "${YELLOW}⚠ Lưu ý: Node type 'ba_jam' có thể chưa được build trong SDK.${NC}"
        echo ""
        echo "Giải pháp:"
        echo "  1. Kiểm tra xem SDK có hỗ trợ ba_jam node không"
        echo "  2. Hoặc sử dụng solution 'ba_crossline_default' thay thế:"
        echo "     ${BLUE}./test_jam_rtmp.sh http://localhost:8080 rtmp://localhost:1935/live/jam_test ba_crossline_default${NC}"
        echo ""
        echo "Để test với ba_crossline (tương tự jam detection):"
        echo "  ${BLUE}curl -X POST ${API_BASE}/instance -H 'Content-Type: application/json' -d '{\"name\":\"crossline_test\",\"solution\":\"ba_crossline_default\",\"additionalParams\":{\"RTSP_URL\":\"rtsp://localhost:8554/live/stream1\",\"RESIZE_RATIO\":\"1.0\"}}'${NC}"
    fi
    exit 1
fi

INSTANCE_ID=$(extract_instance_id "$INSTANCE_RESPONSE")

if [ -z "$INSTANCE_ID" ] || [ "$INSTANCE_ID" = "null" ] || [ "$INSTANCE_ID" = "" ]; then
    echo -e "${RED}Lỗi: Không thể tạo instance. Response không chứa instanceId.${NC}"
    echo -e "${YELLOW}Response từ server:${NC}"
    echo "$INSTANCE_RESPONSE"
    echo ""
    echo "Có thể do:"
    echo "  - Solution '${SOLUTION_ID}' không tồn tại"
    echo "  - Input source không hợp lệ"
    echo "  - Lỗi từ API server"
    echo ""
    echo "Để kiểm tra solutions có sẵn:"
    echo "  ${BLUE}curl ${API_BASE}/solution | jq '.'${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Instance đã được tạo: ${INSTANCE_ID}${NC}"
echo ""

# ==========================================
# 2. Thêm jam zones
# ==========================================
print_section "2. Thêm Jam Zones"

echo "Thêm jam zone vào instance..."
JAM_RESPONSE=$(curl -s -X POST ${API_BASE}/instance/${INSTANCE_ID}/jams \
  -H 'Content-Type: application/json' \
  -d '{
    "name": "Downtown Jam Zone",
    "roi": [
      {"x": 0, "y": 100},
      {"x": 1920, "y": 100},
      {"x": 1920, "y": 400},
      {"x": 0, "y": 400}
    ],
    "enabled": true,
    "check_interval_frames": 20,
    "check_min_hit_frames": 50,
    "check_max_distance": 8,
    "check_min_stops": 8,
    "check_notify_interval": 10
  }')

if command -v jq >/dev/null 2>&1; then
    echo "$JAM_RESPONSE" | jq '.' 2>/dev/null || echo "$JAM_RESPONSE"
else
    echo "$JAM_RESPONSE"
fi
echo ""

echo -e "${GREEN}✓ Jam zone đã được thêm${NC}"
echo ""

# ==========================================
# 3. Cấu hình RTMP stream output
# ==========================================
print_section "3. Cấu Hình RTMP Stream Output"

echo "Cấu hình stream output ra RTMP: ${RTMP_URL}..."
STREAM_RESPONSE=$(curl -s -X POST ${API_BASE}/instance/${INSTANCE_ID}/output/stream \
  -H 'Content-Type: application/json' \
  -d "{
    \"enabled\": true,
    \"uri\": \"${RTMP_URL}\"
  }")

if command -v jq >/dev/null 2>&1; then
    echo "$STREAM_RESPONSE" | jq '.' 2>/dev/null || echo "$STREAM_RESPONSE"
else
    echo "$STREAM_RESPONSE"
fi
echo ""

echo -e "${GREEN}✓ RTMP stream output đã được cấu hình${NC}"
echo ""

# ==========================================
# 4. Kiểm tra cấu hình trước khi start
# ==========================================
print_section "4. Kiểm Tra Cấu Hình Instance"

echo "Lấy thông tin instance..."
execute_cmd "curl -s -X GET ${API_BASE}/instance/${INSTANCE_ID} | jq '{instanceId, displayName, solutionId, running, additionalParams}'"

echo "Kiểm tra jam zones..."
execute_cmd "curl -s -X GET ${API_BASE}/instance/${INSTANCE_ID}/jams | jq '.'"

echo "Kiểm tra stream output config..."
execute_cmd "curl -s -X GET ${API_BASE}/instance/${INSTANCE_ID}/output/stream | jq '.'"

# ==========================================
# 5. Start instance
# ==========================================
print_section "5. Khởi Động Instance"

echo "Đang khởi động instance (có thể mất vài giây)..."
START_RESPONSE=$(curl -s -X POST ${API_BASE}/instance/${INSTANCE_ID}/start \
  -H 'Content-Type: application/json')

if command -v jq >/dev/null 2>&1; then
    echo "$START_RESPONSE" | jq '.' 2>/dev/null || echo "$START_RESPONSE"
else
    echo "$START_RESPONSE"
fi
echo ""

# Đợi một chút để instance khởi động
echo "Đợi 3 giây để instance khởi động..."
sleep 3

# Kiểm tra trạng thái
echo "Kiểm tra trạng thái instance..."
STATUS_RESPONSE=$(curl -s -X GET ${API_BASE}/instance/${INSTANCE_ID})
RUNNING=$(echo "$STATUS_RESPONSE" | jq -r '.running // false' 2>/dev/null)

if [ "$RUNNING" = "true" ]; then
    echo -e "${GREEN}✓ Instance đã được khởi động thành công!${NC}"
else
    echo -e "${YELLOW}⚠ Instance có thể chưa khởi động hoàn toàn. Kiểm tra lại sau vài giây.${NC}"
fi

echo ""
if command -v jq >/dev/null 2>&1; then
    echo "$STATUS_RESPONSE" | jq '{instanceId, displayName, solutionId, running, fps}' 2>/dev/null || echo "$STATUS_RESPONSE"
else
    echo "$STATUS_RESPONSE"
fi
echo ""

# ==========================================
# 7. TỰ ĐỘNG: Kiểm tra RTSP → RTMP và fallback
# ==========================================
print_section "7. Tự động: Kiểm tra RTSP → RTMP và fallback"

echo "Bật debug/statistics và ép RTSP transport -> TCP + USE_URISOURCEBIN..."
UPDATE_RESPONSE=$(curl -s -X PUT ${API_BASE}/instance/${INSTANCE_ID} \
  -H 'Content-Type: application/json' \
  -d '{
    "debugMode": true,
    "statisticsMode": true,
    "additionalParams": {
      "RTSP_TRANSPORT": "TCP",
      "USE_URISOURCEBIN": "true",
      "GST_DECODER_NAME": "decodebin"
    }
  }')

if command -v jq >/dev/null 2>&1; then
    echo "$UPDATE_RESPONSE" | jq '.' 2>/dev/null || echo "$UPDATE_RESPONSE"
else
    echo "$UPDATE_RESPONSE"
fi

# Restart instance to apply settings (stop/start)
echo "Restarting instance to apply settings..."
curl -s -X POST ${API_BASE}/instance/${INSTANCE_ID}/stop >/dev/null 2>&1 || true
sleep 1
curl -s -X POST ${API_BASE}/instance/${INSTANCE_ID}/start >/dev/null 2>&1 || true
sleep 2

# Helper: get frames_processed from statistics
get_frames_processed() {
  S=$(curl -s ${API_BASE}/instance/${INSTANCE_ID}/statistics)
  # If response empty, return 0
  if [ -z "$S" ]; then
    echo 0
    return
  fi
  if command -v jq >/dev/null 2>&1; then
    echo "$S" | jq -r '.frames_processed // 0' 2>/dev/null || echo 0
  else
    echo "$S" | grep -o '"frames_processed"[[:space:]]*:[[:space:]]*[0-9]*' | sed 's/[^0-9]*//g' | head -1 || echo 0
  fi
}

# Poll statistics for frames_processed > 0
MAX_TRIES=20
SLEEP_SEC=2
SUCCESS=0
frames=0
echo "Polling statistics up to $(($MAX_TRIES * $SLEEP_SEC))s for frames_processed>0..."
for i in $(seq 1 $MAX_TRIES); do
  frames=$(get_frames_processed)
  echo "Attempt $i: frames_processed=${frames}"
  if [ "$frames" -gt 0 ]; then
    SUCCESS=1
    break
  fi
  sleep $SLEEP_SEC
done

if [ "$SUCCESS" -eq 1 ]; then
  echo -e "${GREEN}✓ Instance đang xử lý khung hình (frames_processed=${frames})${NC}"
else
  echo -e "${YELLOW}⚠ Không nhận được frames; chuyển sang fallback FILE input để kiểm tra${NC}"

  if ! command -v ffmpeg >/dev/null 2>&1; then
    echo -e "${RED}ffmpeg không tìm thấy; cài đặt ffmpeg rồi chạy lại script để thử fallback${NC}"
  else
    TEST_FILE="/tmp/test_jam_input.mp4"
    echo "Tạo file test: ${TEST_FILE} (30s, test pattern)"
    ffmpeg -y -f lavfi -i testsrc=size=640x480:rate=30 -f lavfi -i sine=frequency=1000 -c:v libx264 -pix_fmt yuv420p -c:a aac -t 30 "${TEST_FILE}" >/dev/null 2>&1 || true

    echo "Xóa instance hiện tại và tạo mới sử dụng FILE PATH=${TEST_FILE} để cách ly lỗi"

    # Delete current instance
    curl -s -X DELETE ${API_BASE}/instance/${INSTANCE_ID} >/dev/null 2>&1 || true
    sleep 1

    # Create a fresh instance using FILE_PATH
    echo "Tạo instance mới với FILE_PATH=${TEST_FILE}..."
    NEW_INSTANCE_RESPONSE=$(curl -s -X POST ${API_BASE}/instance \
      -H 'Content-Type: application/json' \
      -d "{
        \"name\": \"jam_test_instance_file\",
        \"group\": \"jam_detection\",
        \"solution\": \"${SOLUTION_ID}\",
        \"persistent\": false,
        \"autoStart\": false,
        \"frameRateLimit\": 30,
        \"metadataMode\": true,
        \"statisticsMode\": true,
        \"debugMode\": true,
        \"detectionSensitivity\": \"Medium\",
        \"additionalParams\": {
          \"FILE_PATH\": \"${TEST_FILE}\",
          \"RESIZE_RATIO\": \"1.0\",
          \"USE_URISOURCEBIN\": \"true\",
          \"GST_DECODER_NAME\": \"decodebin\"
        }
      }")

    if command -v jq >/dev/null 2>&1; then
      echo "$NEW_INSTANCE_RESPONSE" | jq '.' 2>/dev/null || echo "$NEW_INSTANCE_RESPONSE"
    else
      echo "$NEW_INSTANCE_RESPONSE"
    fi

    NEW_INSTANCE_ID=$(extract_instance_id "$NEW_INSTANCE_RESPONSE")
    if [ -z "$NEW_INSTANCE_ID" ]; then
      echo -e "${RED}✗ Không thể tạo instance mới với FILE_PATH; cần debug thêm${NC}"
    else
      echo -e "${GREEN}✓ Instance mới tạo: ${NEW_INSTANCE_ID}${NC}"

      # Add jam zone
      echo "Thêm jam zone vào instance mới..."
      curl -s -X POST ${API_BASE}/instance/${NEW_INSTANCE_ID}/jams \
        -H 'Content-Type: application/json' \
        -d '{
          "name": "Downtown Jam Zone",
          "roi": [ {"x":0,"y":100},{"x":1920,"y":100},{"x":1920,"y":400},{"x":0,"y":400} ],
          "enabled": true,
          "check_interval_frames": 20,
          "check_min_hit_frames": 50,
          "check_max_distance": 8,
          "check_min_stops": 8,
          "check_notify_interval": 10
        }' >/dev/null 2>&1 || true

      # Configure RTMP output for new instance
      echo "Cấu hình RTMP output cho instance mới..."
      curl -s -X POST ${API_BASE}/instance/${NEW_INSTANCE_ID}/output/stream \
        -H 'Content-Type: application/json' \
        -d "{ \"enabled\": true, \"uri\": \"${RTMP_URL}\" }" >/dev/null 2>&1 || true

      # Start the new instance
      echo "Khởi động instance mới..."
      curl -s -X POST ${API_BASE}/instance/${NEW_INSTANCE_ID}/start >/dev/null 2>&1 || true
      sleep 2

      # Update INSTANCE_ID to new one for polling
      INSTANCE_ID="$NEW_INSTANCE_ID"

      # Poll stats for new instance
      for i in $(seq 1 $MAX_TRIES); do
        frames=$(get_frames_processed)
        echo "Fallback Instance Attempt $i: frames_processed=${frames}"
        if [ "$frames" -gt 0 ]; then
          SUCCESS=1
          break
        fi
        sleep $SLEEP_SEC
      done

      if [ "$SUCCESS" -eq 1 ]; then
        echo -e "${GREEN}✓ Instance mới với FILE input đã xử lý khung hình${NC}"
      else
        echo -e "${RED}✗ Instance mới với FILE input cũng không tạo frames; cần debug thêm (xem logs)${NC}"
      fi
    fi
  fi
fi

# Nếu đã xử lý khung hình, kiểm tra RTMP playback (ffprobe) nếu có
if [ "$SUCCESS" -eq 1 ]; then
  echo "Kiểm tra RTMP playback: ${RTMP_URL}"
  if command -v ffprobe >/dev/null 2>&1; then
    timeout 8 ffprobe -v error -show_streams "${RTMP_URL}" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
      echo -e "${GREEN}✓ RTMP stream có vẻ sẵn sàng tại ${RTMP_URL}${NC}"
    else
      echo -e "${YELLOW}⚠ Không thể truy cập RTMP hoặc stream chưa sẵn sàng. Kiểm tra /tmp/mediamtx.log${NC}"
      echo "Bạn có thể chạy: ${BLUE}ffplay ${RTMP_URL}${NC} để xem chi tiết hoặc xem log mediamtx" 
    fi
  else
    echo -e "${YELLOW}ffprobe không tìm thấy; để kiểm tra thủ công, chạy: ffplay ${RTMP_URL}${NC}"
  fi
fi

# End automatic check
print_section "Hoàn tất kiểm tra tự động"

# ==========================================
# 6. Hướng dẫn kiểm tra kết quả
# ==========================================
print_section "6. Hướng Dẫn Kiểm Tra Kết Quả"

echo -e "${GREEN}Instance đã được cấu hình và khởi động!${NC}"
echo ""
echo "Để kiểm tra kết quả xử lý:"
echo ""
echo "1. ${YELLOW}Kiểm tra RTMP stream:${NC}"
echo "   - Sử dụng VLC hoặc ffplay để xem stream:"
echo "     ${BLUE}ffplay ${RTMP_URL}${NC}"
echo "   - Hoặc với VLC: File > Open Network Stream > ${RTMP_URL}"
echo ""
echo "2. ${YELLOW}Kiểm tra output/processing results:${NC}"
if command -v jq >/dev/null 2>&1; then
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/output | jq '.'${NC}"
else
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/output${NC}"
fi
echo ""
echo "3. ${YELLOW}Kiểm tra statistics:${NC}"
if command -v jq >/dev/null 2>&1; then
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/statistics | jq '.'${NC}"
else
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/statistics${NC}"
fi
echo ""
echo "4. ${YELLOW}Kiểm tra jam zones:${NC}"
if command -v jq >/dev/null 2>&1; then
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/jams | jq '.'${NC}"
else
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/jams${NC}"
fi
echo ""
echo "5. ${YELLOW}Kiểm tra trạng thái instance:${NC}"
if command -v jq >/dev/null 2>&1; then
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID} | jq '.'${NC}"
else
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}${NC}"
fi
echo ""
echo "6. ${YELLOW}Dừng instance khi hoàn thành:${NC}"
echo "   ${BLUE}curl -X POST ${API_BASE}/instance/${INSTANCE_ID}/stop${NC}"
echo ""
echo "7. ${YELLOW}Xóa instance:${NC}"
echo "   ${BLUE}curl -X DELETE ${API_BASE}/instance/${INSTANCE_ID}${NC}"
echo ""

# ==========================================
# Tổng kết
# ==========================================
print_section "Tổng Kết"

echo -e "${GREEN}Đã hoàn thành các bước sau:${NC}"
echo "  ✓ Tạo instance với solution ${SOLUTION_ID}"
echo "  ✓ Thêm jam zone vào instance"
echo "  ✓ Cấu hình RTMP stream output"
echo "  ✓ Khởi động instance"
echo ""
echo -e "${BLUE}Instance ID: ${INSTANCE_ID}${NC}"
echo -e "${BLUE}RTMP Stream: ${RTMP_URL}${NC}"
echo ""
echo "Lưu ý:"
echo "  - Đảm bảo RTMP server (ví dụ: MediaMTX) đang chạy và lắng nghe trên ${RTMP_URL}"
echo "  - Đảm bảo input source (RTSP_URL hoặc FILE_PATH) hợp lệ trong additionalParams"
echo "  - Instance sẽ tự động restart khi cấu hình jam zones hoặc stream output thay đổi"
echo ""
