#!/bin/bash

# ONVIF Debug Test Script
# This script tests ONVIF discovery and shows detailed logs

SERVER_URL="${1:-http://localhost:8080}"
TIMEOUT="${2:-10}"

echo "=========================================="
echo "ONVIF Camera Discovery - Debug Mode"
echo "=========================================="
echo "Server URL: $SERVER_URL"
echo "Discovery Timeout: ${TIMEOUT}s"
echo ""
echo "NOTE: Make sure API logging is enabled in your config"
echo "      Check server logs for detailed debug information"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Step 1: Discover
echo -e "${BLUE}Step 1: Starting discovery...${NC}"
echo "Calling: curl -X POST ${SERVER_URL}/v1/onvif/discover?timeout=${TIMEOUT}"
echo ""

DISCOVER_RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "${SERVER_URL}/v1/onvif/discover?timeout=${TIMEOUT}")

HTTP_CODE=$(echo "$DISCOVER_RESPONSE" | tail -n1)

if [ "$HTTP_CODE" == "204" ]; then
    echo -e "${GREEN}✓ Discovery started (204 No Content)${NC}"
else
    echo -e "${RED}✗ Discovery failed - HTTP $HTTP_CODE${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}Waiting ${TIMEOUT} seconds for discovery...${NC}"
echo "Check server logs for:"
echo "  - [ONVIFDiscovery] Sent Probe message"
echo "  - [ONVIFDiscovery] Received ProbeMatch from ..."
echo "  - [ONVIFDiscovery] Getting device information from: ..."
echo "  - [ONVIFDiscovery] GetDeviceInformation response length: ..."
echo "  - [ONVIFDiscovery] Parsed device info - Manufacturer: ..., Model: ..."
echo ""

sleep $TIMEOUT
echo ""

# Step 2: Get cameras
echo -e "${BLUE}Step 2: Getting discovered cameras...${NC}"
echo "Calling: curl ${SERVER_URL}/v1/onvif/cameras"
echo ""

CAMERAS_RESPONSE=$(curl -s -w "\n%{http_code}" "${SERVER_URL}/v1/onvif/cameras")

HTTP_CODE=$(echo "$CAMERAS_RESPONSE" | tail -n1)
BODY=$(echo "$CAMERAS_RESPONSE" | sed '$d')

if [ "$HTTP_CODE" == "200" ]; then
    echo -e "${GREEN}✓ Successfully retrieved cameras${NC}"
    echo ""
    echo "Response:"
    echo "$BODY" | python3 -m json.tool 2>/dev/null || echo "$BODY"
    echo ""
    
    # Check for empty fields
    if echo "$BODY" | grep -q '"manufacturer":""'; then
        echo -e "${YELLOW}⚠ WARNING: Camera found but manufacturer is empty${NC}"
        echo ""
        echo "Possible causes:"
        echo "  1. GetDeviceInformation request failed"
        echo "  2. Camera requires authentication"
        echo "  3. XML response format is different than expected"
        echo ""
        echo "Check server logs for:"
        echo "  - [ONVIFDiscovery] GetDeviceInformation failed - empty response"
        echo "  - [ONVIFDiscovery] Failed to parse device information"
        echo "  - [ONVIFHttpClient] HTTP POST to ... failed"
    fi
    
    # Extract camera ID
    CAMERA_ID=$(echo "$BODY" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data[0]['ip'] if isinstance(data, list) and len(data) > 0 else '')" 2>/dev/null || echo "")
    
    if [ -n "$CAMERA_ID" ]; then
        echo ""
        echo -e "${BLUE}Step 3: Setting credentials for camera $CAMERA_ID${NC}"
        echo "Username: admincvedix"
        echo "Calling: curl -X POST ${SERVER_URL}/v1/onvif/camera/${CAMERA_ID}/credentials"
        echo ""
        
        CRED_RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "${SERVER_URL}/v1/onvif/camera/${CAMERA_ID}/credentials" \
            -H "Content-Type: application/json" \
            -d '{"username":"admincvedix","password":"12345678"}')
        
        CRED_HTTP_CODE=$(echo "$CRED_RESPONSE" | tail -n1)
        
        if [ "$CRED_HTTP_CODE" == "204" ]; then
            echo -e "${GREEN}✓ Credentials set successfully (204 No Content)${NC}"
        else
            echo -e "${YELLOW}⚠ Failed to set credentials - HTTP $CRED_HTTP_CODE${NC}"
            CRED_BODY=$(echo "$CRED_RESPONSE" | sed '$d')
            echo "Response: $CRED_BODY"
        fi
        
        echo ""
        echo -e "${BLUE}Step 4: Testing GetStreams for camera $CAMERA_ID${NC}"
        echo "Calling: curl ${SERVER_URL}/v1/onvif/streams/${CAMERA_ID}"
        echo ""
        
        STREAMS_RESPONSE=$(curl -s -w "\n%{http_code}" "${SERVER_URL}/v1/onvif/streams/${CAMERA_ID}")
        STREAMS_HTTP_CODE=$(echo "$STREAMS_RESPONSE" | tail -n1)
        STREAMS_BODY=$(echo "$STREAMS_RESPONSE" | sed '$d')
        
        if [ "$STREAMS_HTTP_CODE" == "200" ]; then
            echo -e "${GREEN}✓ Streams retrieved successfully${NC}"
            echo ""
            echo "Streams:"
            echo "$STREAMS_BODY" | python3 -m json.tool 2>/dev/null || echo "$STREAMS_BODY"
        else
            echo -e "${YELLOW}⚠ GetStreams failed - HTTP $STREAMS_HTTP_CODE${NC}"
            echo "Response: $STREAMS_BODY"
            echo ""
            echo "This might be because:"
            echo "  - Camera requires authentication (credentials may be incorrect)"
            echo "  - Camera doesn't support Media service"
            echo "  - Check server logs for detailed error messages"
        fi
    fi
else
    echo -e "${RED}✗ Failed to get cameras - HTTP $HTTP_CODE${NC}"
    echo "Response: $BODY"
fi

echo ""
echo "=========================================="
echo "Debug Test Complete"
echo "=========================================="
echo ""
echo "To see detailed logs, check:"
echo "  - Server console output"
echo "  - Log files (if configured)"
echo ""
echo "If manufacturer/model are empty, check logs for:"
echo "  - HTTP POST errors"
echo "  - XML parsing errors"
echo "  - Response format issues"

