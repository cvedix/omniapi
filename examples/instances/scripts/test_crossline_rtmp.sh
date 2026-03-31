#!/bin/bash

# Script test để khởi động instance với ba_crossline node và stream output ra RTMP
# Sử dụng: ./test_crossline_rtmp.sh [BASE_URL] [RTMP_URL]
# Mặc định: BASE_URL=http://localhost:8080, RTMP_URL=rtmp://localhost:1935/live/crossline_test

BASE_URL="${1:-http://localhost:8080}"
RTMP_URL="${2:-rtmp://localhost:1935/live/crossline_test}"
API_BASE="${BASE_URL}/v1/core"

echo "=========================================="
echo "OmniAPI - Test BA Crossline với RTMP Stream"
echo "=========================================="
echo "Base URL: ${BASE_URL}"
echo "RTMP URL: ${RTMP_URL}"
echo "Solution: ba_crossline_default"
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
        echo "  ${BLUE}./test_crossline_rtmp.sh http://localhost:8080${NC}"
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
    RESPONSE=$(eval "$1")
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

# ==========================================
# 1. Tạo instance với ba_crossline solution
# ==========================================
print_section "1. Tạo Instance với BA Crossline Solution"

echo "Tạo instance với solution ba_crossline_default..."
INSTANCE_RESPONSE=$(curl -s -X POST ${API_BASE}/instance \
  -H 'Content-Type: application/json' \
  -d '{
    "name": "crossline_test_instance",
    "group": "crossline_detection",
    "solution": "ba_crossline_default",
    "persistent": false,
    "autoStart": false,
    "frameRateLimit": 30,
    "metadataMode": true,
    "statisticsMode": false,
    "debugMode": false,
    "detectionSensitivity": "Medium",
    "additionalParams": {
      "RTSP_URL": "rtsp://localhost:8554/live/stream1",
      "RESIZE_RATIO": "1.0"
    }
  }')

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
    echo "Có thể do:"
    echo "  - Solution 'ba_crossline_default' không tồn tại"
    echo "  - Input source không hợp lệ"
    echo "  - Lỗi từ API server"
    echo ""
    echo "Để kiểm tra solutions có sẵn:"
    echo "  ${BLUE}curl ${API_BASE}/solution | jq '.'${NC}"
    exit 1
fi

INSTANCE_ID=$(extract_instance_id "$INSTANCE_RESPONSE")

if [ -z "$INSTANCE_ID" ] || [ "$INSTANCE_ID" = "null" ] || [ "$INSTANCE_ID" = "" ]; then
    echo -e "${RED}Lỗi: Không thể tạo instance. Response không chứa instanceId.${NC}"
    echo -e "${YELLOW}Response từ server:${NC}"
    echo "$INSTANCE_RESPONSE"
    echo ""
    echo "Có thể do:"
    echo "  - Solution 'ba_crossline_default' không tồn tại"
    echo "  - Input source không hợp lệ"
    echo "  - Lỗi từ API server"
    exit 1
fi

echo -e "${GREEN}✓ Instance đã được tạo: ${INSTANCE_ID}${NC}"
echo ""

# ==========================================
# 2. Thêm crossing lines
# ==========================================
print_section "2. Thêm Crossing Lines"

echo "Thêm crossing line vào instance..."
LINE_RESPONSE=$(curl -s -X POST ${API_BASE}/instance/${INSTANCE_ID}/lines \
  -H 'Content-Type: application/json' \
  -d '{
    "name": "Main Counting Line",
    "coordinates": [
      {"x": 0, "y": 250},
      {"x": 700, "y": 220}
    ],
    "direction": "Both",
    "classes": ["Vehicle"],
    "color": [255, 0, 0, 255]
  }')

if command -v jq >/dev/null 2>&1; then
    echo "$LINE_RESPONSE" | jq '.' 2>/dev/null || echo "$LINE_RESPONSE"
else
    echo "$LINE_RESPONSE"
fi
echo ""

echo -e "${GREEN}✓ Crossing line đã được thêm${NC}"
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

echo "Kiểm tra crossing lines..."
execute_cmd "curl -s -X GET ${API_BASE}/instance/${INSTANCE_ID}/lines | jq '.'"

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
RUNNING=$(echo "$STATUS_RESPONSE" | jq -r '.running // false' 2>/dev/null || echo "false")

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
echo "4. ${YELLOW}Kiểm tra crossing lines:${NC}"
if command -v jq >/dev/null 2>&1; then
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/lines | jq '.'${NC}"
else
    echo "   ${BLUE}curl -X GET ${API_BASE}/instance/${INSTANCE_ID}/lines${NC}"
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
echo "  ✓ Tạo instance với solution ba_crossline_default"
echo "  ✓ Thêm crossing line vào instance"
echo "  ✓ Cấu hình RTMP stream output"
echo "  ✓ Khởi động instance"
echo ""
echo -e "${BLUE}Instance ID: ${INSTANCE_ID}${NC}"
echo -e "${BLUE}RTMP Stream: ${RTMP_URL}${NC}"
echo ""
echo "Lưu ý:"
echo "  - Đảm bảo RTMP server (ví dụ: MediaMTX) đang chạy và lắng nghe trên ${RTMP_URL}"
echo "  - Đảm bảo input source (RTSP_URL hoặc FILE_PATH) hợp lệ trong additionalParams"
echo "  - Instance sẽ tự động restart khi cấu hình lines hoặc stream output thay đổi"
echo "  - BA Crossline đếm số lượng đối tượng đi qua đường line được định nghĩa"
echo ""
