# Báo cáo lỗi SDK: ASIO dependency – SSE Broker node

**Mục đích**: Nội dung này dùng để gửi cho Team SDK khi cần report lỗi ASIO liên quan tới SSE broker node.

---

## 1. Tóm tắt

- **Thành phần bị ảnh hưởng**: Node **SSE broker** (`cvedix_sse_broker_node`) – Server-Sent Events broker trong CVEDIX SDK.
- **Triệu chứng**: Khi edgeos-api link với CVEDIX SDK và bật SSE broker (include + tạo node), build bị lỗi do **ASIO dependency issue**. edgeos-api đã tạm thời disable node này để build được.
- **Thời điểm**: Khoảng **03/02/2026** (commit `9d019912` – *Fix build errors: comment ASIO/cereal dependencies temporarily*).

---

## 2. Thông tin môi trường / tích hợp

- **Ứng dụng**: edgeos-api (REST server cho CVEDIX SDK).
- **HTTP stack**: Drogon (Trantor) – dùng **Boost.Asio**.
- **SDK**: CVEDIX Core AI Runtime; header SSE broker:  
  `cvedix/nodes/broker/cvedix_sse_broker_node.h`
- **ASIO trong build edgeos-api**:  
  Include path đã thêm:  
  `/opt/edgeos-sdk/include/cvedix/third_party/asio/include`  
  (trong `CMakeLists.txt` và `tests/CMakeLists.txt`).

---

## 3. Thay đổi workaround trong edgeos-api

| Vị trí | Thay đổi |
|--------|----------|
| `src/core/pipeline_builder_broker_nodes.cpp` | Comment `#include <cvedix/nodes/broker/cvedix_sse_broker_node.h>`. Hàm `createSSEBrokerNode()` throw `std::runtime_error("SSE broker node is temporarily disabled due to CVEDIX SDK ASIO dependency issue")` thay vì tạo node. |
| `src/core/pipeline_builder.cpp` | Với `nodeType == "sse_broker"`: in cảnh báo, return `nullptr` (không tạo node). |

---

## 4. Hướng dẫn từng bước lấy lỗi (build + runtime) để report

Làm lần lượt các bước dưới. Có thể gặp **lỗi lúc build** (compile/link) hoặc **lỗi lúc chạy** (runtime); cả hai đều cần ghi lại và gửi Team SDK.

### Bước 1: Bật lại code SSE broker

**1.1** Mở `src/core/pipeline_builder_broker_nodes.cpp`:

- **Include**: Bỏ comment dòng include (khoảng dòng 51–52):
  - Đổi `// #include <cvedix/nodes/broker/cvedix_sse_broker_node.h>` thành `#include <cvedix/nodes/broker/cvedix_sse_broker_node.h>`.

- **Hàm `createSSEBrokerNode`**: Xóa dòng `throw std::runtime_error(...);` và bỏ comment toàn bộ block `/* DISABLED CODE - ASIO dependency issue` … `*/` (từ khoảng dòng 1533 đến 1614), đồng thời xóa dòng `*/` đóng block.

**1.2** Mở `src/core/pipeline_builder.cpp`:

- Tìm nhánh `nodeConfig.nodeType == "sse_broker"` (khoảng dòng 2344–2349).
- Xóa hoặc comment 3 dòng: `std::cerr`, `return nullptr;`, và dòng comment `// return PipelineBuilderBrokerNodes::...`.
- Bật lại gọi hàm: `return PipelineBuilderBrokerNodes::createSSEBrokerNode(nodeName, params, req);`

### Bước 2: Build và lấy log lỗi build (nếu có)

```bash
cd /home/cvedix/Data/project/edgeos-api
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | tee ../build_cmake.log
make -j$(nproc) 2>&1 | tee ../build_make.log
```

- Nếu **build lỗi**: copy toàn bộ nội dung `build_cmake.log` và/hoặc `build_make.log` (đặc biệt từ dòng báo lỗi trở lên vài chục dòng ngữ cảnh) để gửi Team SDK. Đây là **log lỗi build** để report.
- Nếu **build thành công**: chuyển sang Bước 3 để lấy **lỗi runtime** (nếu có).

### Bước 3: Chạy API và lấy log lỗi runtime (nếu build đã OK)

**3.1** Chạy edgeos-api và ghi log:

