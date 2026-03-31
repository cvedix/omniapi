#!/bin/bash

# Script cấu hình API với thông tin database cụ thể

API_URL="${API_URL:-http://localhost:8080}"

# Thông tin database
DB_HOST="localhost"
DB_PORT="3306"
DB_NAME="face_recognition"
DB_USER="face_user"
DB_PASSWORD="Admin@123"
DB_CHARSET="utf8mb4"

echo "=== Cấu Hình Face Database Connection ==="
echo ""
echo "Thông tin database:"
echo "  Host: $DB_HOST"
echo "  Port: $DB_PORT"
echo "  Database: $DB_NAME"
echo "  Username: $DB_USER"
echo ""

# Kiểm tra API server
echo "Kiểm tra API server..."
if ! curl -s "$API_URL/v1/core/health" > /dev/null; then
    echo "❌ API server không khả dụng tại $API_URL"
    echo "Đảm bảo server đang chạy: ./build/omniapi"
    exit 1
fi
echo "✓ API server đang chạy"
echo ""

# Cấu hình
echo "Đang cấu hình database connection..."
RESPONSE=$(curl -s -X POST "$API_URL/v1/recognition/face-database/connection" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"mysql\",
    \"host\": \"$DB_HOST\",
    \"port\": $DB_PORT,
    \"database\": \"$DB_NAME\",
    \"username\": \"$DB_USER\",
    \"password\": \"$DB_PASSWORD\",
    \"charset\": \"$DB_CHARSET\"
  }")

# Kiểm tra response
if echo "$RESPONSE" | grep -q "configured successfully"; then
    echo "✓ Cấu hình thành công!"
    echo ""
    echo "Response:"
    echo "$RESPONSE" | jq '.' 2>/dev/null || echo "$RESPONSE"
    echo ""
    echo "Kiểm tra cấu hình:"
    curl -s "$API_URL/v1/recognition/face-database/connection" | jq '.' 2>/dev/null || curl -s "$API_URL/v1/recognition/face-database/connection"
else
    echo "❌ Cấu hình thất bại"
    echo ""
    echo "Response:"
    echo "$RESPONSE" | jq '.' 2>/dev/null || echo "$RESPONSE"
    exit 1
fi


