# Hướng Dẫn Phát Triển - edgeos-api

Tài liệu này bao gồm setup môi trường, hướng dẫn phát triển API, tổ chức tests, và các tính năng như Swagger/Scalar documentation.

## 📋 Mục Lục

1. [Setup Môi Trường](#setup-môi-trường)
2. [Cấu Trúc Project](#cấu-trúc-project)
3. [Build Project](#build-project)
4. [Tạo API Handler Mới](#tạo-api-handler-mới)
5. [Tổ Chức Tests](#tổ-chức-tests)
6. [API Documentation (Swagger & Scalar)](#api-documentation-swagger--scalar)
7. [Pre-commit Hooks](#pre-commit-hooks)
8. [Best Practices](#best-practices)
9. [Hot Swap (Zero-Downtime Update)](#hot-swap-zero-downtime-update)
10. [Troubleshooting](#troubleshooting)

---

## 🚀 Setup Môi Trường

### Setup Tự Động (Khuyến Nghị)

```bash
# Clone project
git clone https://github.com/cvedix/edgeos-api.git
cd edgeos-api

# Development setup (cài dependencies, tạo thư mục, setup environment)
./scripts/dev_setup.sh

# Load environment variables và chạy server
./scripts/load_env.sh

# Production setup (nếu cần)
sudo ./scripts/prod_setup.sh
```

Xem chi tiết: [docs/SCRIPTS.md](SCRIPTS.md)

### Yêu Cầu Hệ Thống

- **OS**: Ubuntu 20.04+ / Debian 10+
- **CMake**: 3.14+
- **C++ Standard**: C++17
- **Dependencies**: build-essential, libssl-dev, zlib1g-dev, libjsoncpp-dev, uuid-dev

### Cài Dependencies Thủ Công

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git pkg-config \
    libssl-dev zlib1g-dev libjsoncpp-dev uuid-dev
```

### Environment Variables

Tạo file `.env` hoặc export các biến môi trường:

```bash
# API Configuration
export API_HOST=0.0.0.0
export API_PORT=8080

# CVEDIX SDK Path
export CVEDIX_SDK_PATH=/opt/cvedix

# Logging
export LOG_LEVEL=INFO
```

Xem chi tiết: [docs/ENVIRONMENT_VARIABLES.md](ENVIRONMENT_VARIABLES.md)

---

## 📁 Cấu Trúc Project

### Tổng Quan

```
api/
├── include/              # Header files
│   ├── api/             # API handlers headers
│   ├── core/            # Core functionality
│   ├── instances/       # Instance management
│   ├── solutions/      # Solution management
│   ├── groups/          # Group management
│   ├── nodes/           # Node management
│   ├── models/          # Model management
│   ├── videos/          # Video management
│   ├── config/          # Configuration
│   └── worker/          # Worker threads
│
├── src/                 # Source files (cùng cấu trúc với include/)
│   ├── api/            # API handlers implementation
│   ├── core/           # Core implementation
│   ├── main.cpp        # Entry point
│   └── ...
│
├── tests/               # Tests directory
│   ├── unit/           # Unit tests (Google Test)
│   │   ├── Core_API/
│   │   ├── Instance_Management/
│   │   ├── Recognition/
│   │   ├── Solutions/
│   │   ├── Groups/
│   │   ├── Nodes/
│   │   ├── Analytics/
│   │   └── test_main.cpp
│   │
│   ├── manual/         # Manual test guides
│   │   ├── ONVIF/
│   │   ├── Recognition/
│   │   ├── Instance_Management/
│   │   └── ...
│   │
│   └── CMakeLists.txt  # Test build configuration
│
├── api-specs/          # API documentation
│   ├── openapi/        # OpenAPI specifications
│   │   ├── en/        # English version
│   │   │   ├── openapi.yaml  # File chính (merged)
│   │   │   ├── paths/       # Các file endpoint (mỗi endpoint một file)
│   │   │   └── components/  # Component schemas
│   │   └── vi/        # Vietnamese version (cấu trúc tương tự)
│   ├── scalar/         # Scalar documentation files
│   └── postman/        # Postman collections
│
├── examples/           # Example configurations
│   ├── instances/      # Instance examples
│   └── solutions/     # Solution examples
│
├── scripts/            # Helper scripts
│   ├── dev_setup.sh   # Development setup
│   ├── load_env.sh    # Load environment
│   ├── run_tests.sh   # Run tests
│   └── ...
│
├── CMakeLists.txt      # Main build configuration
├── main.cpp           # Application entry point
└── README.md          # Project README
```

### Quy Tắc Tổ Chức Code

1. **Header Files**: Tất cả headers trong `include/`, giữ nguyên cấu trúc thư mục
2. **Source Files**: Tất cả sources trong `src/`, giữ nguyên cấu trúc thư mục
3. **API Handlers**: Mỗi handler có 2 files:
   - `include/api/xxx_handler.h` - Header
   - `src/api/xxx_handler.cpp` - Implementation
4. **Tests**: Tổ chức theo tính năng trong `tests/unit/<Feature>/`

---

## 🏗️ Build Project

### Build Cơ Bản

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build với Tests

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
```

### Build Options

```bash
# Tự động download dependencies nếu thiếu
cmake .. -DAUTO_DOWNLOAD_DEPENDENCIES=ON

# Build với tests
cmake .. -DBUILD_TESTS=ON

# Kết hợp cả hai
cmake .. -DAUTO_DOWNLOAD_DEPENDENCIES=ON -DBUILD_TESTS=ON
```

### Chạy Server

```bash
# Development (sử dụng script - khuyến nghị)
./scripts/load_env.sh

# Hoặc chạy trực tiếp
cd build
./bin/edgeos-api

# Với logging options
./bin/edgeos-api --log-api --log-instance --log-sdk-output
```

---

## 🆕 Tạo API Handler Mới

### Bước 1: Tạo Header File

Tạo `include/api/my_handler.h`:

```cpp
#pragma once

#include <drogon/HttpController.h>
#include <json/json.h>

using namespace drogon;

/**
 * @brief My Feature Handler
 * 
 * Endpoints:
 * - GET /v1/my/feature - Get feature data
 * - POST /v1/my/feature - Create feature
 */
class MyHandler : public drogon::HttpController<MyHandler> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(MyHandler::getFeature, "/v1/my/feature", Get);
        ADD_METHOD_TO(MyHandler::createFeature, "/v1/my/feature", Post);
    METHOD_LIST_END

    /**
     * @brief Get feature data
     */
    void getFeature(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

    /**
     * @brief Create new feature
     */
    void createFeature(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);
};
```

### Bước 2: Implement Handler

Tạo `src/api/my_handler.cpp`:

```cpp
#include "api/my_handler.h"
#include "core/metrics_interceptor.h"  // Cho metrics tracking

void MyHandler::getFeature(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Set handler start time for metrics
    MetricsInterceptor::setHandlerStartTime(req);
    
    try {
        auto id = req->getParameter("id");
        if (id.empty()) {
            Json::Value error;
            error["error"] = "Missing parameter: id";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
            return;
        }

        Json::Value response;
        response["id"] = id;
        response["data"] = "feature data";

        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k200OK);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

    } catch (const std::exception& e) {
        Json::Value error;
        error["error"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    }
}

void MyHandler::createFeature(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback)
{
    MetricsInterceptor::setHandlerStartTime(req);
    
    try {
        auto json = req->getJsonObject();
        if (!json || !json->isMember("name")) {
            Json::Value error;
            error["error"] = "Missing required field: name";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
            return;
        }

        // Business logic here
        Json::Value response;
        response["id"] = "new-id";
        response["name"] = (*json)["name"].asString();
        response["status"] = "created";

        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k201Created);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

    } catch (const std::exception& e) {
        Json::Value error;
        error["error"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    }
}
```

### Bước 3: Đăng Ký trong main.cpp

Thêm vào `src/main.cpp`:

```cpp
#include "api/my_handler.h"

// Trong hàm main(), sau khi app được khởi tạo
static MyHandler myHandler;
```

### Bước 4: Thêm vào CMakeLists.txt

Thêm vào `CMakeLists.txt` trong phần `SOURCES`:

```cmake
set(SOURCES
    # ... existing files ...
    src/api/my_handler.cpp
)
```

### Bước 5: Cập Nhật OpenAPI Specification

OpenAPI specification được tổ chức với **mỗi endpoint một file riêng** để dễ quản lý.

#### Cấu trúc OpenAPI

```
api-specs/openapi/
├── en/                          # English version
│   ├── openapi.yaml            # File chính (được merge tự động)
│   ├── paths/                  # Các file endpoint được tổ chức theo tag
│   │   ├── core/              # Core API endpoints
│   │   │   ├── core_health.yaml
│   │   │   └── ...
│   │   ├── ai/                # AI API endpoints
│   │   └── ...                # Các tag khác
│   └── components/
│       └── schemas.yaml       # Component schemas
└── vi/                          # Vietnamese version (cấu trúc tương tự)
```

#### Thêm endpoint mới

1. **Tạo file endpoint mới** trong thư mục tag tương ứng:

```bash
# Ví dụ: Thêm endpoint /v1/my/feature với tag "My Feature"
# Tạo file: api-specs/openapi/en/paths/my_feature/core_my_feature.yaml
```

2. **Nội dung file endpoint** (`api-specs/openapi/en/paths/my_feature/core_my_feature.yaml`):

```yaml
# API endpoint: /v1/my/feature
# Tag: My Feature

/v1/my/feature:
  get:
    summary: Get feature data
    description: Retrieve feature information by ID
    operationId: getFeature
    tags:
      - My Feature
    parameters:
      - name: id
        in: query
        required: true
        schema:
          type: string
        description: Feature ID
    responses:
      '200':
        description: Success
        content:
          application/json:
            schema:
              type: object
              properties:
                id:
                  type: string
                data:
                  type: string
      '400':
        description: Bad Request
        content:
          application/json:
            schema:
              $ref: '#/components/schemas/ErrorResponse'
  
  post:
    summary: Create new feature
    description: Create a new feature
    operationId: createFeature
    tags:
      - My Feature
    requestBody:
      required: true
      content:
        application/json:
          schema:
            type: object
            required:
              - name
            properties:
              name:
                type: string
    responses:
      '201':
        description: Created
        content:
          application/json:
            schema:
              type: object
              properties:
                id:
                  type: string
                name:
                  type: string
                status:
                  type: string
```

3. **Merge lại file chính**:

```bash
# Merge file endpoint vào openapi.yaml chính
python3 scripts/merge_openapi.py api-specs/openapi/en

# Làm tương tự cho tiếng Việt
python3 scripts/merge_openapi.py api-specs/openapi/vi
```

4. **Kiểm tra** trên Swagger/Scalar để đảm bảo endpoint hiển thị đúng.

**Lưu ý**: 
- Tên file dựa trên path của endpoint (ví dụ: `/v1/my/feature` → `core_my_feature.yaml`)
- File được đặt trong thư mục tag tương ứng (ví dụ: `paths/my_feature/`)
- Luôn merge lại file chính sau khi chỉnh sửa endpoint

---

## 🧪 Tổ Chức Tests

Project sử dụng 2 loại tests: **Auto Tests** (Unit tests) và **Manual Tests** (Test guides).

### Cấu Trúc Tests

```
tests/
├── unit/                    # Unit tests tự động (Google Test)
│   ├── Core_API/           # Tests cho Core API
│   ├── Instance_Management/ # Tests cho Instance Management
│   ├── Recognition/        # Tests cho Recognition
│   ├── Solutions/          # Tests cho Solutions
│   ├── Groups/             # Tests cho Groups
│   ├── Nodes/              # Tests cho Nodes
│   ├── Analytics/          # Tests cho Analytics
│   └── test_main.cpp       # Entry point
│
├── manual/                  # Manual test guides
│   ├── ONVIF/              # ONVIF test guides
│   ├── Recognition/        # Recognition test guides
│   └── ...                 # Các tính năng khác
│
└── CMakeLists.txt          # Test build configuration
```

### Unit Tests

#### Tổ Chức

- Mỗi tính năng có thư mục riêng trong `tests/unit/<Feature>/`
- Mỗi handler có file test riêng: `test_<handler_name>.cpp`
- Entry point: `tests/unit/test_main.cpp`

#### Viết Unit Test Mới

Tạo `tests/unit/Core_API/test_my_handler.cpp`:

```cpp
#include <gtest/gtest.h>
#include "api/my_handler.h"
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <thread>
#include <chrono>

using namespace drogon;

class MyHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        handler_ = std::make_unique<MyHandler>();
    }
    
    void TearDown() override {
        // Cleanup if needed
    }
    
    std::unique_ptr<MyHandler> handler_;
};

TEST_F(MyHandlerTest, GetFeatureReturnsValidJson) {
    bool callbackCalled = false;
    HttpResponsePtr response;

    auto req = HttpRequest::newHttpRequest();
    req->setPath("/v1/my/feature");
    req->setMethod(Get);
    req->setParameter("id", "123");

    handler_->getFeature(req, [&](const HttpResponsePtr &resp) {
        callbackCalled = true;
        response = resp;
    });

    // Wait for async callback
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackCalled);
    EXPECT_EQ(response->statusCode(), k200OK);

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr);
    EXPECT_EQ((*json)["id"].asString(), "123");
}

TEST_F(MyHandlerTest, GetFeatureMissingIdReturns400) {
    bool callbackCalled = false;
    HttpResponsePtr response;

    auto req = HttpRequest::newHttpRequest();
    req->setPath("/v1/my/feature");
    req->setMethod(Get);
    // Không set parameter "id"

    handler_->getFeature(req, [&](const HttpResponsePtr &resp) {
        callbackCalled = true;
        response = resp;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackCalled);
    EXPECT_EQ(response->statusCode(), k400BadRequest);
}
```

#### Thêm Test vào CMakeLists.txt

Thêm vào `tests/CMakeLists.txt` trong phần `TEST_SOURCES`:

```cmake
set(TEST_SOURCES
    # ... existing tests ...
    unit/Core_API/test_my_handler.cpp
)
```

#### Chạy Tests

```bash
# Build tests
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)

# Chạy tất cả tests
./bin/edgeos_api_tests

# Chạy tests cụ thể
./bin/edgeos_api_tests --gtest_filter=MyHandlerTest.*

# Sử dụng script (khuyến nghị)
./scripts/run_tests.sh

# Sử dụng CTest
cd build
ctest --output-on-failure
```

### Manual Tests

Manual tests là các tài liệu hướng dẫn test thủ công cho từng tính năng.

#### Tổ Chức

- Mỗi tính năng có thư mục riêng trong `tests/manual/<Feature>/`
- Mỗi file markdown mô tả cách test tính năng đó

#### Ví dụ: Tạo Manual Test Guide

Tạo `tests/manual/Core_API/MY_FEATURE_TEST_GUIDE.md`:

```markdown
# My Feature Manual Test Guide

## Prerequisites
- API server đang chạy tại http://localhost:8080
- curl hoặc Postman để test

## Test Cases

### 1. Get Feature - Success
```bash
curl -X GET "http://localhost:8080/v1/my/feature?id=123"
```

**Expected**: Status 200, JSON response với id và data

### 2. Get Feature - Missing ID
```bash
curl -X GET "http://localhost:8080/v1/my/feature"
```

**Expected**: Status 400, error message
```

Xem thêm: [tests/README.md](../tests/README.md)

---

## 📚 API Documentation (Swagger & Scalar)

Project hỗ trợ 2 loại API documentation: **Swagger UI** và **Scalar API Reference**.

### Swagger UI

Swagger UI cung cấp giao diện web để test và explore API.

#### Truy Cập Swagger UI

```bash
# Khi server đang chạy
http://localhost:8080/swagger          # Tất cả versions
http://localhost:8080/v1/swagger       # API v1
http://localhost:8080/v2/swagger      # API v2
```

#### Endpoints

- `GET /swagger` - Swagger UI (all versions)
- `GET /v1/swagger` - Swagger UI cho v1
- `GET /v2/swagger` - Swagger UI cho v2
- `GET /openapi.yaml` - OpenAPI spec (all versions)
- `GET /v1/openapi.yaml` - OpenAPI spec cho v1
- `GET /v2/openapi.yaml` - OpenAPI spec cho v2
- `GET /v1/openapi/{lang}/openapi.yaml` - OpenAPI spec với ngôn ngữ (en/vi)
- `GET /v1/openapi/{lang}/openapi.json` - OpenAPI spec JSON format

#### Cập Nhật Swagger Documentation

1. **Chỉnh sửa file endpoint** trong `api-specs/openapi/en/paths/<tag>/<endpoint>.yaml`
2. **Merge lại file chính**:
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/en
   ```
3. **Đồng bộ sang tiếng Việt**: Cập nhật file tương ứng trong `api-specs/openapi/vi/paths/<tag>/` và merge:
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/vi
   ```
4. Server tự động load và serve file `openapi.yaml` đã merge

**Lưu ý**: Không chỉnh sửa trực tiếp file `openapi.yaml` chính. Luôn chỉnh sửa file endpoint riêng và merge lại.

### Scalar API Reference

Scalar cung cấp giao diện documentation hiện đại hơn với hỗ trợ đa ngôn ngữ.

#### Truy Cập Scalar Documentation

```bash
# Khi server đang chạy
http://localhost:8080/v1/document      # API v1
http://localhost:8080/v2/document      # API v2

# Với ngôn ngữ
http://localhost:8080/v1/document?lang=en  # English
http://localhost:8080/v1/document?lang=vi  # Tiếng Việt
```

#### Endpoints

- `GET /v1/document` - Scalar documentation cho v1
- `GET /v2/document` - Scalar documentation cho v2
- `GET /v1/scalar/standalone.css` - Scalar CSS file
- `GET /v1/document/examples` - List example files
- `GET /v1/document/examples/{path}` - Get example file content

#### Tính Năng Scalar

- ✅ Hỗ trợ đa ngôn ngữ (English/Tiếng Việt)
- ✅ Giao diện hiện đại và dễ sử dụng
- ✅ Tự động lưu ngôn ngữ đã chọn
- ✅ Deep linking với query parameter `?lang=en` hoặc `?lang=vi`
- ✅ Tích hợp examples từ `examples/instances/`

#### Cấu Trúc Files

```
api-specs/
├── openapi/
│   ├── en/                     # English version
│   │   ├── openapi.yaml        # File chính (được merge tự động)
│   │   ├── paths/              # Các file endpoint (mỗi endpoint một file)
│   │   │   ├── core/          # Core API endpoints
│   │   │   │   ├── core_health.yaml
│   │   │   │   └── ...
│   │   │   ├── ai/            # AI API endpoints
│   │   │   └── ...            # Các tag khác
│   │   └── components/
│   │       └── schemas.yaml   # Component schemas
│   └── vi/                     # Vietnamese version (cấu trúc tương tự)
│       ├── openapi.yaml
│       ├── paths/
│       └── components/
├── scalar/
│   ├── index.html              # Scalar HTML template
│   └── standalone.css         # Scalar CSS (optional, có thể dùng CDN)
└── scripts/                    # Scripts hỗ trợ
    ├── split_openapi.py       # Tách file OpenAPI lớn thành các file nhỏ
    └── merge_openapi.py       # Merge các file endpoint lại thành file chính
```

#### Cập Nhật Scalar Documentation

1. **Chỉnh sửa file endpoint**: Sửa file trong `api-specs/openapi/en/paths/<tag>/<endpoint>.yaml`
2. **Merge lại file chính**:
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/en
   ```
3. **Đồng bộ sang tiếng Việt**: Cập nhật file tương ứng trong `api-specs/openapi/vi/paths/<tag>/` và merge:
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/vi
   ```
4. **Kiểm tra**: Truy cập `/v1/document` và kiểm tra cả hai ngôn ngữ

**Lưu ý**: 
- Mỗi endpoint có file riêng để dễ quản lý
- Luôn merge lại file chính sau khi chỉnh sửa
- Xem thêm hướng dẫn chi tiết: [api-specs/openapi/README.md](../../api-specs/openapi/README.md)

#### Scalar Files Setup

Nếu chưa có Scalar files:

```bash
# Download Scalar CSS (nếu cần offline)
mkdir -p api-specs/scalar
curl -o api-specs/scalar/standalone.css \
  https://cdn.jsdelivr.net/npm/@scalar/api-reference@1.24.0/dist/browser/standalone.css

# Scalar HTML template sẽ được generate tự động bởi ScalarHandler
```

Xem chi tiết: [api-specs/README.md](../api-specs/README.md)

---

## 🔧 Pre-commit Hooks

### Cài đặt

```bash
# Cài pre-commit
pip install pre-commit
# hoặc: pipx install pre-commit
# hoặc: sudo apt install pre-commit

# Cài hooks
pre-commit install
pre-commit install --hook-type pre-push
```

### Hooks được cấu hình

| Hook | Khi nào | Mục đích |
|------|---------|----------|
| `trailing-whitespace` | commit | Xóa whitespace cuối dòng |
| `end-of-file-fixer` | commit | File kết thúc bằng newline |
| `check-yaml` | commit | Validate YAML |
| `check-json` | commit | Validate JSON |
| `check-added-large-files` | commit | Cảnh báo file > 1MB |
| `check-merge-conflict` | commit | Phát hiện conflict markers |
| `mixed-line-ending` | commit | Đảm bảo dùng LF |
| `clang-format` | commit | Format C/C++ |
| `shellcheck` | commit | Lint shell scripts |
| `run-tests` | push | Build và chạy tests |

### Quy trình làm việc

```bash
# Commit - tự động format và validate
git add .
git commit -m "feat: add feature"

# Push - tự động build và test
git push
```

### Lệnh hữu ích

```bash
# Chạy tất cả hooks
pre-commit run --all-files

# Chạy hook cụ thể
pre-commit run clang-format --all-files

# Chạy tests
pre-commit run run-tests --hook-stage pre-push

# Skip hooks (khẩn cấp - không khuyến nghị)
git commit --no-verify
git push --no-verify

# Cập nhật hooks
pre-commit autoupdate
```

---

## ✅ Best Practices

### Code Organization

1. **Mỗi handler một file**: Mỗi API handler có 2 files riêng (header + source)
2. **Tổ chức theo tính năng**: Code được tổ chức theo tính năng lớn
3. **Consistent naming**: Tuân thủ naming conventions
4. **Documentation**: Comment rõ ràng cho public APIs

### Error Handling

```cpp
try {
    // Business logic
} catch (const std::exception& e) {
    Json::Value error;
    error["error"] = e.what();
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}
```

### Input Validation

```cpp
// Validate query parameters
auto id = req->getParameter("id");
if (id.empty()) {
    // Return 400 Bad Request
}

// Validate JSON body
auto json = req->getJsonObject();
if (!json || !json->isMember("required_field")) {
    // Return 400 Bad Request
}
```

### HTTP Status Codes

- `200 OK`: Success
- `201 Created`: Resource created successfully
- `400 Bad Request`: Invalid input
- `404 Not Found`: Resource not found
- `500 Internal Server Error`: Server error

### Naming Conventions

- **Handlers**: `XxxHandler` (PascalCase)
- **Files**: `xxx_handler.h/cpp` (snake_case)
- **Endpoints**: `/v1/xxx/yyy` (lowercase, kebab-case)
- **Methods**: `getXxx`, `createXxx` (camelCase)
- **Variables**: `variable_name` (snake_case)

### Metrics Tracking

Luôn sử dụng `MetricsInterceptor` để track metrics:

```cpp
// Đầu handler
MetricsInterceptor::setHandlerStartTime(req);

// Cuối handler (trong callback)
MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
```

### CORS Headers

Luôn thêm CORS headers cho responses:

```cpp
resp->addHeader("Access-Control-Allow-Origin", "*");
```

### Testing

1. **Viết tests cho mọi handler mới**
2. **Test cả success và failure cases**
3. **Test edge cases**
4. **Giữ tests độc lập**
5. **Cleanup trong TearDown()**

### Documentation

1. **Cập nhật OpenAPI spec khi thêm endpoint mới**:
   - Tạo file endpoint mới trong `api-specs/openapi/en/paths/<tag>/<endpoint>.yaml`
   - Merge lại file chính: `python3 scripts/merge_openapi.py api-specs/openapi/en`
2. **Đồng bộ cả tiếng Anh và tiếng Việt**:
   - Cập nhật file tương ứng trong `api-specs/openapi/vi/paths/<tag>/`
   - Merge lại: `python3 scripts/merge_openapi.py api-specs/openapi/vi`
3. **Thêm examples vào OpenAPI spec** (trong file endpoint)
4. **Cập nhật manual test guides nếu cần**
5. **Không chỉnh sửa trực tiếp file `openapi.yaml` chính** - luôn chỉnh sửa file endpoint riêng và merge lại

---

## Hot Swap (Zero-Downtime Update)

**Hot swap là cơ chế đặc trưng của hệ thống:** Mọi cập nhật cấu hình instance (line, jam, stop, PATCH) đều **ưu tiên hot swap (zero downtime)**; restart chỉ dùng khi hot swap không khả dụng (ví dụ instance chưa chạy).

Phần này mô tả **tính năng hot swap** (cập nhật cấu hình instance không ngắt luồng RTMP), **cách thức hoạt động**, **cách xử lý khi tính năng bị hỏng** và **cách back lại code** để khôi phục.

### Mô tả

- **Hot swap** cho phép PATCH/PUT cấu hình instance (ví dụ đổi CrossingLines, zone, model) trong khi **luồng phát RTMP ra server không bị ngắt**.
- **Fallback thống nhất:** Khi cập nhật Lines/Jams/Stops (hoặc SecuRT lines) qua API chuyên biệt mà runtime update (IPC/set_lines) thất bại, hệ thống **áp dụng qua hot swap** (`updateInstanceFromConfig` với patch tương ứng) thay vì restart instance. Worker UPDATE_LINES: nếu `set_lines()` thất bại hoặc exception thì worker tự fallback hot swap và vẫn trả về thành công.
- **Điều kiện:** Instance đang chạy ở **subprocess mode** (`EDGE_AI_EXECUTION_MODE=subprocess`), có output RTMP; khi chỉ đổi line hoặc khi cần rebuild pipeline thì worker dùng **atomic swap** (build pipeline mới → đổi con trỏ active → drain pipeline cũ) thay vì stop → build → start (gây mất stream vài giây).
- **Tài liệu thiết kế:** [ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md](ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md). **Tóm tắt kiến trúc (diagram):** [ARCHITECTURE.md](ARCHITECTURE.md) — section "Kiến Trúc Hot Swap & Zero-Downtime".

### Cách thức hoạt động (tóm tắt)

1. **Persistent output leg:** Proxy (RtmpLastFrameFallbackProxyNode) + rtmp_des **không** nằm trong pipeline có thể thay; chúng được giữ nguyên, chỉ **đầu vào** của proxy thay đổi (từ pipeline cũ → pipeline mới qua Frame Router).
2. **Frame Router:** Giữ con trỏ "pipeline đang active". Pipeline đang chạy đẩy frame từ OSD vào router → router đẩy xuống proxy → rtmp_des.
3. **Khi PATCH (line-only hoặc rebuild):** Worker merge config → nếu có RTMP output thì gọi `hotSwapPipeline(config_)`: bật last-frame pump → build pipeline mới (output nối vào cùng Frame Router) → **atomic swap** con trỏ active → dừng source pipeline cũ → drain pipeline cũ → start source pipeline mới → tắt pump.
4. **Line-only + RTMP:** Worker chọn hot swap thay vì `set_lines()` để tránh block/timeout và mất stream; pipeline mới được build với CrossingLines mới từ config.

### Các file / đoạn code then chốt (để biết chỗ cần back lại)

| Vai trò | File | Hàm / vị trí quan trọng |
|--------|------|---------------------------|
| Quyết định path (line-only vs rebuild), gọi hot swap | `src/worker/worker_handler_handlers.cpp` | `handleUpdateInstance`: merge config, `onlyLineParamsChanged`, `frame_router_ && output_leg_` → `hotSwapPipeline(config_)`; nếu không thì `checkIfNeedsRebuild` / `applyConfigToPipeline` rồi `hotSwapPipeline` khi cần rebuild. |
| Thực hiện hot swap (build mới, swap, drain, start mới) | `src/worker/worker_handler_hotswap.cpp` | `hotSwapPipeline`: merge config vào `tempConfig`, giữ RTMP trong tempConfig; `preBuildPipeline(tempConfig)`; `frame_router_->setActivePipeline(newSnapshot)`; `stopSourceNodeForSnapshot`; `drainPipelineSnapshot`; `startSourceNodeForSnapshot`. |
| Build pipeline (AI leg nối vào frame_router_) | `src/core/pipeline_builder.cpp` (và behavior_analysis, broker, …) | Build với `frame_router_.get()` khi có RTMP; output pipeline nối vào FrameRouterSinkNode → router. |
| OSD crossline: tránh ghi đè nhiều line thành 1 line | `src/core/pipeline_builder_behavior_analysis_nodes.cpp` | `createBACrosslineOSDNode`: set line từ CrossingLines trước; fallback CROSSLINE_START/END_X/Y **chỉ** chạy khi `!osdLinesSetFromCrossingLines`. |
| Spawn worker, redirect log worker | `src/worker/worker_supervisor.cpp` | `spawnWorker`: trong nhánh child (pid == 0), redirect stdout/stderr vào `logs/worker_<instance_id>.log` (hoặc `$LOG_DIR/...`). |
| Merge config PATCH, sync AdditionalParams ↔ additionalParams | `src/worker/worker_handler_handlers.cpp` | `handleUpdateInstance`: `mergeJsonInto(config_, newConfig)`; đồng bộ `additionalParams` ↔ `AdditionalParams`; giữ RtmpUrl/output khi PATCH chỉ gửi line. |
| Preserve RTMP trong tempConfig khi hot swap | `src/worker/worker_handler_hotswap.cpp` | Đầu `hotSwapPipeline`: copy `config_` → `tempConfig`, merge `newConfig`; nếu tempConfig mất output RTMP thì gán lại từ `config_["additionalParams"]["output"]` và `config_["RtmpUrl"]`. |

### Cách xử lý khi hot swap không hoạt động

1. **Xác định triệu chứng**
   - PATCH trả 200 nhưng cấu hình không đổi (ví dụ vẫn 1 line thay vì 2 line) → thường do OSD bị ghi đè hoặc config merge thiếu CrossingLines.
   - PATCH xong stream RTMP mất / ngắt vài giây → có thể swap không dùng zero-downtime path (thiếu frame_router_/output_leg_) hoặc tempConfig mất RTMP.
   - PATCH timeout hoặc lỗi 500 → preBuild thất bại hoặc worker crash; xem log worker.

2. **Kiểm tra log worker**
   - File: `logs/worker_<instance_id>.log` hoặc `$LOG_DIR/worker_<instance_id>.log`.
   - Cần thấy: `UPDATE_INSTANCE received`, `Line-only update but RTMP output present -> hot-swap` (nếu line-only), `Zero-downtime pipeline swap`, `Pre-built pipeline with N nodes`, `Zero-downtime swap done`, hoặc lỗi `Pre-build failed` / `Failed to start new pipeline source`.

3. **Kiểm tra nhanh trong code**
   - Worker có nhận config đúng không: merge có giữ `additionalParams.CrossingLines` và `additionalParams.output` (RTMP)?
   - `frame_router_` và `output_leg_` có tồn tại khi instance có RTMP? (nếu không, sẽ đi legacy path stop → build → start.)
   - OSD: sau khi sửa, fallback CROSSLINE_* có chỉ chạy khi chưa set từ CrossingLines không?

### Cách back lại khi tính năng bị hỏng sau thay đổi code

1. **Xác định phạm vi thay đổi**
   - Nếu chỉ sửa đúng các file liên quan hot swap (handlers, hotswap, pipeline_builder, behavior_analysis, worker_supervisor), có thể revert từng file hoặc từng commit.
   - Nếu đã chỉnh nhiều chỗ, ưu tiên revert **theo từng tính năng** (ví dụ chỉ revert phần OSD, hoặc chỉ phần worker log).

2. **Revert theo từng file (git)**
   ```bash
   # Xem lịch sử file để chọn commit cần back
   git log --oneline -- src/worker/worker_handler_handlers.cpp
   git log --oneline -- src/worker/worker_handler_hotswap.cpp
   git log --oneline -- src/core/pipeline_builder_behavior_analysis_nodes.cpp
   git log --oneline -- src/worker/worker_supervisor.cpp

   # Khôi phục một file về phiên bản trước (thay <commit> bằng hash hoặc HEAD~1)
   git checkout <commit> -- src/worker/worker_handler_hotswap.cpp

   # Hoặc khôi phục toàn bộ thay đổi chưa commit
   git checkout -- src/worker/worker_handler_handlers.cpp
   ```

3. **Điểm cần khôi phục thủ công nếu không dùng git**
   - **OSD nhiều line:** Trong `createBACrosslineOSDNode`, đảm bảo biến `osdLinesSetFromCrossingLines` được set `true` khi đã set line từ CrossingLines, và khối fallback CROSSLINE_START/END_X/Y chỉ chạy khi `if (!osdLinesSetFromCrossingLines) { ... }`.
   - **Preserve RTMP khi hot swap:** Trong `hotSwapPipeline`, sau khi `mergeJsonInto(tempConfig, newConfig)` phải kiểm tra và gán lại `tempConfig["additionalParams"]["output"]` và `tempConfig["RtmpUrl"]` từ `config_` nếu PATCH không gửi output (ví dụ chỉ gửi CrossingLines).
   - **Line-only + RTMP → hot swap:** Trong `handleUpdateInstance`, khi `onlyLineParamsChanged == true` và `frame_router_ && output_leg_` thì gọi `hotSwapPipeline(config_)` thay vì chỉ `applyLinesFromParamsToPipeline`.

4. **Sau khi back lại**
   - Build lại: `cd build && make -j$(nproc)`.
   - Chạy instance có RTMP, PATCH CrossingLines (2 line), kiểm tra stream không mất và OSD hiển thị đủ 2 line.
   - Xem log worker trong `logs/worker_<instance_id>.log` để xác nhận dòng "Zero-downtime pipeline swap" và "Zero-downtime swap done".

### Tài liệu tham chiếu

- [ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md](ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md) — thiết kế chi tiết, pseudo-code, threading, memory safety.
- [ARCHITECTURE.md](ARCHITECTURE.md) — section "Kiến Trúc Hot Swap & Zero-Downtime (Chuyển giao công nghệ)" (diagram, bảng khi nào dùng hot swap, thay đổi code chính).

---

## ⚠️ Troubleshooting

### Build Errors

```bash
# Xóa cache và build lại
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Lỗi "Could NOT find OpenSSL"

```bash
sudo apt-get install libssl-dev
```

### Lỗi "Could NOT find jsoncpp"

```bash
# Tự động download (khuyến nghị)
cmake .. -DAUTO_DOWNLOAD_DEPENDENCIES=ON

# Hoặc cài thủ công
sudo apt-get install libjsoncpp-dev
```

### Lỗi CVEDIX SDK symlinks

```bash
sudo ln -sf /opt/cvedix/lib/libtinyexpr.so /usr/lib/libtinyexpr.so
sudo ln -sf /opt/cvedix/lib/libcvedix_instance_sdk.so /usr/lib/libcvedix_instance_sdk.so
```

### Pre-commit hooks không chạy

```bash
pre-commit uninstall
pre-commit install
pre-commit install --hook-type pre-push
```

### Tests fail

```bash
# Xem chi tiết lỗi
cd build
./bin/edgeos_api_tests --gtest_output=xml

# Hoặc với CTest
ctest --output-on-failure -V
```

### Swagger/Scalar không hiển thị

1. Kiểm tra server đang chạy
2. Kiểm tra file OpenAPI spec tồn tại: `api-specs/openapi/en/openapi.yaml`
3. **Nếu đã chỉnh sửa file endpoint, đảm bảo đã merge lại**:
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/en
   ```
4. Kiểm tra log server để xem lỗi
5. Kiểm tra file Scalar HTML: `api-specs/scalar/index.html` (nếu có)

### Port đã được sử dụng

```bash
# Kill process trên port 8080
./scripts/kill_port_8080.sh

# Hoặc thủ công
lsof -ti:8080 | xargs kill -9
```

### Không thoát được bằng Ctrl+C khi develop (SIGABRT / heap corruption)

Khi log xuất hiện `free(): corrupted unsorted chunks` hoặc `[RECOVERY] Received SIGABRT`, libc đã phát hiện heap corruption và gọi `abort()` → **SIGABRT**. Handler mặc định cố “recovery” (dừng instance rồi **tiếp tục chạy**); sau corruption trạng thái process không còn an toàn, dễ **treo** hoặc **Ctrl+C không có tác dụng**.

**Cách thoát ngay khi develop** (không chờ recovery):

```bash
# Thoát ngay khi SIGABRT (không recovery) — khuyến nghị khi develop
EDGE_AI_SIGABRT_IMMEDIATE_EXIT=1 sudo LD_PRELOAD=... EDGE_AI_EXECUTION_MODE=subprocess ./build/bin/edgeos-api
```

Hoặc sau khi process treo: **`kill -9 <pid>`** (hoặc `kill -ABRT` chỉ gửi lại SIGABRT, có thể vẫn vào recovery).

**Gốc lỗi:** cần sửa double-free/use-after-free hoặc race trong pipeline/thread (không chỉ tắt recovery).

### Hot Swap không hoạt động / mất stream sau PATCH

- **Triệu chứng:** PATCH instance (ví dụ đổi CrossingLines) trả 200 nhưng cấu hình không đổi, hoặc stream RTMP mất/ngắt sau khi update.
- **Xử lý nhanh:** Xem log worker `logs/worker_<instance_id>.log` (hoặc `$LOG_DIR/worker_<instance_id>.log`); kiểm tra có dòng `Zero-downtime pipeline swap` và `Zero-downtime swap done` hay không, hoặc lỗi `Pre-build failed` / `Failed to start new pipeline source`.
- **Back lại code / sửa đúng chỗ:** Làm theo section [Hot Swap (Zero-Downtime Update)](#hot-swap-zero-downtime-update) — mục "Các file / đoạn code then chốt" và "Cách back lại khi tính năng bị hỏng sau thay đổi code".

---

## 📚 Tài Liệu Liên Quan

- [Hướng Dẫn Khởi Động](GETTING_STARTED.md)
- [Architecture](ARCHITECTURE.md) — gồm section Kiến Trúc Hot Swap & Zero-Downtime
- [Zero-Downtime Atomic Pipeline Swap Design](ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md)
- [Environment Variables](ENVIRONMENT_VARIABLES.md)
- [Scripts Documentation](SCRIPTS.md)
- [Tests Documentation](../tests/README.md)
- [API Specifications](../api-specs/README.md)
- [Drogon Framework](https://drogon.docsforge.com/)
- [Google Test](https://google.github.io/googletest/)
- [OpenAPI Specification](https://swagger.io/specification/)
- [Scalar API Reference](https://github.com/scalar/scalar)

---

## 🎯 Quick Start cho Developer Mới

1. **Clone và setup**:
   ```bash
   git clone <repo-url>
   cd edgeos-api
   ./scripts/dev_setup.sh
   ```

2. **Build project**:
   ```bash
   mkdir build && cd build
   cmake .. -DBUILD_TESTS=ON
   make -j$(nproc)
   ```

3. **Chạy tests**:
   ```bash
   ./bin/edgeos_api_tests
   ```

4. **Chạy server**:
   ```bash
   cd ..
   ./scripts/load_env.sh
   ```

5. **Xem API documentation**:
   - Swagger: http://localhost:8080/swagger
   - Scalar: http://localhost:8080/v1/document

6. **Tạo handler mới**: Làm theo hướng dẫn ở [Tạo API Handler Mới](#tạo-api-handler-mới)

7. **Viết tests**: Làm theo hướng dẫn ở [Tổ Chức Tests](#tổ-chức-tests)

8. **Cập nhật documentation**: Cập nhật OpenAPI spec và test guides

---

**Chúc bạn phát triển thành công! 🚀**
