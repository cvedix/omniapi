#!/bin/bash

# Script để kiểm tra nguyên nhân lỗi "open file failed" khi start instance xử lý video
# Usage: ./debug_file_source.sh <instance_id>

INSTANCE_ID="${1}"

# Nếu không có instance ID, tự động detect từ logs "open file failed"
if [ -z "$INSTANCE_ID" ]; then
    echo "Không có instance ID được cung cấp, đang tìm trong logs..."
    if command -v journalctl >/dev/null 2>&1; then
        DETECTED_INSTANCE_ID=$(journalctl -u edgeos-api --since "1 hour ago" --no-pager 2>/dev/null | \
            grep -i "open file failed" | \
            grep -oE "file_src_[a-f0-9-]+" | \
            sed 's/file_src_//' | \
            sort -u | \
            head -1)
        
        if [ ! -z "$DETECTED_INSTANCE_ID" ]; then
            INSTANCE_ID="$DETECTED_INSTANCE_ID"
            echo "✓ Tìm thấy instance ID từ logs: $INSTANCE_ID"
        else
            echo "⚠ Không tìm thấy instance ID trong logs"
            echo "   Sử dụng: $0 <instance_id>"
            echo "   Hoặc instance ID sẽ được detect tự động nếu có lỗi 'open file failed'"
            INSTANCE_ID=""
        fi
    else
        echo "⚠ Không tìm thấy journalctl, không thể auto-detect instance ID"
        echo "   Sử dụng: $0 <instance_id>"
        INSTANCE_ID=""
    fi
    echo ""
fi

if [ -z "$INSTANCE_ID" ]; then
    echo "=========================================="
    echo "LỖI: Cần cung cấp instance ID"
    echo "=========================================="
    echo ""
    echo "Usage: $0 <instance_id>"
    echo ""
    echo "Hoặc để script tự động detect từ logs:"
    echo "  $0"
    echo ""
    exit 1
fi

echo "=========================================="
echo "KIỂM TRA NGUYÊN NHÂN LỖI FILE SOURCE"
echo "=========================================="
echo "Instance ID: $INSTANCE_ID"
echo ""

# Màu sắc cho output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 1. Kiểm tra instance có tồn tại không
echo "1. Kiểm tra thông tin instance..."
echo "-----------------------------------"

# Tìm port thực tế từ journalctl logs
DETECTED_PORT=""
if command -v journalctl >/dev/null 2>&1; then
    # Tìm port từ logs startup
    DETECTED_PORT=$(journalctl -u edgeos-api --since "1 hour ago" --no-pager 2>/dev/null | \
        grep -iE "(listening|port|started.*port|server.*port)" | \
        grep -oE "port[:\s]+([0-9]+)" | \
        grep -oE "[0-9]+" | \
        head -1)
    
    # Nếu không tìm thấy, thử tìm từ tất cả logs
    if [ -z "$DETECTED_PORT" ]; then
        DETECTED_PORT=$(journalctl --since "1 hour ago" --no-pager 2>/dev/null | \
            grep -iE "(edge.*ai.*api.*listening|listening.*port)" | \
            grep -oE "port[:\s]+([0-9]+)" | \
            grep -oE "[0-9]+" | \
            head -1)
    fi
    
    # Nếu vẫn không tìm thấy, kiểm tra port nào đang được bind
    if [ -z "$DETECTED_PORT" ]; then
        # Tìm process và port nó đang sử dụng
        PID=$(pgrep -f "edgeos-api\|edgeos-api" | head -1)
        if [ ! -z "$PID" ]; then
            DETECTED_PORT=$(ss -tlnp 2>/dev/null | grep "pid=$PID" | grep LISTEN | \
                grep -oE ":[0-9]+" | tr -d ':' | head -1)
        fi
    fi
fi

# Thử các API endpoints khác nhau (hỗ trợ cả old và new format)
API_ENDPOINTS=(
    "/v1/core/instance/$INSTANCE_ID"
    "/api/v1/instances/$INSTANCE_ID"
    "/instances/$INSTANCE_ID"
)

# Thử các base URLs khác nhau
BASE_URLS=(
    "http://localhost:8080"
    "http://127.0.0.1:8080"
    "http://192.168.1.56:8080"
)

