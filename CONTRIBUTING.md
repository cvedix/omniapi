# Contributing to OmniAPI

Cảm ơn bạn đã quan tâm đến việc đóng góp cho OmniAPI! Hướng dẫn này sẽ giúp bạn
bắt đầu.

---

## 📋 Mục Lục

1. [Quy Tắc Ứng Xử](#quy-tắc-ứng-xử)
2. [Bắt Đầu Nhanh](#bắt-đầu-nhanh)
3. [Git Workflow](#git-workflow)
4. [Coding Standards](#coding-standards)
5. [Commit Messages](#commit-messages)
6. [Pull Request Process](#pull-request-process)
7. [Tạo API Handler Mới](#tạo-api-handler-mới)
8. [Viết Tests](#viết-tests)
9. [Cập Nhật Documentation](#cập-nhật-documentation)
10. [Xử Lý Hot Swap](#xử-lý-hot-swap)
11. [Reporting Issues](#reporting-issues)

---

## Quy Tắc Ứng Xử

- Tôn trọng và lịch sự với tất cả contributors
- Sử dụng language chuyên nghiệp, không phân biệt đối xử
- Tập trung vào nội dung kỹ thuật, không phải cá nhân
- Ưu tiên constructive feedback và giải pháp thay vì phê phán

---

## Bắt Đầu Nhanh

### 1. Fork và Clone

```bash
# Fork repo trên GitHub
git clone https://github.com/<your-username>/omniapi.git
cd omniapi
git remote add upstream https://github.com/cvedix/omniapi.git
```

### 2. Setup Môi Trường

```bash
# Development setup (khuyến nghị)
./scripts/dev_setup.sh

# Build project
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
```

### 3. Tạo Branch cho Feature

```bash
# Luôn tạo branch từ main
git checkout main
git pull upstream main

# Tạo branch mới cho feature/fix
git checkout -b feat/my-new-feature
# hoặc
git checkout -b fix/issue-description
```

### 4. Development Cycle

```bash
# Chạy server trong development
cd ..
./scripts/load_env.sh

# Chạy tests
cd build
./bin/omniapi_tests

# Hoặc sử dụng script
./scripts/run_tests.sh
```

---

## Git Workflow

### Branch Naming Conventions

```
feat/<description>        # Feature mới
fix/<description>         # Bug fix
refactor/<description>     # Refactoring
docs/<description>         # Documentation
test/<description>        # Tests
chore/<description>        # Maintenance, dependencies
hotfix/<description>       # Emergency fix
```

**Ví dụ:**
```bash
feat/add-securt-area-api
fix/hot-swap-stream-loss
docs/update-api-reference
test/instance-management
chore/upgrade-drogon
```

### Commit Frequently

```bash
# Commit thường xuyên với message rõ ràng
git add src/api/my_handler.cpp
git commit -m "feat: add MyHandler API

- Implement GET /v1/my/feature endpoint
- Implement POST /v1/my/feature endpoint
- Add metrics interceptor tracking
- Add CORS headers"
```

### Sync với Upstream

```bash
# Cập nhật branch từ upstream
git fetch upstream
git rebase upstream/main

# Hoặc merge
git merge upstream/main
```

---

## Coding Standards

### C++ Guidelines

#### Naming Conventions

| Loại | Convention | Ví dụ |
|------|-----------|-------|
| Class/Struct | PascalCase | `MyHandler`, `InstanceRegistry` |
| Method/Function | PascalCase | `getFeature`, `createInstance` |
| Variable | snake_case | `instance_id`, `config_json` |
| Member variable | `m_` prefix + snake_case | `m_instance_id`, `m_config` |
| Constant | kPrefix + PascalCase | `kMaxRetries`, `kDefaultPort` |
| Enum value | PascalCase | `Status::Running`, `Error::NotFound` |
| File | snake_case | `my_handler.cpp`, `instance_registry.cpp` |
| Endpoint | kebab-case | `/v1/my/feature`, `/v1/core/instance` |

#### Code Organization

```cpp
// File: include/api/my_handler.h
#pragma once

#include <drogon/HttpController.h>
#include <json/json.h>

namespace omniapi {

/**
 * @brief My Feature Handler
 *
 * Handles endpoints for my feature.
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
     * @param req HTTP request
     * @param callback Response callback
     */
    void getFeature(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

    /**
     * @brief Create new feature
     * @param req HTTP request
     * @param callback Response callback
     */
    void createFeature(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);
};

} // namespace omniapi
```

```cpp
// File: src/api/my_handler.cpp
#include "api/my_handler.h"
#include "core/metrics_interceptor.h"
#include <plog/Log.h>

namespace omniapi {

void MyHandler::getFeature(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &&callback)
{
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

        // Business logic here

        Json::Value response;
        response["id"] = id;
        response["data"] = "feature data";

        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k200OK);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

    } catch (const std::exception& e) {
        PLOG_ERROR << "Exception in getFeature: " << e.what();
        Json::Value error;
        error["error"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    }
}

} // namespace omniapi
```

#### Best Practices

1. **Luôn sử dụng `MetricsInterceptor`** ở đầu và cuối mỗi handler
2. **Luôn thêm CORS headers** cho mọi response
3. **Luôn validate input** trước khi xử lý
4. **Sử dụng structured logging** với plog
5. **Xử lý exception** cho mọi handler
6. **Trả về HTTP status code chính xác**:
   - `200 OK` — Success
   - `201 Created` — Resource created
   - `400 Bad Request` — Invalid input
   - `404 Not Found` — Resource not found
   - `500 Internal Server Error` — Server error

#### Include Order

```cpp
// 1. Corresponding header (nếu là .cpp)
#include "api/my_handler.h"

// 2. Project headers
#include "core/metrics_interceptor.h"
#include "instances/instance_registry.h"

// 3. Drogon headers
#include <drogon/HttpController.h>
#include <drogon/HttpResponse.h>

// 4. Third-party headers
#include <json/json.h>
#include <plog/Log.h>

// 5. Standard library
#include <string>
#include <vector>
#include <memory>
```

### CMakeLists.txt

```cmake
# Thêm vào phần SOURCES trong CMakeLists.txt
set(SOURCES
    # ... existing files ...
    src/api/my_handler.cpp
)

# Thêm vào INCLUDE_DIRS nếu cần
set(INCLUDE_DIRS
    # ...
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

---

## Commit Messages

### Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Types

| Type | Mô tả |
|------|-------|
| `feat` | Feature mới |
| `fix` | Bug fix |
| `docs` | Chỉ thay đổi documentation |
| `style` | Format code, không thay đổi logic |
| `refactor` | Refactoring code |
| `test` | Thêm hoặc sửa tests |
| `chore` | Maintenance, dependencies, build |
| `perf` | Cải thiện performance |
| `ci` | CI/CD changes |
| `revert` | Revert commit trước |

### Scope (tùy chọn)

Phạm vi thay đổi: `api`, `core`, `instance`, `solution`, `worker`, `pipeline`, `ai-runtime`, `onvif`, `docs`, `tests`

### Examples

```bash
# Feature
git commit -m "feat(instance): add batch start/stop API

Add POST /v1/core/instance/batch/start and batch/stop endpoints
to allow starting multiple instances in one request.

Closes #123"

# Bug fix
git commit -m "fix(hotswap): prevent stream loss during pipeline swap

When updating CrossingLines with RTMP output, the hot swap was not
preserving the RTMP connection. This fix ensures tempConfig preserves
the output settings from the original config.

Fixes #456"

# Documentation
git commit -m "docs(api): update OpenAPI spec for SecuRT endpoints

Add missing SecuRT line and area endpoint definitions.
Update example requests and responses."

# Refactor
git commit -m "refactor(worker): extract IPC message handling

Move IPC message parsing logic to separate handler classes.
Reduce complexity in worker_handler_handlers.cpp by 30%."

# Test
git commit -m "test(instance): add unit tests for hot reload

Add tests for:
- Line-only update with RTMP
- Full config update
- Hot swap fallback on error"
```

### Rules

1. **Subject** không viết hoa đầu
2. **Subject** không kết thúc bằng dấu chấm
3. **Subject** tối đa 72 ký tự
4. **Body** giải thích **WHAT** và **WHY**, không phải HOW
5. **Footer** tham chiếu issues: `Closes #123`, `Fixes #456`

---

## Pull Request Process

### Trước Khi Tạo PR

1. **Chạy tất cả tests**:
   ```bash
   cd build
   ./bin/omniapi_tests
   ```

2. **Chạy pre-commit hooks**:
   ```bash
   pre-commit run --all-files
   ```

3. **Build thành công**:
   ```bash
   make -j$(nproc)
   ```

4. **Cập nhật CHANGELOG.md** (nếu có thay đổi đáng chú ý)

5. **Cập nhật documentation** (nếu thêm API mới)

### Tạo Pull Request

1. **Push branch** lên fork:
   ```bash
   git push origin feat/my-new-feature
   ```

2. **Tạo PR** trên GitHub với:
   - **Title**: Rõ ràng, mô tả ngắn gọn thay đổi
   - **Description**: Giải thích WHAT, WHY, và cách test
   - **Screenshots** (nếu có UI changes)
   - **Link đến issues** liên quan

3. **PR Template** (sử dụng template nếu có trong repo)

### PR Template

```markdown
## Mô Tả
<!-- Mô tả ngắn gọn thay đổi -->

## Loại Thay Đổi
- [ ] Feature mới
- [ ] Bug fix
- [ ] Refactoring
- [ ] Documentation
- [ ] Tests

## Test Plan
<!-- Mô tả cách test thay đổi này -->
- [ ] Unit tests passed
- [ ] Manual test completed
- [ ] API endpoint verified

## Screenshots (nếu có)
<!-- Ảnh trước/sau hoặc GIF -->

## Checklist
- [ ] Code tuân thủ coding standards
- [ ] Tests được thêm/sửa
- [ ] Documentation được cập nhật
- [ ] CHANGELOG được cập nhật
```

### Sau Khi PR Được Merge

```bash
# Xóa branch đã merge
git checkout main
git pull upstream main
git branch -d feat/my-new-feature
git push origin --delete feat/my-new-feature
```

---

## Tạo API Handler Mới

### Checklist

- [ ] Tạo `include/api/xxx_handler.h`
- [ ] Tạo `src/api/xxx_handler.cpp`
- [ ] Đăng ký trong `main.cpp`
- [ ] Thêm vào `CMakeLists.txt`
- [ ] Thêm OpenAPI spec trong `api-specs/openapi/en/paths/`
- [ ] Merge OpenAPI: `python3 scripts/merge_openapi.py api-specs/openapi/en`
- [ ] Đồng bộ sang `api-specs/openapi/vi/paths/`
- [ ] Viết unit tests trong `tests/unit/`
- [ ] Viết manual test guide trong `tests/manual/`
- [ ] Cập nhật `CHANGELOG.md`

### Chi Tiết

Xem [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — section "Tạo API Handler Mới".

---

## Viết Tests

### Unit Tests

**Location**: `tests/unit/<Feature>/test_xxx_handler.cpp`

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

    std::unique_ptr<MyHandler> handler_;
};

TEST_F(MyHandlerTest, GetFeatureSuccess) {
    // Test implementation
}

TEST_F(MyHandlerTest, GetFeatureMissingId) {
    // Test implementation
}
```

**Chạy tests**:
```bash
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
./bin/omniapi_tests
```

### Manual Test Guides

**Location**: `tests/manual/<Feature>/FEATURE_TEST_GUIDE.md`

```markdown
# My Feature Manual Test Guide

## Prerequisites
- API server đang chạy tại http://localhost:8080
- curl hoặc Postman

## Test Cases

### 1. Create Feature - Success
```bash
curl -X POST http://localhost:8080/v1/my/feature \
  -H "Content-Type: application/json" \
  -d '{"name": "test"}'
```
**Expected**: Status 201, JSON response
```

Xem thêm: [tests/README.md](tests/README.md)

### Checklist

- [ ] Thêm unit tests cho handler mới
- [ ] Test success cases
- [ ] Test error cases (400, 404, 500)
- [ ] Test edge cases
- [ ] Tạo manual test guide
- [ ] Chạy tất cả tests trước khi commit

---

## Cập Nhật Documentation

### OpenAPI Spec

**Luôn chỉnh sửa file riêng, không sửa file chính**:

```bash
# 1. Tạo/chỉnh sửa file endpoint
vim api-specs/openapi/en/paths/my_feature/core_my_feature.yaml

# 2. Merge vào file chính
python3 scripts/merge_openapi.py api-specs/openapi/en

# 3. Đồng bộ sang tiếng Việt
vim api-specs/openapi/vi/paths/my_feature/core_my_feature.yaml
python3 scripts/merge_openapi.py api-specs/openapi/vi

# 4. Verify trên Swagger
curl http://localhost:8080/v1/openapi/en/openapi.yaml | head -50
```

### Chi Tiết

Xem [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — section "API Documentation (Swagger & Scalar)".

### Checklist Documentation

- [ ] Cập nhật OpenAPI spec (en)
- [ ] Cập nhật OpenAPI spec (vi)
- [ ] Merge OpenAPI files
- [ ] Thêm examples vào spec
- [ ] Cập nhật manual test guide
- [ ] Cập nhật CHANGELOG.md

---

## Xử Lý Hot Swap

### Khi Nào Cần Cẩn Thận

Hot swap là tính năng phức tạp, ảnh hưởng trực tiếp đến RTMP stream.
**Cần review kỹ** khi thay đổi:

- `src/worker/worker_handler_handlers.cpp`
- `src/worker/worker_handler_hotswap.cpp`
- `src/core/pipeline_builder*.cpp`
- `src/core/pipeline_builder_behavior_analysis_nodes.cpp`
- `src/worker/worker_supervisor.cpp`

### Checklist Hot Swap

- [ ] Test line-only update với RTMP output
- [ ] Test full config update với RTMP output
- [ ] Verify stream không bị ngắt sau PATCH
- [ ] Kiểm tra log worker: `logs/worker_<instance_id>.log`
- [ ] Xác nhận dòng "Zero-downtime pipeline swap" trong log
- [ ] Test fallback khi hot swap thất bại

### Chi Tiết

Xem [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — section "Kiến Trúc Hot Swap & Zero-Downtime"
và [docs/ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md](docs/ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md).

---

## Reporting Issues

### Trước Khi Tạo Issue

1. **Search** xem issue đã tồn tại chưa
2. **Update** repo về phiên bản mới nhất
3. **Reproduce** lỗi để xác nhận

### Issue Template

```markdown
## Mô Tả
<!-- Mô tả ngắn gọn vấn đề -->

## Môi Trường
- OS: Ubuntu 22.04
- Version: 2026.0.1.60
- Hardware: NVIDIA Jetson AGX Orin

## Cách Tái Hiện
1. Go to '...'
2. Click on '...'
3. Scroll down to '...'
4. See error

## Expected Behavior
<!-- Mô tả behavior mong đợi -->

## Actual Behavior
<!-- Mô tả behavior thực tế -->

## Logs
```
<!-- Paste relevant logs here -->
```

## Additional Context
<!-- Ảnh chụp màn hình, ghi chú thêm -->
```

### Labels

| Label | Mô tả |
|-------|-------|
| `bug` | Bug hoặc lỗi |
| `feature` | Feature request |
| `enhancement` | Cải thiện tính năng hiện có |
| `documentation` | Documentation issue |
| `hot-swap` | Liên quan đến hot swap |
| `worker` | Liên quan đến worker subprocess |
| `api` | Liên quan đến API endpoints |

---

## Resources

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — Hướng dẫn phát triển chi tiết
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Kiến trúc hệ thống
- [docs/API.md](docs/API.md) — API reference
- [tests/README.md](tests/README.md) — Hướng dẫn tests

---

Cảm ơn bạn đã đóng góp! 🚀
