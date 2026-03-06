#!/bin/bash

# ONVIF API Test Script
# Usage: ./test_onvif.sh [API_BASE_URL]

API_BASE_URL="${1:-http://localhost:8080}"

echo "=========================================="
echo "ONVIF API Test Script"
echo "API Base URL: $API_BASE_URL"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test 1: Discover Cameras
echo -e "${YELLOW}Test 1: Discover ONVIF Cameras${NC}"
echo "POST $API_BASE_URL/v1/onvif/discover"
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_BASE_URL/v1/onvif/discover")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [ "$HTTP_CODE" == "204" ]; then
    echo -e "${GREEN}✓ Discovery started successfully${NC}"
else
    echo -e "${RED}✗ Discovery failed (HTTP $HTTP_CODE)${NC}"
    echo "Response: $BODY"
fi
echo ""

# Wait a bit for discovery to complete
echo "Waiting 5 seconds for discovery to complete..."
sleep 5
echo ""

# Test 2: Get Cameras List
echo -e "${YELLOW}Test 2: Get Discovered Cameras${NC}"
echo "GET $API_BASE_URL/v1/onvif/cameras"
RESPONSE=$(curl -s -w "\n%{http_code}" "$API_BASE_URL/v1/onvif/cameras")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [ "$HTTP_CODE" == "200" ]; then
    echo -e "${GREEN}✓ Get cameras successful${NC}"
    echo "Response:"
    echo "$BODY" | python3 -m json.tool 2>/dev/null || echo "$BODY"
    
    # Extract camera ID (first camera's IP or UUID)
    CAMERA_ID=$(echo "$BODY" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if isinstance(data, list) and len(data) > 0:
        camera = data[0]
        print(camera.get('uuid') or camera.get('ip', ''))
except:
    pass
" 2>/dev/null)
    
    if [ -n "$CAMERA_ID" ]; then
        echo ""
        echo -e "${GREEN}Found camera: $CAMERA_ID${NC}"
        export CAMERA_ID
    else
        echo -e "${YELLOW}No cameras found or unable to parse response${NC}"
    fi
else
    echo -e "${RED}✗ Get cameras failed (HTTP $HTTP_CODE)${NC}"
    echo "Response: $BODY"
fi
echo ""

# Test 3: Set Credentials (if camera ID available)
if [ -n "$CAMERA_ID" ]; then
    echo -e "${YELLOW}Test 3: Set Camera Credentials${NC}"
    echo "Enter camera username (or press Enter to skip):"
    read -r USERNAME
    
    if [ -n "$USERNAME" ]; then
        echo "Enter camera password:"
        read -rs PASSWORD
        echo ""
        
        echo "POST $API_BASE_URL/v1/onvif/camera/$CAMERA_ID/credentials"
        JSON_BODY=$(cat <<EOF
{
  "username": "$USERNAME",
  "password": "$PASSWORD"
}
EOF
)
        RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
            "$API_BASE_URL/v1/onvif/camera/$CAMERA_ID/credentials" \
            -H "Content-Type: application/json" \
            -d "$JSON_BODY")
        HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
        BODY=$(echo "$RESPONSE" | sed '$d')
        
        if [ "$HTTP_CODE" == "204" ]; then
            echo -e "${GREEN}✓ Credentials set successfully${NC}"
        else
            echo -e "${RED}✗ Set credentials failed (HTTP $HTTP_CODE)${NC}"
            echo "Response: $BODY"
        fi
    else
        echo -e "${YELLOW}Skipping credentials test${NC}"
    fi
    echo ""
    
    # Test 4: Get Streams
    echo -e "${YELLOW}Test 4: Get Camera Streams${NC}"
    echo "GET $API_BASE_URL/v1/onvif/streams/$CAMERA_ID"
    RESPONSE=$(curl -s -w "\n%{http_code}" "$API_BASE_URL/v1/onvif/streams/$CAMERA_ID")
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    BODY=$(echo "$RESPONSE" | sed '$d')
    
    if [ "$HTTP_CODE" == "200" ]; then
        echo -e "${GREEN}✓ Get streams successful${NC}"
        echo "Response:"
        echo "$BODY" | python3 -m json.tool 2>/dev/null || echo "$BODY"
        
        # Extract stream URI
        STREAM_URI=$(echo "$BODY" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if isinstance(data, list) and len(data) > 0:
        stream = data[0]
        print(stream.get('uri', ''))
except:
    pass
" 2>/dev/null)
        
        if [ -n "$STREAM_URI" ]; then
            echo ""
            echo -e "${GREEN}Stream URI: $STREAM_URI${NC}"
            echo ""
            echo "To test the stream, use:"
            echo "  ffplay $STREAM_URI"
            echo "  or"
            echo "  vlc $STREAM_URI"
        fi
    else
        echo -e "${RED}✗ Get streams failed (HTTP $HTTP_CODE)${NC}"
        echo "Response: $BODY"
    fi
    echo ""
else
    echo -e "${YELLOW}Skipping credentials and streams tests (no camera found)${NC}"
fi

echo "=========================================="
echo "Test Complete"
echo "=========================================="

