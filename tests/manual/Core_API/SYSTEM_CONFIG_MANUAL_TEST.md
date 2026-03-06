# Hướng Dẫn Test Thủ Công - System Config & Preferences API

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Test Cases](#test-cases)
5. [Expected Results](#expected-results)
6. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công các endpoints của System Config & Preferences API, bao gồm:
- **System Configuration**: Quản lý cấu hình hệ thống với metadata
- **System Preferences**: Quản lý preferences từ rtconfig.json
- **System Decoders**: Thông tin về decoders có sẵn trong hệ thống
- **Registry**: Lấy registry key value (optional)
- **Shutdown**: Shutdown hệ thống (optional)

### Base URL
```
http://localhost:8080/v1/core/system
```

---

## Prerequisites

1. **API Server đang chạy**:
   ```bash
   # Kiểm tra server đang chạy
   curl http://localhost:8080/v1/core/health
   ```

2. **Công cụ cần thiết**:
   - `curl` - để gửi HTTP requests
   - `jq` (optional) - để format JSON output đẹp hơn
   - Text editor - để chỉnh sửa JSON files

3. **File cấu hình** (optional):
   - `config/rtconfig.json` hoặc `config/rtconfig.json.example`
   - `config/system_config.json` (sẽ được tạo tự động nếu không có)

---

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/system"
```

---

## Test Cases

### 1. GET System Config - Lấy Cấu Hình Hệ Thống

#### Test Case 1.1: Get System Config (Success)
```bash
echo "=== Test 1.1: Get System Config ==="
curl -X GET "${BASE_URL}/config" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200, JSON response với structure:
```json
{
  "systemConfig": [
    {
      "fieldId": "voice6",
      "displayName": "voice n. 6",
      "type": "options",
      "value": "solutions",
      "group": "groupBy",
      "availableValues": [
        {
          "displayName": "securt 1 version 1.0",
          "value": "securt"
        }
      ]
    }
  ]
}
```

#### Test Case 1.2: Get System Config với Pretty Print
```bash
echo "=== Test 1.2: Get System Config (Pretty) ==="
curl -X GET "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" | jq .
```

---

### 2. PUT System Config - Cập Nhật Cấu Hình Hệ Thống

#### Test Case 2.1: Update System Config (Success)
```bash
echo "=== Test 2.1: Update System Config ==="
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d '{
    "systemConfig": [
      {
        "fieldId": "voice6",
        "value": "securt"
      }
    ]
  }' | jq .
```

**Expected**: Status 200, JSON response:
```json
{
  "status": "success",
  "message": "System config updated successfully"
}
```

#### Test Case 2.2: Update System Config với Invalid FieldId
```bash
echo "=== Test 2.2: Update System Config (Invalid FieldId) ==="
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

**Expected**: Status 406 Not Acceptable, error message

#### Test Case 2.3: Update System Config với Invalid Value (Options Type)
```bash
echo "=== Test 2.3: Update System Config (Invalid Value) ==="
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d '{
    "systemConfig": [
      {
        "fieldId": "voice6",
        "value": "invalid_option"
      }
    ]
  }' | jq .
```

**Expected**: Status 406 Not Acceptable (nếu validation được implement)

#### Test Case 2.4: Update System Config với Empty Body
```bash
echo "=== Test 2.4: Update System Config (Empty Body) ==="
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d '{}' | jq .
```

**Expected**: Status 400 Bad Request hoặc 406 Not Acceptable

---

### 3. GET System Preferences - Lấy Preferences từ rtconfig.json

#### Test Case 3.1: Get Preferences (Success)
```bash
echo "=== Test 3.1: Get System Preferences ==="
curl -X GET "${BASE_URL}/preferences" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200, JSON response với preferences:
```json
{
  "vms.show_area_crossing": true,
  "vms.show_line_crossing": true,
  "vms.show_intrusion": true,
  "vms.show_crowding": true,
  "vms.show_loitering": true,
  "vms.show_object_left": true,
  "vms.show_object_guarding": true,
  "vms.show_tailgating": true,
  "vms.show_fallen_person": true,
  "vms.show_armed_person": true,
  "vms.show_access_control_plate": true,
  "vms.show_access_control_face": true,
  "vms.show_appearance_search": true,
  "vms.show_identities": true,
  "vms.show_remote_mode": true,
  "vms.show_thermal_support": true,
  "vms.show_active_detection": true,
  "vms.show_error_reporting": true,
  "vms.show_trial_licenses": true,
  "vms.hardware_decoding": true,
  "global.default_performance_mode": "Performance"
}
```

#### Test Case 3.2: Get Preferences với Filter (nếu hỗ trợ)
```bash
echo "=== Test 3.2: Get Preferences (All) ==="
curl -X GET "${BASE_URL}/preferences" | jq 'keys'
```

**Expected**: Danh sách tất cả keys

---

### 4. GET System Decoders - Lấy Thông Tin Decoders

#### Test Case 4.1: Get Decoders (Success)
```bash
echo "=== Test 4.1: Get System Decoders ==="
curl -X GET "${BASE_URL}/decoders" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200, JSON response với decoder information:
```json
{
  "nvidia": {
    "hevc": 1,
    "h264": 1
  }
}
```

Hoặc nếu không có NVIDIA:
```json
{
  "intel": {
    "hevc": 1,
    "h264": 1
  }
}
```

Hoặc empty object nếu không có hardware decoder:
```json
{}
```

#### Test Case 4.2: Get Decoders - Check Specific Platform
```bash
echo "=== Test 4.2: Check NVIDIA Decoders ==="
curl -X GET "${BASE_URL}/decoders" | jq '.nvidia // "NVIDIA not available"'

echo "=== Test 4.3: Check Intel Decoders ==="
curl -X GET "${BASE_URL}/decoders" | jq '.intel // "Intel not available"'
```

---

### 5. GET Registry - Lấy Registry Key Value (Optional)

#### Test Case 5.1: Get Registry với Key Parameter
```bash
echo "=== Test 5.1: Get Registry Key Value ==="
curl -X GET "${BASE_URL}/registry?key=test" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200, JSON response:
```json
{
  "test": {
    "value_bool": true,
    "value_float": 3.14,
    "value_int": 42,
    "value_string": "hello"
  },
  "system": {
    "version": "1.0.0"
  }
}
```

#### Test Case 5.2: Get Registry không có Key Parameter
```bash
echo "=== Test 5.2: Get Registry (Missing Key) ==="
curl -X GET "${BASE_URL}/registry" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 400 Bad Request, error message về missing parameter

#### Test Case 5.3: Get Registry với Invalid Key
```bash
echo "=== Test 5.3: Get Registry (Invalid Key) ==="
curl -X GET "${BASE_URL}/registry?key=nonexistent" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 404 Not Found hoặc empty object

---

### 6. POST System Shutdown - Shutdown Hệ Thống (Optional)

⚠️ **CẢNH BÁO**: Endpoint này sẽ shutdown server, chỉ test khi cần thiết!

#### Test Case 6.1: Shutdown System
```bash
echo "=== Test 6.1: Shutdown System ==="
curl -X POST "${BASE_URL}/shutdown" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200, response trước khi shutdown:
```json
{
  "status": "success",
  "message": "System shutdown initiated"
}
```

Sau đó server sẽ shutdown sau 1 giây.

---

## Test Sequence - Full Flow

### Sequence 1: Complete System Config Flow
```bash
#!/bin/bash

SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/system"

echo "=========================================="
echo "System Config & Preferences API Test"
echo "=========================================="
echo ""

# Step 1: Get System Config
echo "=== Step 1: Get System Config ==="
CONFIG_RESPONSE=$(curl -s -X GET "${BASE_URL}/config")
echo "$CONFIG_RESPONSE" | jq .
echo ""

# Step 2: Get Preferences
echo "=== Step 2: Get System Preferences ==="
PREFERENCES_RESPONSE=$(curl -s -X GET "${BASE_URL}/preferences")
echo "$PREFERENCES_RESPONSE" | jq .
echo ""

# Step 3: Get Decoders
echo "=== Step 3: Get System Decoders ==="
DECODERS_RESPONSE=$(curl -s -X GET "${BASE_URL}/decoders")
echo "$DECODERS_RESPONSE" | jq .
echo ""

# Step 4: Update System Config (nếu có fieldId hợp lệ)
echo "=== Step 4: Update System Config ==="
FIELD_ID=$(echo "$CONFIG_RESPONSE" | jq -r '.systemConfig[0].fieldId // empty')
if [ ! -z "$FIELD_ID" ]; then
  ORIGINAL_VALUE=$(echo "$CONFIG_RESPONSE" | jq -r ".systemConfig[0].value // empty")
  echo "Updating fieldId: $FIELD_ID"
  curl -s -X PUT "${BASE_URL}/config" \
    -H "Content-Type: application/json" \
    -d "{
      \"systemConfig\": [
        {
          \"fieldId\": \"$FIELD_ID\",
          \"value\": \"$ORIGINAL_VALUE\"
        }
      ]
    }" | jq .