```bash
cd /home/cvedix/Data/project/edgeos-api
./build/bin/edgeos-api --log-api --log-instance 2>&1 | tee runtime_capture.log
```

Để chạy nền và vẫn ghi log:

```bash
./build/bin/edgeos-api --log-api --log-instance >> runtime_capture.log 2>&1 &
```

**3.2** Tạo instance có node `sse_broker` (qua API):

Ví dụ tạo solution có pipeline chứa `sse_broker`, rồi tạo instance từ solution đó. Hoặc gọi API tạo instance với body có cấu hình pipeline chứa node `sse_broker` (ví dụ port 8090, endpoint `/events`).

Ví dụ (minimal) – tạo instance với solution chứa `sse_broker`:

```bash
# Tạo instance (thay solution/config theo môi trường bạn có pipeline có sse_broker)
curl -s -X POST "http://127.0.0.1:18080/v1/core/instance" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Test-SSE-Broker",
    "solutionId": "<solution_id_có_sse_broker>",
    "additionalParams": {}
  }'
```

Sau đó start instance:

```bash
INSTANCE_ID="<id_trả_về_từ_POST_trên>"
curl -s -X POST "http://127.0.0.1:18080/v1/core/instance/${INSTANCE_ID}/start"
```

**3.3** Thu thập log runtime:

- Xem ngay: `tail -f runtime_capture.log` (hoặc `tail -f logs/instance/...` nếu log ghi vào file theo ngày).
- Nếu có crash/exception: copy toàn bộ stack trace hoặc thông báo lỗi từ `runtime_capture.log` (và stderr của process) vào một file, ví dụ `runtime_error.txt`, để gửi Team SDK.

### Bước 4: Gửi cho Team SDK

- **Nếu lỗi ở bước 2**: gửi `build_cmake.log` và/hoặc `build_make.log` (hoặc phần có lỗi + ngữ cảnh).
- **Nếu lỗi ở bước 3**: gửi `runtime_capture.log` (hoặc `runtime_error.txt`) và mô tả thao tác (tạo instance → start → lỗi xảy ra khi nào).
- Kèm theo bản tóm tắt mục 1–3 của file này và thông tin môi trường (OS, compiler, phiên bản SDK, build type).

### Sau khi xong: Hoàn nguyên code (tắt lại SSE broker)

Để tránh ảnh hưởng build thường ngày, nhớ hoàn nguyên các thay đổi ở Bước 1 (comment lại include, throw lại trong `createSSEBrokerNode`, return `nullptr` cho `sse_broker` trong `pipeline_builder.cpp`), hoặc dùng `git checkout -- src/core/pipeline_builder_broker_nodes.cpp src/core/pipeline_builder.cpp`.

---

## 5. Nội dung lỗi build gốc (chưa có trong repo)

**Lưu ý**: Repo chỉ lưu workaround, **không** lưu full compiler/linker error message. Để có nội dung lỗi chính xác cho Team SDK, thực hiện Bước 1–2 ở mục 4 trên; copy toàn bộ output lỗi (compile hoặc link) và gửi kèm báo cáo.

---

## 6. Câu hỏi / yêu cầu gửi Team SDK

1. **Phiên bản ASIO**: SDK dùng standalone ASIO hay Boost.Asio? Phiên bản/commit nào?
2. **Symbol / ODR**: Có khả năng conflict với Boost.Asio (Drogon/Trantor) khi link chung không? Nếu có, SDK có thể đổi namespace hoặc option build (header-only, static lib) để tránh conflict không?
3. **Header-only**: Nếu SDK dùng ASIO header-only, thứ tự include và macro (ví dụ `ASIO_STANDALONE`, `BOOST_ASIO_NO_DEPRECATED`) có cần chỉ định cụ thể khi tích hợp vào app dùng Boost.Asio không?
4. **Reproduce**: Khi có lại full error log từ bước 4, Team SDK có thể reproduce và đề xuất fix (phía SDK hoặc hướng dẫn tích hợp phía edgeos-api) không?

---

## 7. Liên hệ / tài liệu thêm

- edgeos-api: repo `edgeos-api` (branch hiện tại: xem `git status` / `git branch`).
- SDK: `cvedix/core_ai_runtime`.
- Known issues trong edgeos-api: mục “Known Issues” trong `docs/CVEDIX_SDK_INSTALL.md`.
