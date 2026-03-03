# Tests Directory - Edge AI API

Thư mục này chứa tất cả các tests cho Edge AI API, được tổ chức thành 2 phần chính:

## Cấu Trúc

```
tests/
├── manual/          # Tài liệu và hướng dẫn test thủ công
│   ├── ONVIF/       # Manual tests cho tính năng ONVIF
│   ├── Recognition/ # Manual tests cho tính năng Recognition
│   ├── Instance_Management/
│   ├── Core_API/
│   ├── Solutions/
│   ├── Groups/
│   ├── Nodes/
│   ├── Analytics/
│   └── Config/
│
├── unit/            # Unit tests tự động (Google Test)
│   ├── ONVIF/       # Unit tests cho tính năng ONVIF
│   ├── Recognition/ # Unit tests cho tính năng Recognition
│   ├── Instance_Management/
│   ├── Core_API/
│   ├── Solutions/
│   ├── Groups/
│   ├── Nodes/
│   ├── Analytics/
│   ├── Config/
│   └── test_main.cpp
│
├── CMakeLists.txt   # Cấu hình build cho unit tests
└── README.md        # File này
```

## Manual Tests

Thư mục `manual/` chứa các tài liệu hướng dẫn test thủ công cho từng tính năng lớn. Mỗi tính năng có thư mục riêng với các file markdown mô tả cách test.

Xem chi tiết tại: [manual/README.md](manual/README.md)

## Unit Tests

Thư mục `unit/` chứa các unit tests tự động sử dụng Google Test framework. Tests được tổ chức theo từng tính năng lớn để dễ quản lý và maintain.

Xem chi tiết tại: [unit/README.md](unit/README.md)

## Build và Chạy Tests

### Build tests

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
```

**Lưu ý:** Test executable sẽ được tạo tại `build/bin/edgeos-api_tests`

### Chạy tests

**Cách 1: Sử dụng script (khuyến nghị)**
```bash
# Từ project root
./scripts/run_tests.sh

# Hoặc chỉ định build directory
./scripts/run_tests.sh build
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

## Tổng Quan Tests

**Tổng số unit tests:** 15+ tests từ nhiều test suites

### Core API Tests
- HealthHandler: 3 tests
- VersionHandler: 4 tests
- SwaggerHandler: 6 tests
- WatchdogHandler: 1 test
- EndpointsHandler: 1 test
- ConfigHandler: tests
- SystemInfoHandler: tests
- MetricsHandler: tests

### Instance Management Tests
- InstanceStorage: tests
- InstanceUpdate: tests
- InstanceStatusSummary: tests
- InstanceGetConfig: tests
- InstanceConfigureStream: tests
- CreateInstanceHandler: tests

### Recognition Tests
- RecognitionHandler: tests

### Solutions Tests
- SolutionHandler: tests

### Groups Tests
- GroupHandler: tests

### Nodes Tests
- NodeHandler: tests
- NodePoolHandler: tests

### Analytics Tests
- LinesHandler: tests
- JamsHandler: tests
- StopsHandler: tests
- BA Jam Detection: tests
- BA Jam Pipeline: tests
- SecuRT Handler: tests
- Area Handler: tests

## Best Practices

1. **Mỗi API handler có file test riêng** - Dễ maintain và tìm kiếm
2. **Tests độc lập** - Mỗi test không phụ thuộc vào test khác
3. **Cleanup trong TearDown** - Đảm bảo không có side effects
4. **Test cả success và failure cases** - Đảm bảo error handling đúng
5. **Tổ chức theo tính năng** - Dễ tìm và quản lý tests

## Thêm Tests Mới

Khi thêm API handler mới:

1. Xác định tính năng lớn mà handler thuộc về
2. Tạo file `test_<handler_name>.cpp` trong thư mục `unit/<Feature>/`
3. Thêm file vào `CMakeLists.txt` trong `TEST_SOURCES` với đường dẫn đầy đủ
4. Viết tests cho:
   - Valid requests
   - Invalid requests
   - Edge cases
   - Error handling

## CI/CD Integration

Tests có thể được tích hợp vào CI/CD pipeline:

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
- Kiểm tra Google Test đã được download (sẽ tự động qua FetchContent)
- Kiểm tra Drogon đã được build
- Kiểm tra jsoncpp dependencies
- Đảm bảo đã chạy `cmake .. -DBUILD_TESTS=ON` trước khi make

### Tests fail
- Kiểm tra log output để xem test nào fail
- Đảm bảo các dependencies được setup đúng trong SetUp()
- Xem chi tiết lỗi trong test output

### Không tìm thấy test executable
- Test executable nằm tại `build/bin/edgeos-api_tests`
- Sử dụng script `./scripts/run_tests.sh` để tự động tìm đúng vị trí
- Hoặc chạy trực tiếp: `./build/bin/edgeos-api_tests`
