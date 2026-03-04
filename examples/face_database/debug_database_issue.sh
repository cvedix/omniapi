#!/bin/bash

# Script debug để kiểm tra tại sao database không lưu được

echo "=== Debug Database Connection Issue ==="
echo ""

# Check config.json
echo "[1/5] Kiểm tra config.json..."
if [ -f "config.json" ]; then
    echo "✓ config.json tồn tại"
    echo ""
    echo "Face database config:"
    cat config.json | jq '.face_database' 2>/dev/null || echo "  (không có hoặc lỗi JSON)"
else
    echo "❌ config.json không tồn tại"
    echo "Tìm trong các vị trí khác..."
    if [ -f "/opt/edgeos-api/config/config.json" ]; then
        echo "Tìm thấy tại: /opt/edgeos-api/config/config.json"
        cat /opt/edgeos-api/config/config.json | jq '.face_database' 2>/dev/null
    elif [ -f "/etc/edgeos-api/config.json" ]; then
        echo "Tìm thấy tại: /etc/edgeos-api/config.json"
        cat /etc/edgeos-api/config.json | jq '.face_database' 2>/dev/null
    fi
fi
echo ""

# Check API response
echo "[2/5] Kiểm tra API config response..."
API_URL="${API_URL:-http://localhost:8080}"
RESPONSE=$(curl -s "$API_URL/v1/recognition/face-database/connection" 2>/dev/null)
if [ $? -eq 0 ]; then
    echo "✓ API response:"
    echo "$RESPONSE" | jq '.' 2>/dev/null || echo "$RESPONSE"
    
    ENABLED=$(echo "$RESPONSE" | jq -r '.enabled' 2>/dev/null)
    if [ "$ENABLED" = "true" ]; then
        echo ""
        echo "✓ Database enabled trong API"
    else
        echo ""
        echo "❌ Database NOT enabled trong API"
        echo "Có thể cần restart API server sau khi cấu hình"
    fi
else
    echo "❌ Không thể kết nối API server tại $API_URL"
fi
echo ""

# Test MySQL connection
echo "[3/5] Test MySQL connection trực tiếp..."
DB_USER="face_user"
DB_PASSWORD="Admin@123"
DB_HOST="localhost"
DB_PORT="3306"
DB_NAME="face_recognition"

MYSQL_PWD="$DB_PASSWORD" mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" "$DB_NAME" -e "SELECT 1;" 2>&1 | grep -q "1"
if [ $? -eq 0 ]; then
    echo "✓ MySQL connection thành công"
else
    echo "❌ MySQL connection thất bại"
fi
echo ""

# Test INSERT
echo "[4/5] Test INSERT vào database..."
cat > /tmp/debug_insert.sql <<EOF
INSERT INTO face_libraries (image_id, subject, base64_image, embedding, created_at) 
VALUES ('debug-test-123', 'debug_test', 'test_base64', '1.0,2.0,3.0', NOW());
EOF

MYSQL_PWD="$DB_PASSWORD" mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" "$DB_NAME" < /tmp/debug_insert.sql 2>&1
if [ $? -eq 0 ]; then
    echo "✓ INSERT thành công"
    # Clean up
    MYSQL_PWD="$DB_PASSWORD" mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" "$DB_NAME" -e "DELETE FROM face_libraries WHERE image_id='debug-test-123';" 2>&1 > /dev/null
else
    echo "❌ INSERT thất bại"
    echo "SQL:"
    cat /tmp/debug_insert.sql
fi
rm -f /tmp/debug_insert.sql
echo ""

# Check logs
echo "[5/5] Kiểm tra logs (nếu có)..."
if [ -f "logs/api.log" ]; then
    echo "Recent database-related logs:"
    tail -50 logs/api.log | grep -i "FaceDatabaseHelper\|RecognitionHandler.*Database\|Database enabled" | tail -10
elif [ -f "/opt/edgeos-api/logs/api.log" ]; then
    echo "Recent database-related logs:"
    tail -50 /opt/edgeos-api/logs/api.log | grep -i "FaceDatabaseHelper\|RecognitionHandler.*Database\|Database enabled" | tail -10
else
    echo "Không tìm thấy log file"
fi
echo ""

echo "=== Kết luận ==="
echo ""
echo "Nếu database enabled = false trong API response:"
echo "  1. Đảm bảo đã cấu hình database connection thành công"
echo "  2. RESTART API server sau khi cấu hình"
echo "  3. Kiểm tra config.json có section 'face_database' với 'enabled: true'"
echo ""
echo "Nếu database enabled = true nhưng vẫn lưu vào file:"
echo "  1. Kiểm tra logs để xem lỗi MySQL"
echo "  2. Kiểm tra MySQL connection và permissions"
echo "  3. Kiểm tra xem mysql command có trong PATH không: which mysql"