else
  echo "No config entities found to update"
fi
echo ""

echo "=========================================="
echo "Test Complete"
echo "=========================================="
```

### Sequence 2: Quick Test All Endpoints
```bash
#!/bin/bash

SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/system"

echo "Quick Test - System Config API"
echo "================================"

# Test all endpoints
for endpoint in "config" "preferences" "decoders"; do
  echo ""
  echo "Testing: GET ${BASE_URL}/${endpoint}"
  curl -s -X GET "${BASE_URL}/${endpoint}" | jq . | head -20
  echo "---"
done

# Test registry with key
echo ""
echo "Testing: GET ${BASE_URL}/registry?key=test"
curl -s -X GET "${BASE_URL}/registry?key=test" | jq . | head -20
```

---

## Expected Results Summary

| Endpoint | Method | Expected Status | Key Fields |
|----------|--------|-----------------|------------|
| `/config` | GET | 200 | `systemConfig` array |
| `/config` | PUT | 200 | `status`, `message` |
| `/preferences` | GET | 200 | Preferences object với dot-notation keys |
| `/decoders` | GET | 200 | Decoder object (nvidia/intel) |
| `/registry` | GET | 200/400 | Registry object hoặc error |
| `/shutdown` | POST | 200 | `status`, `message` |

---

## Troubleshooting

### Problem 1: Server không phản hồi

**Triệu chứng**:
```bash
curl: (7) Failed to connect to localhost port 8080: Connection refused
```

**Giải pháp**:
1. Kiểm tra server đang chạy:
   ```bash
   ps aux | grep edgeos-api
   ```

2. Khởi động server:
   ```bash
   cd /path/to/edgeos-api
   ./scripts/load_env.sh
   ```

3. Kiểm tra port:
   ```bash
   netstat -tuln | grep 8080
   # hoặc
   lsof -i :8080
   ```

### Problem 2: Config không load được

**Triệu chứng**:
- Response trả về empty array hoặc default values
- Log có thông báo "Config file not found"

**Giải pháp**:
1. Kiểm tra file config tồn tại:
   ```bash
   ls -la config/system_config.json
   ls -la config/rtconfig.json
   ```

2. Tạo file config nếu chưa có:
   ```bash
   # Copy example file
   cp config/rtconfig.json.example config/rtconfig.json
   ```

3. Kiểm tra permissions:
   ```bash
   chmod 644 config/*.json
   ```

### Problem 3: Decoders không detect được

**Triệu chứng**:
- Response trả về empty object `{}`
- Không có NVIDIA hoặc Intel decoders

**Giải pháp**:
1. Kiểm tra NVIDIA hardware:
   ```bash
   nvidia-smi
   # hoặc
   lspci | grep -i nvidia
   ```

2. Kiểm tra Intel Quick Sync:
   ```bash
   lspci | grep -i vga
   # Kiểm tra có Intel GPU không
   ```

3. Kiểm tra libraries:
   ```bash
   # NVIDIA
   ls -la /usr/lib/x86_64-linux-gnu/libcuda.so*
   ls -la /usr/lib/x86_64-linux-gnu/libnvidia-encode.so*

   # Intel
   ls -la /usr/lib/x86_64-linux-gnu/libmfx.so*
   ls -la /usr/lib/x86_64-linux-gnu/libva.so*
   ```

### Problem 4: Update Config trả về 406 Not Acceptable

**Triệu chứng**:
```json
{
  "error": "Not Acceptable",
  "message": "Failed to update system config"
}
```

**Giải pháp**:
1. Kiểm tra fieldId có tồn tại:
   ```bash
   curl -s "${BASE_URL}/config" | jq '.systemConfig[].fieldId'
   ```

2. Kiểm tra value có hợp lệ (nếu type là "options"):
   ```bash
   curl -s "${BASE_URL}/config" | jq '.systemConfig[] | select(.fieldId=="voice6") | .availableValues'
   ```

3. Kiểm tra file permissions:
   ```bash
   ls -la config/system_config.json
   # Cần có quyền write
   ```

### Problem 5: Preferences không load từ rtconfig.json

**Triệu chứng**:
- Response chỉ có default values
- Không có preferences từ file

**Giải pháp**:
1. Kiểm tra file tồn tại và có nội dung:
   ```bash
   cat config/rtconfig.json
   ```

2. Kiểm tra JSON format hợp lệ:
   ```bash
   jq . config/rtconfig.json
   ```

3. Kiểm tra file path trong logs:
   ```bash
   # Xem log khi server khởi động
   # Tìm dòng "[PreferencesManager] Loaded preferences from ..."
   ```

---

## Advanced Testing

### Test với Different Content Types
```bash
# Test với Accept header
curl -X GET "${BASE_URL}/config" \
  -H "Accept: application/json" | jq .

# Test với Content-Type
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d @config_update.json
```

### Test CORS Headers
```bash
# Test OPTIONS request
curl -X OPTIONS "${BASE_URL}/config" \
  -H "Origin: http://example.com" \
  -H "Access-Control-Request-Method: GET" \
  -v
```

**Expected**: Response có headers:
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: GET, PUT, POST, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type`

### Test Error Handling
```bash
# Test với invalid JSON
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d '{invalid json}' | jq .

# Test với missing required fields
curl -X PUT "${BASE_URL}/config" \
  -H "Content-Type: application/json" \
  -d '{"systemConfig": []}' | jq .
```

---

## Performance Testing

### Test Response Time
```bash
# Test response time cho mỗi endpoint
for endpoint in "config" "preferences" "decoders"; do
  echo "Testing: ${BASE_URL}/${endpoint}"
  time curl -s -X GET "${BASE_URL}/${endpoint}" > /dev/null
done
```

### Test Concurrent Requests
```bash
# Test với nhiều requests đồng thời
for i in {1..10}; do
  curl -s -X GET "${BASE_URL}/config" > /dev/null &
done
wait
echo "All requests completed"
```

---

## Notes

1. **File Locations**:
   - System config: `config/system_config.json` (tự động tạo nếu không có)
   - Preferences: `config/rtconfig.json` hoặc `./rtconfig.json`

2. **Default Values**:
   - Nếu file không tồn tại, system sẽ sử dụng default values
   - Default values được định nghĩa trong code

3. **Thread Safety**:
   - Tất cả managers đều thread-safe
   - Có thể gọi từ nhiều threads đồng thời

4. **Persistence**:
   - System config được lưu vào file khi update
   - Preferences được load từ file khi khởi động

---

## Related Documentation

- [DEVELOPMENT.md](../../../docs/DEVELOPMENT.md) - Development guide
- [TASK-004-System-Config.md](../../../task/edgeos-api_task/TASK-004-System-Config.md) - Task specification
- [API Specifications](../../../api-specs/) - OpenAPI specifications

