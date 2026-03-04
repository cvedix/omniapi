# Unit Tests - Edge AI API

Thư mục này chứa các unit tests tự động sử dụng Google Test framework, được tổ chức theo từng tính năng lớn.

## Cấu Trúc

```
unit/
├── Core_API/              # Tests cho Core API handlers
│   ├── test_health_handler.cpp
│   ├── test_version_handler.cpp
│   ├── test_swagger_handler.cpp
│   ├── test_watchdog_handler.cpp
│   ├── test_endpoints_handler.cpp
│   ├── test_config_handler.cpp
│   ├── test_system_info_handler.cpp
│   ├── test_system_handler.cpp
│   ├── test_system_config_manager.cpp
│   ├── test_preferences_manager.cpp
│   ├── test_decoder_detector.cpp
│   └── test_metrics_handler.cpp
│
├── Instance_Management/   # Tests cho Instance Management
│   ├── test_create_instance_handler.cpp
│   ├── test_instance_storage.cpp
│   ├── test_instance_update.cpp
│   ├── test_instance_status_summary.cpp
│   ├── test_instance_get_config.cpp
│   └── test_instance_configure_stream.cpp
│
├── Recognition/           # Tests cho Recognition API
│   └── test_recognition_handler.cpp
│
├── Solutions/             # Tests cho Solutions
│   └── test_solution_handler.cpp
│
├── Groups/                # Tests cho Groups
│   └── test_group_handler.cpp
│
├── Nodes/                 # Tests cho Nodes
│   ├── test_node_handler.cpp
│   └── test_node_pool_handler.cpp
│
├── Analytics/             # Tests cho Analytics features
│   ├── test_lines_handler.cpp
│   ├── test_jams_handler.cpp
│   ├── test_stops_handler.cpp
│   ├── test_ba_jam_detection_params.cpp
│   ├── test_ba_jam_pipeline.cpp
│   ├── test_securt_handler.cpp
│   ├── test_area_handler.cpp
│   └── test_ai_handler.cpp (commented out - requires C++20)
│
├── ONVIF/                 # Tests cho ONVIF (nếu có)
│
├── Config/                # Tests cho Config (nếu có)
│
└── test_main.cpp          # Entry point cho tất cả tests
```

## Tổng Quan Tests

### Core_API Tests
- **HealthHandler**: Tests cho health check endpoint
- **VersionHandler**: Tests cho version information endpoint
- **SwaggerHandler**: Tests cho Swagger UI và OpenAPI endpoints
- **WatchdogHandler**: Tests cho watchdog status
- **EndpointsHandler**: Tests cho endpoints statistics
- **ConfigHandler**: Tests cho configuration management
- **SystemInfoHandler**: Tests cho system information
- **SystemHandler**: Tests cho system config, preferences, và decoders endpoints
- **SystemConfigManager**: Tests cho system configuration manager
- **PreferencesManager**: Tests cho preferences manager (rtconfig.json)
- **DecoderDetector**: Tests cho hardware decoder detection
- **MetricsHandler**: Tests cho Prometheus metrics

### Instance_Management Tests
- **CreateInstanceHandler**: Tests cho tạo instance mới
- **InstanceStorage**: Tests cho instance storage operations
- **InstanceUpdate**: Tests cho cập nhật instance
- **InstanceStatusSummary**: Tests cho status summary
- **InstanceGetConfig**: Tests cho lấy configuration
- **InstanceConfigureStream**: Tests cho cấu hình stream

### Recognition Tests
- **RecognitionHandler**: Tests cho face recognition API

### Solutions Tests
- **SolutionHandler**: Tests cho solution management

### Groups Tests
- **GroupHandler**: Tests cho group management

### Nodes Tests
- **NodeHandler**: Tests cho node management
- **NodePoolHandler**: Tests cho node pool management

### Analytics Tests
- **LinesHandler**: Tests cho line detection
- **JamsHandler**: Tests cho traffic jam detection
- **StopsHandler**: Tests cho stop detection
- **BA Jam Detection**: Tests cho BA jam detection parameters
- **BA Jam Pipeline**: Tests cho BA jam pipeline
- **SecuRT Handler**: Tests cho SecuRT features
- **Area Handler**: Tests cho area management

## Build và Chạy Tests

### Build Tests

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
```

### Chạy Tests

**Cách 1: Sử dụng script**
```bash
./scripts/run_tests.sh
```

**Cách 2: Chạy trực tiếp**
```bash
cd build
./bin/edgeos-api_tests
```

**Cách 3: Sử dụng CTest**
```bash
cd build
ctest
```

**Cách 4: Chạy tests cụ thể**
```bash
cd build
./bin/edgeos-api_tests --gtest_filter=HealthHandler.*
```

## Test Coverage

Mỗi test suite bao gồm:
- ✅ Valid requests testing
- ✅ Invalid requests testing
- ✅ Edge cases testing
- ✅ Error handling testing
- ✅ Response format validation

## Thêm Test Mới

Khi thêm test mới:

1. **Xác định tính năng**: Xác định tính năng lớn mà handler thuộc về
2. **Tạo file test**: Tạo `test_<handler_name>.cpp` trong thư mục `unit/<Feature>/`
3. **Cập nhật CMakeLists.txt**: Thêm file vào `TEST_SOURCES` trong `tests/CMakeLists.txt`
4. **Viết tests**: Viết tests cho:
   - Valid requests
   - Invalid requests
   - Edge cases
   - Error handling
   - Response validation

### Ví dụ: Thêm test mới

```cpp
// unit/Core_API/test_new_handler.cpp
#include <gtest/gtest.h>
#include "api/new_handler.h"

class NewHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }
    
    void TearDown() override {
        // Cleanup
    }
};

TEST_F(NewHandlerTest, ValidRequest) {
    // Test valid request
}

TEST_F(NewHandlerTest, InvalidRequest) {
    // Test invalid request
}
```

Sau đó thêm vào `tests/CMakeLists.txt`:
```cmake
unit/Core_API/test_new_handler.cpp
```

## Best Practices

1. **Tests độc lập**: Mỗi test không phụ thuộc vào test khác
2. **Cleanup**: Luôn cleanup trong TearDown()
3. **Naming**: Đặt tên test rõ ràng, mô tả được test case
4. **Assertions**: Sử dụng assertions phù hợp (ASSERT vs EXPECT)
5. **Test data**: Sử dụng test data riêng, không phụ thuộc vào production data
6. **Mocking**: Mock external dependencies khi cần

## CI/CD Integration

Tests được tích hợp vào CI/CD pipeline để chạy tự động sau mỗi commit hoặc pull request.

```yaml
# Example GitHub Actions
- name: Build and Test
  run: |
    mkdir build && cd build
    cmake .. -DBUILD_TESTS=ON
    make -j$(nproc)
    ./bin/edgeos-api_tests
```

## Troubleshooting

### Tests không compile
- Kiểm tra Google Test đã được download
- Kiểm tra Drogon đã được build
- Kiểm tra dependencies (jsoncpp, OpenCV, etc.)
- Đảm bảo đã chạy `cmake .. -DBUILD_TESTS=ON`

### Tests fail
- Kiểm tra log output để xem test nào fail
- Đảm bảo dependencies được setup đúng trong SetUp()
- Xem chi tiết lỗi trong test output
- Kiểm tra test data và environment

### Performance issues
- Chạy tests với verbose output để xem test nào chậm
- Tối ưu hóa tests nếu cần
- Sử dụng test fixtures để tránh setup/teardown lặp lại

