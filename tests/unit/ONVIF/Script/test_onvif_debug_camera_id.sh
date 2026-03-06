#!/bin/bash

# Debug script to check camera ID extraction

SERVER_URL="${1:-http://localhost:8080}"

echo "=========================================="
echo "ONVIF Camera ID Debug"
echo "=========================================="
echo ""

# Get cameras
echo "1. Getting cameras..."
CAMERAS_RESPONSE=$(curl -s "${SERVER_URL}/v1/onvif/cameras")
echo "Response:"
echo "$CAMERAS_RESPONSE" | python3 -m json.tool 2>/dev/null || echo "$CAMERAS_RESPONSE"
echo ""

# Extract camera ID using same logic as script
echo "2. Extracting camera ID..."
CAMERA_ID=$(echo "$CAMERAS_RESPONSE" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if isinstance(data, list) and len(data) > 0:
        cam = data[0]
        if 'ip' in cam and cam['ip']:
            print(cam['ip'])
        elif 'uuid' in cam and cam['uuid']:
            print(cam['uuid'])
        else:
            print('')
    else:
        print('')
except Exception as e:
    print('Error:', e)
    sys.exit(1)
" 2>&1)

echo "Extracted Camera ID: '$CAMERA_ID'"
echo "Length: ${#CAMERA_ID}"
echo ""

# Check if it matches expected format
if [[ "$CAMERA_ID" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "✓ Camera ID is valid IP format"
else
    echo "✗ Camera ID is NOT valid IP format"
fi

echo ""

# Try to set credentials with extracted ID
if [ -n "$CAMERA_ID" ]; then
    echo "3. Testing setCredentials with extracted ID..."
    echo "URL: ${SERVER_URL}/v1/onvif/camera/${CAMERA_ID}/credentials"
    echo ""
    
    CRED_RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "${SERVER_URL}/v1/onvif/camera/${CAMERA_ID}/credentials" \
        -H "Content-Type: application/json" \
        -d '{"username":"admincvedix","password":"12345678"}')
    
    HTTP_CODE=$(echo "$CRED_RESPONSE" | tail -n1)
    BODY=$(echo "$CRED_RESPONSE" | sed '$d')
    
    if [ "$HTTP_CODE" == "204" ]; then
        echo "✓ Credentials set successfully"
    else
        echo "✗ Failed - HTTP $HTTP_CODE"
        echo "Response: $BODY"
    fi
fi

echo ""
echo "=========================================="

