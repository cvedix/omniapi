#!/bin/bash

# ONVIF Full Test Script with Credentials
# Automatically tests discovery, credentials, and streams

SERVER_URL="${1:-http://localhost:8080}"
TIMEOUT="${2:-10}"
USERNAME="${3:-admincvedix}"
PASSWORD="${4:-12345678}"

echo "=========================================="
echo "ONVIF Full Test with Credentials"
echo "=========================================="
echo "Server URL: $SERVER_URL"
echo "Discovery Timeout: ${TIMEOUT}s"
echo "Username: $USERNAME"
echo "Password: ********"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Step 1: Discover
echo -e "${BLUE}[1/4] Starting camera discovery...${NC}"
DISCOVER_RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "${SERVER_URL}/v1/onvif/discover?timeout=${TIMEOUT}")
HTTP_CODE=$(echo "$DISCOVER_RESPONSE" | tail -n1)

if [ "$HTTP_CODE" != "204" ]; then
    echo -e "${RED}✗ Discovery failed - HTTP $HTTP_CODE${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Discovery started${NC}"
echo "Waiting ${TIMEOUT} seconds..."
sleep $TIMEOUT
echo ""

# Step 2: Get cameras
echo -e "${BLUE}[2/4] Getting discovered cameras...${NC}"
CAMERAS_RESPONSE=$(curl -s -w "\n%{http_code}" "${SERVER_URL}/v1/onvif/cameras")
HTTP_CODE=$(echo "$CAMERAS_RESPONSE" | tail -n1)
BODY=$(echo "$CAMERAS_RESPONSE" | sed '$d')

if [ "$HTTP_CODE" != "200" ]; then
    echo -e "${RED}✗ Failed to get cameras - HTTP $HTTP_CODE${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Cameras retrieved${NC}"
echo "$BODY" | python3 -m json.tool 2>/dev/null || echo "$BODY"
echo ""

# Extract first camera ID
CAMERA_ID=$(echo "$BODY" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data[0]['ip'] if isinstance(data, list) and len(data) > 0 and 'ip' in data[0] else data[0]['uuid'] if isinstance(data, list) and len(data) > 0 and 'uuid' in data[0] else '')" 2>/dev/null | tr -d '\n\r' | xargs)

if [ -z "$CAMERA_ID" ]; then
    echo -e "${YELLOW}⚠ No camera ID found in response${NC}"
    exit 0
fi

# Validate and clean camera ID (remove any whitespace, newlines, etc)
CAMERA_ID=$(echo "$CAMERA_ID" | tr -d '\n\r\t ')

# Validate IP format using regex (if it looks like an IP)
if [[ "$CAMERA_ID" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    # More strict validation: each octet should be 0-255
    IFS='.' read -ra OCTETS <<< "$CAMERA_ID"
    VALID_IP=true
    for octet in "${OCTETS[@]}"; do
        if ! [[ "$octet" =~ ^[0-9]+$ ]] || [ "$octet" -gt 255 ] || [ "$octet" -lt 0 ]; then
            VALID_IP=false
            break
        fi
    done
    
    if [ "$VALID_IP" = false ]; then
        echo -e "${RED}✗ Invalid IP format: $CAMERA_ID${NC}"
        echo "  Expected format: 0-255.0-255.0-255.0-255"
        exit 1
    fi
fi

# Debug: Show extracted camera ID with length
echo -e "${GREEN}Using camera ID: '$CAMERA_ID' (length: ${#CAMERA_ID})${NC}"
echo ""

# Step 3: Set credentials
echo -e "${BLUE}[3/4] Setting credentials...${NC}"
# Ensure CAMERA_ID is properly quoted and URL-encoded if needed
CRED_URL="${SERVER_URL}/v1/onvif/camera/${CAMERA_ID}/credentials"
echo "Debug: Request URL: $CRED_URL"
CRED_RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$CRED_URL" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}")

CRED_HTTP_CODE=$(echo "$CRED_RESPONSE" | tail -n1)

if [ "$CRED_HTTP_CODE" == "204" ]; then
    echo -e "${GREEN}✓ Credentials set successfully${NC}"
else
    echo -e "${YELLOW}⚠ Failed to set credentials - HTTP $CRED_HTTP_CODE${NC}"
    CRED_BODY=$(echo "$CRED_RESPONSE" | sed '$d')
    echo "Response: $CRED_BODY"
fi
echo ""

# Step 4: Get streams
echo -e "${BLUE}[4/4] Getting streams...${NC}"
STREAMS_URL="${SERVER_URL}/v1/onvif/streams/${CAMERA_ID}"
STREAMS_RESPONSE=$(curl -s -w "\n%{http_code}" "$STREAMS_URL")
STREAMS_HTTP_CODE=$(echo "$STREAMS_RESPONSE" | tail -n1)
STREAMS_BODY=$(echo "$STREAMS_RESPONSE" | sed '$d')

if [ "$STREAMS_HTTP_CODE" == "200" ]; then
    echo -e "${GREEN}✓ Streams retrieved successfully${NC}"
    echo ""
    echo "Streams:"
    echo "$STREAMS_BODY" | python3 -m json.tool 2>/dev/null || echo "$STREAMS_BODY"
    
    # Extract RTSP URI if available
    RTSP_URI=$(echo "$STREAMS_BODY" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data[0]['uri'] if isinstance(data, list) and len(data) > 0 and 'uri' in data[0] else '')" 2>/dev/null || echo "")
    
    if [ -n "$RTSP_URI" ]; then
        echo ""
        echo -e "${GREEN}RTSP Stream URI:${NC}"
        echo "$RTSP_URI"
        echo ""
        echo "To test the stream, run:"
        echo "  ffplay $RTSP_URI"
        echo "  # or"
        echo "  vlc $RTSP_URI"
    fi
else
    echo -e "${RED}✗ GetStreams failed - HTTP $STREAMS_HTTP_CODE${NC}"
    echo "Response: $STREAMS_BODY"
fi

echo ""
echo "=========================================="
echo "Test Complete"
echo "=========================================="

