#!/bin/bash

# Face Database Connection Configuration Script
# Cấu hình kết nối MySQL local server cho Face Database

# Màu sắc cho output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Face Database Connection Configuration ===${NC}\n"

# Thông tin mặc định (thay đổi theo môi trường của bạn)
DB_HOST="${DB_HOST:-localhost}"
DB_PORT="${DB_PORT:-3306}"
DB_NAME="${DB_NAME:-face_recognition}"
DB_USER="${DB_USER:-root}"
DB_PASSWORD="${DB_PASSWORD:-}"
API_URL="${API_URL:-http://localhost:8080}"

# Nếu không có password từ env, hỏi user
if [ -z "$DB_PASSWORD" ]; then
    echo -e "${YELLOW}Nhập MySQL password cho user '$DB_USER':${NC}"
    read -s DB_PASSWORD
    echo
fi

# Kiểm tra API server đang chạy
echo -e "${GREEN}[1/4] Kiểm tra API server...${NC}"
if ! curl -s "$API_URL/v1/core/health" > /dev/null; then
    echo -e "${RED}❌ API server không khả dụng tại $API_URL${NC}"
    echo -e "${YELLOW}Đảm bảo server đang chạy: ./build/omniapi${NC}"
    exit 1
fi
echo -e "${GREEN}✓ API server đang chạy${NC}\n"

# Kiểm tra database connection
echo -e "${GREEN}[2/4] Kiểm tra MySQL connection...${NC}"
if ! mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" -p"$DB_PASSWORD" -e "USE $DB_NAME;" 2>/dev/null; then
    echo -e "${RED}❌ Không thể kết nối MySQL${NC}"
    echo -e "${YELLOW}Kiểm tra:${NC}"
    echo "  - MySQL đang chạy: sudo systemctl status mysql"
    echo "  - Database tồn tại: mysql -u $DB_USER -p -e 'SHOW DATABASES;'"
    echo "  - User có quyền: mysql -u $DB_USER -p -e 'SHOW GRANTS;'"
    exit 1
fi
echo -e "${GREEN}✓ MySQL connection OK${NC}\n"

# Kiểm tra tables
echo -e "${GREEN}[3/4] Kiểm tra database tables...${NC}"
TABLES=$(mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" -p"$DB_PASSWORD" "$DB_NAME" -e "SHOW TABLES;" 2>/dev/null | grep -E "(face_libraries|face_log)" | wc -l)
if [ "$TABLES" -lt 2 ]; then
    echo -e "${YELLOW}⚠ Cảnh báo: Thiếu một số tables${NC}"
    echo -e "${YELLOW}Đảm bảo có các bảng: face_libraries, face_log${NC}"
    echo -e "${YELLOW}Xem hướng dẫn: docs/FACE_DATABASE_CONNECTION.md${NC}\n"
else
    echo -e "${GREEN}✓ Database tables OK${NC}\n"
fi

# Cấu hình connection
echo -e "${GREEN}[4/4] Cấu hình database connection...${NC}"

# Tạo JSON payload
JSON_PAYLOAD=$(cat <<EOF
{
  "type": "mysql",
  "host": "$DB_HOST",
  "port": $DB_PORT,
  "database": "$DB_NAME",
  "username": "$DB_USER",
  "password": "$DB_PASSWORD",
  "charset": "utf8mb4"
}
EOF
)

# Gửi request
RESPONSE=$(curl -s -X POST "$API_URL/v1/recognition/face-database/connection" \
  -H "Content-Type: application/json" \
  -d "$JSON_PAYLOAD")

# Kiểm tra response
if echo "$RESPONSE" | grep -q "configured successfully"; then
    echo -e "${GREEN}✓ Cấu hình thành công!${NC}\n"
    echo -e "${GREEN}Response:${NC}"
    echo "$RESPONSE" | jq '.' 2>/dev/null || echo "$RESPONSE"
else
    echo -e "${RED}❌ Cấu hình thất bại${NC}\n"
    echo -e "${RED}Response:${NC}"
    echo "$RESPONSE" | jq '.' 2>/dev/null || echo "$RESPONSE"
    exit 1
fi

# Verify configuration
echo -e "\n${GREEN}=== Verify Configuration ===${NC}"
VERIFY_RESPONSE=$(curl -s "$API_URL/v1/recognition/face-database/connection")
echo "$VERIFY_RESPONSE" | jq '.' 2>/dev/null || echo "$VERIFY_RESPONSE"

echo -e "\n${GREEN}=== Hoàn thành! ===${NC}"
echo -e "${YELLOW}Để test, đăng ký một face:${NC}"
echo "curl -X POST \"$API_URL/v1/recognition/faces?subject=test_user\" -F \"file=@test_face.jpg\""
echo -e "\n${YELLOW}Kiểm tra trong database:${NC}"
echo "mysql -u $DB_USER -p -e \"USE $DB_NAME; SELECT * FROM face_libraries;\""