# Thêm port được detect vào danh sách thử
if [ ! -z "$DETECTED_PORT" ] && [ "$DETECTED_PORT" != "8080" ]; then
    BASE_URLS=(
        "http://localhost:$DETECTED_PORT"
        "http://127.0.0.1:$DETECTED_PORT"
        "http://192.168.1.56:$DETECTED_PORT"
        "${BASE_URLS[@]}"
    )
    echo "   Phát hiện port từ logs: $DETECTED_PORT"
fi

INSTANCE_INFO=""
API_URL=""
for base_url in "${BASE_URLS[@]}"; do
    for endpoint in "${API_ENDPOINTS[@]}"; do
        test_url="${base_url}${endpoint}"
        response=$(curl -s -w "\n%{http_code}" "$test_url" 2>/dev/null)
        http_code=$(echo "$response" | tail -1)
        body=$(echo "$response" | sed '$d')
        
        # Kiểm tra nếu response là JSON (không phải HTML error)
        if [ "$http_code" = "200" ] && echo "$body" | python3 -c "import sys, json; json.load(sys.stdin)" 2>/dev/null; then
            INSTANCE_INFO="$body"
            API_URL="$base_url"
            echo -e "${GREEN}✓ Tìm thấy API tại: $test_url${NC}"
            break 2
        fi
    done
done

