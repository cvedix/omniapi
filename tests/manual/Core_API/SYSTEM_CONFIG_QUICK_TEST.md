# System Config API - Quick Test Commands

Copy-paste các lệnh sau để test nhanh System Config & Preferences API.

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/system"
```

---

## Quick Test Sequence

### 1. Get System Config
```bash
curl -X GET "${BASE_URL}/config" | jq .
```

### 2. Get System Preferences
```bash
curl -X GET "${BASE_URL}/preferences" | jq .
```

### 3. Get System Decoders
```bash
curl -X GET "${BASE_URL}/decoders" | jq .
```

### 4. Get Registry (với key)
```bash
curl -X GET "${BASE_URL}/registry?key=test" | jq .
```

---

## One-Liner Full Test

```bash
SERVER="http://localhost:8080" && \
BASE_URL="${SERVER}/v1/core/system" && \
echo "=== Step 1: Get System Config ===" && \
curl -s -X GET "${BASE_URL}/config" | jq . && \
echo -e "\n=== Step 2: Get Preferences ===" && \
curl -s -X GET "${BASE_URL}/preferences" | jq . && \
echo -e "\n=== Step 3: Get Decoders ===" && \
curl -s -X GET "${BASE_URL}/decoders" | jq . && \
echo -e "\n=== Step 4: Get Registry ===" && \
curl -s -X GET "${BASE_URL}/registry?key=test" | jq .
```

---

## Update Config Test

### Get current config first
```bash
CONFIG=$(curl -s -X GET "${BASE_URL}/config")
echo "$CONFIG" | jq .
```

### Extract fieldId và value
```bash
FIELD_ID=$(echo "$CONFIG" | jq -r '.systemConfig[0].fieldId // empty')
ORIGINAL_VALUE=$(echo "$CONFIG" | jq -r '.systemConfig[0].value // empty')
echo "FieldId: $FIELD_ID"
echo "Value: $ORIGINAL_VALUE"
```

### Update config
```bash
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d "{
    \"systemConfig\": [
      {
        \"fieldId\": \"$FIELD_ID\",
        \"value\": \"$ORIGINAL_VALUE\"
      }
    ]
  }" | jq .
```

---

## Check Specific Values

### Check if NVIDIA decoders available
```bash
curl -s -X GET "${BASE_URL}/decoders" | jq '.nvidia // "NVIDIA not available"'
```

### Check if Intel decoders available
```bash
curl -s -X GET "${BASE_URL}/decoders" | jq '.intel // "Intel not available"'
```

### List all preference keys
```bash
curl -s -X GET "${BASE_URL}/preferences" | jq 'keys'
```

### Get specific preference value
```bash
curl -s -X GET "${BASE_URL}/preferences" | jq '.["vms.show_area_crossing"]'
```

---

## Error Testing

### Test với invalid fieldId
```bash
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d '{
    "systemConfig": [
      {
        "fieldId": "invalid_field",
        "value": "test"
      }
    ]
  }' | jq .
```

### Test với missing key parameter
```bash
curl -X GET "${BASE_URL}/registry" | jq .
```

### Test với empty body
```bash
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d '{}' | jq .
```

---

## CORS Test

### Test OPTIONS request
```bash
curl -X OPTIONS "${BASE_URL}/config" \
  -H "Origin: http://example.com" \
  -H "Access-Control-Request-Method: GET" \
  -v 2>&1 | grep -i "access-control"
```

---

## Performance Test

### Test response time
```bash
echo "Config endpoint:"
time curl -s -X GET "${BASE_URL}/config" > /dev/null

echo "Preferences endpoint:"
time curl -s -X GET "${BASE_URL}/preferences" > /dev/null

echo "Decoders endpoint:"
time curl -s -X GET "${BASE_URL}/decoders" > /dev/null
```

---

## Save Responses to Files

### Save all responses
```bash
mkdir -p test_outputs

curl -s -X GET "${BASE_URL}/config" | jq . > test_outputs/config.json
curl -s -X GET "${BASE_URL}/preferences" | jq . > test_outputs/preferences.json
curl -s -X GET "${BASE_URL}/decoders" | jq . > test_outputs/decoders.json
curl -s -X GET "${BASE_URL}/registry?key=test" | jq . > test_outputs/registry.json

echo "Responses saved to test_outputs/"
```

---

## Compare Before/After Update

### Save before
```bash
curl -s -X GET "${BASE_URL}/config" | jq . > before.json
```

### Update config
```bash
FIELD_ID=$(curl -s -X GET "${BASE_URL}/config" | jq -r '.systemConfig[0].fieldId')
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d "{
    \"systemConfig\": [
      {
        \"fieldId\": \"$FIELD_ID\",
        \"value\": \"new_value\"
      }
    ]
  }"
