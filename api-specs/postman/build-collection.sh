#!/bin/bash

# Script để build Postman collection từ OpenAPI YAML
# Usage: ./build-collection.sh

set -e

# Đường dẫn
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
API_SPECS_DIR="$(dirname "$SCRIPT_DIR")"
OPENAPI_FILE="$API_SPECS_DIR/openapi.yaml"
OUTPUT_FILE="$SCRIPT_DIR/api.collection.json"

# Màu sắc cho output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}🚀 Building Postman collection from OpenAPI spec...${NC}"

# Kiểm tra file OpenAPI có tồn tại không
if [ ! -f "$OPENAPI_FILE" ]; then
    echo -e "${RED}❌ Error: OpenAPI file not found at $OPENAPI_FILE${NC}"
    exit 1
fi

echo -e "${YELLOW}📄 OpenAPI file: $OPENAPI_FILE${NC}"
echo -e "${YELLOW}📦 Output file: $OUTPUT_FILE${NC}"

# Kiểm tra Node.js và npm
if ! command -v node &> /dev/null; then
    echo -e "${RED}❌ Error: Node.js is not installed. Please install Node.js first.${NC}"
    exit 1
fi

if ! command -v npm &> /dev/null; then
    echo -e "${RED}❌ Error: npm is not installed. Please install npm first.${NC}"
    exit 1
fi

# Tạo thư mục output nếu chưa có
mkdir -p "$SCRIPT_DIR"

# Chuyển đổi OpenAPI sang Postman collection sử dụng npx
# npx sẽ tự động tải và chạy openapi-to-postmanv2 nếu chưa có
echo -e "${GREEN}🔄 Converting OpenAPI to Postman collection...${NC}"

# Capture cả stdout và stderr
CONVERSION_OUTPUT=$(npx -y openapi-to-postmanv2 -s "$OPENAPI_FILE" -o "$OUTPUT_FILE" 2>&1)
CONVERSION_EXIT_CODE=$?

# Hiển thị output của conversion
echo "$CONVERSION_OUTPUT"

# Kiểm tra xem có lỗi trong output không
if echo "$CONVERSION_OUTPUT" | grep -qi "error\|UserError\|YAMLException\|duplicated mapping key"; then
    echo -e "${RED}⚠️  Warning: Conversion completed but there were errors/warnings in the OpenAPI file${NC}"
    echo -e "${YELLOW}   Please check the OpenAPI file for issues${NC}"
fi

# Kiểm tra exit code
if [ $CONVERSION_EXIT_CODE -ne 0 ]; then
    echo -e "${RED}❌ Error: Conversion failed with exit code $CONVERSION_EXIT_CODE${NC}"
    exit 1
fi

# Kiểm tra file output
if [ ! -f "$OUTPUT_FILE" ]; then
    echo -e "${RED}❌ Error: Output file was not created${NC}"
    exit 1
fi

# Validate JSON format
if ! python3 -m json.tool "$OUTPUT_FILE" > /dev/null 2>&1; then
    echo -e "${RED}❌ Error: Output file is not valid JSON${NC}"
    exit 1
fi

# Kiểm tra file có rỗng không
if [ ! -s "$OUTPUT_FILE" ]; then
    echo -e "${RED}❌ Error: Output file is empty${NC}"
    exit 1
fi

FILE_SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
echo -e "${GREEN}✅ Success! Postman collection created: $OUTPUT_FILE${NC}"
echo -e "${GREEN}   File size: $FILE_SIZE${NC}"
echo -e "${GREEN}📥 You can now import this file into Postman${NC}"