if [ ! -z "$INSTANCE_INFO" ]; then
    echo -e "${GREEN}✓ Instance tồn tại${NC}"
    echo ""
    
    # Extract file path từ JSON response (hỗ trợ nhiều format)
    FILE_PATH=$(echo "$INSTANCE_INFO" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    print(data.get('filePath', ''))
except:
    pass
" 2>/dev/null)
    
    ADDITIONAL_PARAMS_FILE_PATH=$(echo "$INSTANCE_INFO" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if 'additionalParams' in data:
        if isinstance(data['additionalParams'], dict):
            print(data['additionalParams'].get('FILE_PATH', ''))
        elif isinstance(data['additionalParams'], str):
            import json as json2
            params = json2.loads(data['additionalParams'])
            print(params.get('FILE_PATH', ''))
except:
    pass
" 2>/dev/null)
    
    # Extract từ Input.uri (format mới)
    INPUT_URI=$(echo "$INSTANCE_INFO" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if 'Input' in data and isinstance(data['Input'], dict):
        uri = data['Input'].get('uri', '')
        media_type = data['Input'].get('media_type', '')
        # Chỉ lấy nếu media_type là File và uri không có protocol
        if media_type == 'File' and uri and '://' not in uri:
            print(uri)
except:
    pass
" 2>/dev/null)
    
    # Sử dụng FILE_PATH từ additionalParams nếu có, nếu không thì dùng filePath, cuối cùng là Input.uri
    if [ ! -z "$ADDITIONAL_PARAMS_FILE_PATH" ]; then
        ACTUAL_FILE_PATH="$ADDITIONAL_PARAMS_FILE_PATH"
        echo "   File path từ additionalParams.FILE_PATH: $ACTUAL_FILE_PATH"
    elif [ ! -z "$FILE_PATH" ]; then
        ACTUAL_FILE_PATH="$FILE_PATH"
        echo "   File path từ filePath: $ACTUAL_FILE_PATH"
    elif [ ! -z "$INPUT_URI" ]; then
        ACTUAL_FILE_PATH="$INPUT_URI"
        echo "   File path từ Input.uri: $ACTUAL_FILE_PATH"
    else
        ACTUAL_FILE_PATH=""
        echo -e "${YELLOW}⚠ Không tìm thấy file path trong instance info${NC}"
        echo "   (Có thể file path chưa được set hoặc instance chưa được config đúng)"
    fi
    
    echo ""
    echo "   Instance info (một phần):"
    echo "$INSTANCE_INFO" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    # Chỉ hiển thị các field quan trọng
    important_fields = ['instanceId', 'filePath', 'additionalParams', 'status', 'solutionId']
    filtered = {k: v for k, v in data.items() if k in important_fields}
    print(json.dumps(filtered, indent=2, ensure_ascii=False))
except:
    print(sys.stdin.read())
" 2>/dev/null | head -30
else
    echo -e "${YELLOW}⚠ Không thể lấy thông tin instance qua API${NC}"
    echo "   (API có thể không chạy, port khác, hoặc instance không tồn tại)"
    echo ""
    echo "   Đang tìm file path từ logs..."
    
    # Tìm file path từ logs
    LOG_DIRS=(
        "/var/lib/edgeos-api/logs"
        "/var/log/edgeos-api"
        "./logs"
        "$HOME/.local/share/edgeos-api/logs"
        "/opt/edgeos-api/logs"
    )
    
    for log_dir in "${LOG_DIRS[@]}"; do
        if [ -d "$log_dir" ]; then
            # Tìm trong log files gần đây
            for log_file in $(find "$log_dir" -name "*.log" -type f -mtime -1 2>/dev/null | head -5); do
                # Tìm file path trong logs liên quan đến instance ID
                FOUND_PATH=$(grep -i "$INSTANCE_ID" "$log_file" 2>/dev/null | \
                    grep -iE "(file.*path|FILE_PATH|filePath)" | \
                    grep -oE "(FILE_PATH|filePath).*['\"]([^'\"]+)['\"]" | \
                    grep -oE "['\"]([^'\"]+)['\"]" | \
                    head -1 | tr -d "'\"")
                
                if [ ! -z "$FOUND_PATH" ]; then
                    ACTUAL_FILE_PATH="$FOUND_PATH"
                    echo -e "   ${GREEN}✓ Tìm thấy file path trong logs: $ACTUAL_FILE_PATH${NC}"
                    break 2
                fi
            done
        fi
    done
    
    # Nếu vẫn không tìm thấy, thử từ journalctl với nhiều patterns
    if [ -z "$ACTUAL_FILE_PATH" ]; then
        echo "   Đang tìm trong journalctl..."
        
        # Pattern 1: Tìm FILE_PATH trong logs
        FOUND_PATH=$(journalctl --since "24 hours ago" --no-pager 2>/dev/null | \
            grep -i "$INSTANCE_ID" | \
            grep -iE "(FILE_PATH|filePath|file.*path)" | \
            grep -oE "(FILE_PATH|filePath)\s*[:=]\s*['\"]?([^'\"]+)['\"]?" | \
            grep -oE "['\"]?([^'\"]+)['\"]?$" | \
            head -1 | tr -d "'\"")
        
        # Pattern 2: Tìm "File path:" trong logs
        if [ -z "$FOUND_PATH" ]; then
            FOUND_PATH=$(journalctl --since "24 hours ago" --no-pager 2>/dev/null | \
                grep -i "$INSTANCE_ID" | \
                grep -iE "File path:" | \
                grep -oE "File path:\s*['\"]?([^'\"]+)['\"]?" | \
                sed 's/File path:\s*//' | \
                head -1 | tr -d "'\"")
        fi
        
        # Pattern 3: Tìm đường dẫn file tuyệt đối trong logs
        if [ -z "$FOUND_PATH" ]; then
            FOUND_PATH=$(journalctl --since "24 hours ago" --no-pager 2>/dev/null | \
                grep -i "$INSTANCE_ID" | \
                grep -oE "(/|\./)[^[:space:]]+\.(mp4|avi|mov|mkv|flv)" | \
                head -1)
        fi
        
        # Pattern 4: Tìm trong error messages về file
        if [ -z "$FOUND_PATH" ]; then
            FOUND_PATH=$(journalctl --since "24 hours ago" --no-pager 2>/dev/null | \
                grep -i "$INSTANCE_ID" | \
                grep -iE "(open file|file.*failed|cannot.*file)" | \
                grep -oE "(/|\./)[^[:space:]]+\.(mp4|avi|mov|mkv|flv)" | \
                head -1)
        fi
        
        if [ ! -z "$FOUND_PATH" ]; then
            ACTUAL_FILE_PATH="$FOUND_PATH"
            echo -e "   ${GREEN}✓ Tìm thấy file path trong journal: $ACTUAL_FILE_PATH${NC}"
        else
            ACTUAL_FILE_PATH=""
            echo -e "   ${YELLOW}⚠ Không tìm thấy file path trong logs${NC}"
            echo "   Thử xem logs chi tiết hơn:"
            echo "   journalctl -u edgeos-api --since '1 hour ago' | grep -i '$INSTANCE_ID'"
        fi
    fi
fi

echo ""
echo ""

# 2. Kiểm tra file path có rỗng không
if [ -z "$ACTUAL_FILE_PATH" ]; then
    echo "2. Kiểm tra file path..."
    echo "-----------------------------------"
    echo -e "${RED}✗ File path rỗng hoặc không được set${NC}"
    echo ""
    echo "   Nguyên nhân có thể:"
    echo "   - Instance được tạo không có FILE_PATH trong additionalParams"
    echo "   - Instance được tạo không có filePath field"
    echo "   - File path bị mất trong quá trình update instance"
    echo ""
    echo "   Giải pháp:"
    echo "   - Kiểm tra request tạo instance có chứa FILE_PATH không"
    echo "   - Update instance với FILE_PATH đúng:"
    echo "     curl -X PUT http://localhost:8080/api/v1/instances/$INSTANCE_ID \\"
    echo "          -H 'Content-Type: application/json' \\"
    echo "          -d '{\"additionalParams\": {\"FILE_PATH\": \"/path/to/video.mp4\"}}'"
    echo ""
else
    echo "2. Kiểm tra file path..."
    echo "-----------------------------------"
    echo "   File path: $ACTUAL_FILE_PATH"
    echo ""
    
    # 3. Kiểm tra file có tồn tại không
    echo "3. Kiểm tra file có tồn tại..."
    echo "-----------------------------------"
    if [ -f "$ACTUAL_FILE_PATH" ]; then
        echo -e "${GREEN}✓ File tồn tại${NC}"
    elif [ -e "$ACTUAL_FILE_PATH" ]; then
        echo -e "${YELLOW}⚠ Path tồn tại nhưng không phải là file thường${NC}"
        echo "   (có thể là directory hoặc symlink)"
    else
        echo -e "${RED}✗ File KHÔNG tồn tại${NC}"
        echo ""
        echo "   Nguyên nhân: File path không đúng hoặc file đã bị xóa"
        echo ""
        echo "   Kiểm tra:"
        echo "   - File path có đúng không: $ACTUAL_FILE_PATH"
        echo "   - Thư mục chứa file có tồn tại không:"
        DIR_PATH=$(dirname "$ACTUAL_FILE_PATH")
        if [ -d "$DIR_PATH" ]; then
            echo -e "     ${GREEN}✓ Thư mục tồn tại: $DIR_PATH${NC}"
            echo "     Files trong thư mục:"
            ls -lh "$DIR_PATH" | head -10
        else
            echo -e "     ${RED}✗ Thư mục không tồn tại: $DIR_PATH${NC}"
        fi
        echo ""
    fi
    
    # 4. Kiểm tra quyền truy cập thư mục cha (QUAN TRỌNG!)
    echo ""
    echo "4. Kiểm tra quyền truy cập thư mục cha..."
    echo "-----------------------------------"
    DIR_PATH=$(dirname "$ACTUAL_FILE_PATH")
    if [ -x "$DIR_PATH" ]; then
        echo -e "${GREEN}✓ Thư mục có quyền traverse (execute)${NC}"
        echo "   Thư mục: $DIR_PATH"
        echo "   Permissions: $(stat -c '%a %A' "$DIR_PATH" 2>/dev/null || stat -f '%A %Sp' "$DIR_PATH" 2>/dev/null || echo 'N/A')"
    else
        echo -e "${RED}✗ Thư mục KHÔNG có quyền traverse (execute)${NC}"
        echo ""
        echo "   Thư mục: $DIR_PATH"
        echo "   Permissions: $(stat -c '%a %A' "$DIR_PATH" 2>/dev/null || stat -f '%A %Sp' "$DIR_PATH" 2>/dev/null || echo 'N/A')"
        echo ""
        echo "   ⚠️ QUAN TRỌNG: Trên Linux, để truy cập file trong thư mục,"
        echo "   bạn CẦN quyền execute (x) trên thư mục cha!"
        echo ""
        echo "   Giải pháp:"
        echo "   sudo chmod 755 $DIR_PATH"
        echo "   hoặc"
        echo "   sudo chmod u+x $DIR_PATH"
    fi
    
    # 5. Kiểm tra quyền truy cập file
    echo ""
    echo "5. Kiểm tra quyền truy cập file..."
    echo "-----------------------------------"
    if [ -r "$ACTUAL_FILE_PATH" ]; then
        echo -e "${GREEN}✓ File có quyền đọc${NC}"
    else
        echo -e "${RED}✗ File KHÔNG có quyền đọc${NC}"
        echo ""
        echo "   Thông tin file:"
        ls -lh "$ACTUAL_FILE_PATH" 2>/dev/null || echo "   Không thể lấy thông tin file"
        echo ""
        echo "   Giải pháp:"
        echo "   chmod 644 $ACTUAL_FILE_PATH"
        echo "   hoặc"
        echo "   chmod 755 $(dirname $ACTUAL_FILE_PATH)"
    fi
    
    # 6. Kiểm tra thông tin chi tiết file
    echo ""
    echo "6. Thông tin chi tiết file..."
    echo "-----------------------------------"
    if [ -f "$ACTUAL_FILE_PATH" ]; then
        echo "   Size: $(du -h "$ACTUAL_FILE_PATH" | cut -f1)"
        echo "   Permissions: $(stat -c '%a %A' "$ACTUAL_FILE_PATH" 2>/dev/null || stat -f '%A %Sp' "$ACTUAL_FILE_PATH" 2>/dev/null || echo 'N/A')"
        echo "   Owner: $(stat -c '%U:%G' "$ACTUAL_FILE_PATH" 2>/dev/null || stat -f '%Su:%Sg' "$ACTUAL_FILE_PATH" 2>/dev/null || echo 'N/A')"
        echo "   Type: $(file "$ACTUAL_FILE_PATH" 2>/dev/null | cut -d: -f2- || echo 'N/A')"
    fi
fi

echo ""
echo ""

# 7. Kiểm tra GStreamer plugins
echo "7. Kiểm tra GStreamer plugins..."
echo "-----------------------------------"

# Tìm gst-inspect-1.0 trong PATH
GST_INSPECT=""
for path in /usr/bin/gst-inspect-1.0 /usr/local/bin/gst-inspect-1.0 $(which gst-inspect-1.0 2>/dev/null); do
    if [ -x "$path" ]; then
        GST_INSPECT="$path"
        break
    fi
done

if [ -z "$GST_INSPECT" ]; then
    echo -e "${YELLOW}⚠ Không tìm thấy gst-inspect-1.0 trong PATH${NC}"
    echo "   GStreamer có thể chưa được cài đặt hoặc không có trong PATH"
    echo ""
    echo "   Kiểm tra GStreamer đã được cài đặt:"
    if command -v gst-launch-1.0 >/dev/null 2>&1; then
        echo -e "   ${GREEN}✓ gst-launch-1.0 có sẵn${NC}"
    else
        echo -e "   ${RED}✗ GStreamer chưa được cài đặt${NC}"
    fi
    echo ""
    echo "   Cài đặt GStreamer:"
    echo "   sudo apt-get update"
    echo "   sudo apt-get install gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav"
    MISSING_PLUGINS=("isomp4" "h264parse" "avdec_h264" "filesrc" "videoconvert")
else
    REQUIRED_PLUGINS=("isomp4" "h264parse" "avdec_h264" "filesrc" "videoconvert")
    MISSING_PLUGINS=()
    
    for plugin in "${REQUIRED_PLUGINS[@]}"; do
        if "$GST_INSPECT" "$plugin" >/dev/null 2>&1; then
            echo -e "   ${GREEN}✓ $plugin${NC}"
        else
            echo -e "   ${RED}✗ $plugin (THIẾU)${NC}"
            MISSING_PLUGINS+=("$plugin")
        fi
    done
    
    if [ ${#MISSING_PLUGINS[@]} -gt 0 ]; then
        echo ""
        echo -e "${RED}✗ Thiếu các GStreamer plugins cần thiết${NC}"
        echo "   Cài đặt:"
        echo "   sudo apt-get update"
        echo "   sudo apt-get install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav"
    fi
fi

echo ""
echo ""

# 8. Kiểm tra logs từ file
echo "8. Kiểm tra logs từ file..."
echo "-----------------------------------"
LOG_DIRS=(
    "/var/lib/edgeos-api/logs"
    "/var/log/edgeos-api"
    "./logs"
    "$HOME/.local/share/edgeos-api/logs"
    "/opt/edgeos-api/logs"
)

FOUND_LOG=false
for log_dir in "${LOG_DIRS[@]}"; do
    if [ -d "$log_dir" ]; then
        echo "   Tìm trong: $log_dir"
        # Tìm log files gần đây (trong 24h)
        INSTANCE_LOGS=$(find "$log_dir" -name "*.log" -type f -mtime -1 2>/dev/null)
        if [ ! -z "$INSTANCE_LOGS" ]; then
            FOUND_LOG=true
            for INSTANCE_LOG in $INSTANCE_LOGS; do
                echo ""
                echo "   Log file: $INSTANCE_LOG"
                
                # Kiểm tra xem log có chứa instance ID không
                if grep -qi "$INSTANCE_ID" "$INSTANCE_LOG" 2>/dev/null; then
                    echo "   Các dòng liên quan đến instance $INSTANCE_ID:"
                    grep -i "$INSTANCE_ID" "$INSTANCE_LOG" | tail -20 || echo "   Không tìm thấy"
                    echo ""
                fi
                
                # Kiểm tra "open file failed"
                if grep -qi "open file failed" "$INSTANCE_LOG" 2>/dev/null; then
                    echo "   Các dòng liên quan đến 'open file failed':"
                    grep -i "open file failed" "$INSTANCE_LOG" | tail -10 || echo "   Không tìm thấy"
                    echo ""
                fi
                
                # Kiểm tra file path nếu có
                if [ ! -z "$ACTUAL_FILE_PATH" ]; then
                    if grep -qi "$(basename "$ACTUAL_FILE_PATH")" "$INSTANCE_LOG" 2>/dev/null; then
                        echo "   Các dòng liên quan đến file path:"
                        grep -i "$(basename "$ACTUAL_FILE_PATH")" "$INSTANCE_LOG" | tail -10 || echo "   Không tìm thấy"
                        echo ""
                    fi
                fi
            done
            break
        fi
    fi
done

if [ "$FOUND_LOG" = false ]; then
    echo "   Không tìm thấy log directory hoặc log files gần đây"
    echo "   (Logs có thể được lưu ở nơi khác hoặc chưa được tạo)"
fi

echo ""
echo ""

# 9. Kiểm tra systemd logs
echo "9. Kiểm tra systemd logs..."
echo "-----------------------------------"

# Tìm service name có thể
SERVICE_NAMES=("edgeos-api" "edgeai-api")

FOUND_SERVICE=false
for service_name in "${SERVICE_NAMES[@]}"; do
    if systemctl list-units --type=service --all 2>/dev/null | grep -q "$service_name"; then
        FOUND_SERVICE=true
        if systemctl is-active --quiet "$service_name" 2>/dev/null; then
            echo -e "${GREEN}✓ Service '$service_name' đang chạy${NC}"
            echo ""
            
            # Hiển thị thông tin về port nếu đã detect
            if [ ! -z "$DETECTED_PORT" ]; then
                echo "   Port được phát hiện: $DETECTED_PORT"
                echo ""
            fi
            
            echo "   Logs gần đây liên quan đến instance:"
            INSTANCE_LOGS=$(journalctl -u "$service_name" --since "1 hour ago" --no-pager 2>/dev/null | grep -i "$INSTANCE_ID")
            if [ ! -z "$INSTANCE_LOGS" ]; then
                echo "$INSTANCE_LOGS" | tail -30
                
                # Tìm file path từ logs này nếu chưa có
                if [ -z "$ACTUAL_FILE_PATH" ]; then
                    FOUND_PATH=$(echo "$INSTANCE_LOGS" | \
                        grep -iE "(FILE_PATH|filePath|File path:)" | \
                        grep -oE "(FILE_PATH|filePath)\s*[:=]\s*['\"]?([^'\"]+)['\"]?" | \
                        grep -oE "['\"]?([^'\"]+)['\"]?$" | \
                        head -1 | tr -d "'\"")
                    
                    if [ ! -z "$FOUND_PATH" ]; then
                        ACTUAL_FILE_PATH="$FOUND_PATH"
                        echo ""
                        echo -e "   ${GREEN}✓ Tìm thấy file path trong logs: $ACTUAL_FILE_PATH${NC}"
                    fi
                fi
            else
                echo "   Không tìm thấy logs về instance này trong 1 giờ qua"
                echo "   Thử tìm trong khoảng thời gian dài hơn..."
                journalctl -u "$service_name" --since "24 hours ago" --no-pager 2>/dev/null | grep -i "$INSTANCE_ID" | tail -10 || echo "   Không tìm thấy"
            fi
            
            echo ""
            echo "   Logs gần đây liên quan đến 'open file failed':"
            FAILED_LOGS=$(journalctl -u "$service_name" --since "1 hour ago" --no-pager 2>/dev/null | grep -i "open file failed")
            if [ ! -z "$FAILED_LOGS" ]; then
                echo "$FAILED_LOGS" | tail -20
                
                # Tìm file path từ error logs
                if [ -z "$ACTUAL_FILE_PATH" ]; then
                    FOUND_PATH=$(echo "$FAILED_LOGS" | \
                        grep -i "$INSTANCE_ID" | \
                        grep -oE "(/|\./)[^[:space:]]+\.(mp4|avi|mov|mkv|flv)" | \
                        head -1)
                    
                    if [ ! -z "$FOUND_PATH" ]; then
                        ACTUAL_FILE_PATH="$FOUND_PATH"
                        echo ""
                        echo -e "   ${GREEN}✓ Tìm thấy file path từ error logs: $ACTUAL_FILE_PATH${NC}"
                    fi
                fi
            else
                echo "   Không tìm thấy"
            fi
            break
        fi
    fi
done

if [ "$FOUND_SERVICE" = false ]; then
    echo -e "${YELLOW}⚠ Không tìm thấy systemd service${NC}"
    echo "   Đang tìm process đang chạy..."
    
    # Tìm process có thể liên quan
    PIDS=$(pgrep -f "edgeos-api\|edgeos-api" 2>/dev/null)
    if [ ! -z "$PIDS" ]; then
        echo -e "   ${GREEN}✓ Tìm thấy process: $PIDS${NC}"
        echo "   Kiểm tra logs từ journal (tất cả services):"
        journalctl --since "10 minutes ago" --no-pager 2>/dev/null | grep -i "$INSTANCE_ID" | tail -30 || echo "   Không tìm thấy"
    else
        echo -e "   ${YELLOW}⚠ Không tìm thấy process đang chạy${NC}"
        echo "   Service có thể không chạy hoặc chạy với tên khác"
    fi
fi

echo ""
echo ""

# 10. Kiểm tra user service đang chạy
echo ""
echo "10. Kiểm tra user service đang chạy..."
echo "-----------------------------------"
SERVICE_USER=$(systemctl show -p User --value edgeos-api 2>/dev/null || systemctl show -p User --value edgeos-api 2>/dev/null || echo "edgeai")
echo "   Service user: $SERVICE_USER"
echo ""
echo "   Kiểm tra quyền với user này:"
if [ "$SERVICE_USER" != "$USER" ]; then
    echo "   (Chạy với sudo để kiểm tra quyền của user $SERVICE_USER)"
    echo "   sudo -u $SERVICE_USER test -r \"$ACTUAL_FILE_PATH\" && echo 'File readable' || echo 'File NOT readable'"
    echo "   sudo -u $SERVICE_USER test -x \"$DIR_PATH\" && echo 'Directory traversable' || echo 'Directory NOT traversable'"
else
    if [ -r "$ACTUAL_FILE_PATH" ]; then
        echo -e "   ${GREEN}✓ File readable by current user${NC}"
    else
        echo -e "   ${RED}✗ File NOT readable by current user${NC}"
    fi
    if [ -x "$DIR_PATH" ]; then
        echo -e "   ${GREEN}✓ Directory traversable by current user${NC}"
    else
        echo -e "   ${RED}✗ Directory NOT traversable by current user${NC}"
    fi
fi

echo ""
echo ""

# 11. Tóm tắt và đề xuất
echo "=========================================="
echo "TÓM TẮT VÀ ĐỀ XUẤT"
echo "=========================================="
echo ""

ISSUES_FOUND=0

# Kiểm tra API connection
if [ -z "$API_URL" ]; then
    echo -e "${YELLOW}[LƯU Ý] Không thể kết nối đến API${NC}"
    echo "   - API có thể không chạy"
    echo "   - Port có thể khác (không phải 8080)"
    echo "   - Instance có thể không tồn tại"
    echo ""
    echo "   Kiểm tra:"
    echo "   - Service có đang chạy không: systemctl status edgeos-api"
    echo "   - Port nào đang được sử dụng: netstat -tlnp | grep 8080"
    echo "   - Thử các endpoint khác:"
    for base_url in "${BASE_URLS[@]}"; do
        for endpoint in "${API_ENDPOINTS[@]}"; do
            echo "     curl ${base_url}${endpoint}"
        done
    done
    echo ""
fi

if [ -z "$ACTUAL_FILE_PATH" ]; then
    echo -e "${RED}[VẤN ĐỀ] File path rỗng hoặc không được set${NC}"
    ISSUES_FOUND=$((ISSUES_FOUND + 1))
    echo ""
    echo "   Giải pháp:"
    if [ ! -z "$API_URL" ]; then
        echo "   Update instance với FILE_PATH:"
        echo "   curl -X PUT $API_URL/instances/$INSTANCE_ID \\"
        echo "        -H 'Content-Type: application/json' \\"
        echo "        -d '{\"additionalParams\": {\"FILE_PATH\": \"/path/to/video.mp4\"}}'"
    else
        echo "   - Kiểm tra lại request tạo instance có chứa FILE_PATH không"
        echo "   - Kiểm tra logs để tìm file path đã được sử dụng"
    fi
    echo ""
fi

if [ ! -z "$ACTUAL_FILE_PATH" ] && [ ! -f "$ACTUAL_FILE_PATH" ]; then
    echo -e "${RED}[VẤN ĐỀ] File không tồn tại: $ACTUAL_FILE_PATH${NC}"
    ISSUES_FOUND=$((ISSUES_FOUND + 1))
fi

DIR_PATH_CHECK=$(dirname "$ACTUAL_FILE_PATH" 2>/dev/null || echo "")
if [ ! -z "$ACTUAL_FILE_PATH" ] && [ ! -z "$DIR_PATH_CHECK" ] && [ ! -x "$DIR_PATH_CHECK" ]; then
    echo -e "${RED}[VẤN ĐỀ] Thư mục cha không có quyền traverse (execute): $DIR_PATH_CHECK${NC}"
    ISSUES_FOUND=$((ISSUES_FOUND + 1))
    echo ""
    echo "   ⚠️ Đây là nguyên nhân phổ biến nhất của lỗi 'open file failed'!"
    echo "   Trên Linux, để truy cập file trong thư mục, bạn CẦN quyền execute (x) trên thư mục."
    echo ""
    echo "   Giải pháp:"
    echo "   sudo chmod 755 $DIR_PATH_CHECK"
fi

if [ ! -z "$ACTUAL_FILE_PATH" ] && [ -f "$ACTUAL_FILE_PATH" ] && [ ! -r "$ACTUAL_FILE_PATH" ]; then
    echo -e "${RED}[VẤN ĐỀ] File không có quyền đọc${NC}"
    ISSUES_FOUND=$((ISSUES_FOUND + 1))
fi

if [ ${#MISSING_PLUGINS[@]} -gt 0 ]; then
    echo -e "${RED}[VẤN ĐỀ] Thiếu GStreamer plugins: ${MISSING_PLUGINS[*]}${NC}"
    ISSUES_FOUND=$((ISSUES_FOUND + 1))
fi

if [ $ISSUES_FOUND -eq 0 ]; then
    echo -e "${GREEN}✓ Không phát hiện vấn đề rõ ràng${NC}"
    echo ""
    echo "   Các nguyên nhân khác có thể:"
    echo "   1. File bị corrupt hoặc format không được hỗ trợ"
    echo "   2. File đang được sử dụng bởi process khác"
    echo "   3. Vấn đề với GStreamer pipeline configuration"
    echo "   4. File path có ký tự đặc biệt cần escape"
    echo ""
    echo "   Kiểm tra thêm:"
    echo "   - Thử mở file bằng GStreamer:"
    echo "     gst-launch-1.0 filesrc location=\"$ACTUAL_FILE_PATH\" ! decodebin ! autovideosink"
    echo "   - Kiểm tra format file:"
    echo "     ffprobe \"$ACTUAL_FILE_PATH\""
else
    echo ""
    echo "   Đã phát hiện $ISSUES_FOUND vấn đề. Vui lòng sửa các vấn đề trên trước khi thử lại."
fi

echo ""
echo "=========================================="