```

### Save after
```bash
curl -s -X GET "${BASE_URL}/config" | jq . > after.json
```

### Compare
```bash
diff -u before.json after.json
# hoặc
jq -S . before.json > before_sorted.json
jq -S . after.json > after_sorted.json
diff before_sorted.json after_sorted.json
```

---

## Health Check Integration

### Check health trước khi test
```bash
curl -s "${SERVER}/v1/core/health" | jq .
```

### Test nếu health OK
```bash
HEALTH=$(curl -s "${SERVER}/v1/core/health" | jq -r '.status')
if [ "$HEALTH" = "healthy" ]; then
  echo "Server is healthy, running tests..."
  curl -s -X GET "${BASE_URL}/config" | jq .
else
  echo "Server is not healthy, skipping tests"
fi
```

---

## Batch Test Script

```bash
#!/bin/bash

SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/system"

echo "System Config API - Batch Test"
echo "==============================="
echo ""

# Test 1: Get Config
echo "[1/5] Testing GET /config"
RESPONSE=$(curl -s -w "\nHTTP_CODE:%{http_code}" -X GET "${BASE_URL}/config")
HTTP_CODE=$(echo "$RESPONSE" | grep "HTTP_CODE" | cut -d: -f2)
BODY=$(echo "$RESPONSE" | sed '/HTTP_CODE/d')
if [ "$HTTP_CODE" = "200" ]; then
  echo "✓ PASS - Status: $HTTP_CODE"
else
  echo "✗ FAIL - Status: $HTTP_CODE"
fi
echo ""

# Test 2: Get Preferences
echo "[2/5] Testing GET /preferences"
RESPONSE=$(curl -s -w "\nHTTP_CODE:%{http_code}" -X GET "${BASE_URL}/preferences")
HTTP_CODE=$(echo "$RESPONSE" | grep "HTTP_CODE" | cut -d: -f2)
if [ "$HTTP_CODE" = "200" ]; then
  echo "✓ PASS - Status: $HTTP_CODE"
else
  echo "✗ FAIL - Status: $HTTP_CODE"
fi
echo ""

# Test 3: Get Decoders
echo "[3/5] Testing GET /decoders"
RESPONSE=$(curl -s -w "\nHTTP_CODE:%{http_code}" -X GET "${BASE_URL}/decoders")
HTTP_CODE=$(echo "$RESPONSE" | grep "HTTP_CODE" | cut -d: -f2)
if [ "$HTTP_CODE" = "200" ]; then
  echo "✓ PASS - Status: $HTTP_CODE"
else
  echo "✗ FAIL - Status: $HTTP_CODE"
fi
echo ""

# Test 4: Get Registry
echo "[4/5] Testing GET /registry?key=test"
RESPONSE=$(curl -s -w "\nHTTP_CODE:%{http_code}" -X GET "${BASE_URL}/registry?key=test")
HTTP_CODE=$(echo "$RESPONSE" | grep "HTTP_CODE" | cut -d: -f2)
if [ "$HTTP_CODE" = "200" ]; then
  echo "✓ PASS - Status: $HTTP_CODE"
else
  echo "✗ FAIL - Status: $HTTP_CODE"
fi
echo ""

# Test 5: Update Config (nếu có config entities)
echo "[5/5] Testing PUT /config"
CONFIG=$(curl -s -X GET "${BASE_URL}/config")
FIELD_ID=$(echo "$CONFIG" | jq -r '.systemConfig[0].fieldId // empty')
if [ ! -z "$FIELD_ID" ]; then
  ORIGINAL_VALUE=$(echo "$CONFIG" | jq -r ".systemConfig[0].value // empty")
  RESPONSE=$(curl -s -w "\nHTTP_CODE:%{http_code}" -X PUT "${BASE_URL}/config" \
    -H "Content-Type: application/json" \
    -d "{
      \"systemConfig\": [
        {
          \"fieldId\": \"$FIELD_ID\",
          \"value\": \"$ORIGINAL_VALUE\"
        }
      ]
    }")
  HTTP_CODE=$(echo "$RESPONSE" | grep "HTTP_CODE" | cut -d: -f2)
  if [ "$HTTP_CODE" = "200" ]; then
    echo "✓ PASS - Status: $HTTP_CODE"
  else
    echo "✗ FAIL - Status: $HTTP_CODE"
  fi
else
  echo "⚠ SKIP - No config entities found"
fi
echo ""

echo "==============================="
echo "Batch Test Complete"
echo "==============================="
```

Lưu script trên vào file `test_system_config.sh`, chmod +x và chạy:
```bash
chmod +x test_system_config.sh
./test_system_config.sh
```

