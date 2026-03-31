# OmniAPI Specifications

Thư mục này chứa các tài liệu và đặc tả API cho OmniAPI với hỗ trợ đa ngôn ngữ.

## Cấu trúc thư mục

```
api-specs/
├── openapi/
│   ├── en/
│   │   └── openapi.yaml        # OpenAPI specification (English)
│   ├── vi/
│   │   └── openapi.yaml        # OpenAPI specification (Tiếng Việt)
│   └── shared/
│       ├── schemas.yaml         # Shared schemas (optional)
│       └── responses.yaml      # Shared responses (optional)
├── scalar/
│   └── index.html              # Scalar API documentation viewer với hỗ trợ đa ngôn ngữ
├── postman/
│   └── api.collection.json     # Postman collection để test API
├── openapi.yaml                 # File OpenAPI gốc (legacy, có thể xóa)
├── swagger.json                 # Auto-generate từ openapi.yaml (optional)
└── README.md                    # File này
```

## Tính năng đa ngôn ngữ

Scalar documentation hỗ trợ chuyển đổi ngôn ngữ:
- **English (en)**: Tài liệu API bằng tiếng Anh
- **Tiếng Việt (vi)**: Tài liệu API bằng tiếng Việt

Người dùng có thể chọn ngôn ngữ từ dropdown menu ở góc trên bên phải của giao diện Scalar.

## Cho Developers

### OpenAPI Specification

Các file `openapi.yaml` trong thư mục `en/` và `vi/` là nguồn chuẩn cho API specification. Khi có thay đổi về API:

1. Cập nhật file `en/openapi.yaml` (file chính)
2. Đồng bộ và dịch các thay đổi sang `vi/openapi.yaml`
3. Đảm bảo cả hai file có cùng cấu trúc và operationId

### Cấu trúc file OpenAPI

Mỗi file OpenAPI nên có:
- `info`: Thông tin API (title, description, version)
- `servers`: Danh sách servers
- `paths`: Định nghĩa các endpoints
- `components`: Schemas, parameters, responses
- `tags`: Nhóm các endpoints
- `x-tagGroups`: Tổ chức tags thành các topic chính (Core API, SecuRT API, ONVIF API)

### Xem tài liệu API

#### Scalar Documentation (Khuyến nghị)
Mở file `scalar/index.html` trong trình duyệt để xem tài liệu API với Scalar.

**Tính năng:**
- Hỗ trợ đa ngôn ngữ (English/Tiếng Việt)
- Giao diện hiện đại và dễ sử dụng
- Tự động lưu ngôn ngữ đã chọn
- Hỗ trợ deep linking với tham số `?lang=en` hoặc `?lang=vi`

**Cách sử dụng:**
1. Mở file `scalar/index.html` trong trình duyệt
2. Chọn ngôn ngữ từ dropdown ở góc trên bên phải
3. Duyệt và tìm kiếm các API endpoints

### Cập nhật tài liệu

Khi có thay đổi API:

1. **Cập nhật file tiếng Anh:**
   - Sửa file `openapi/en/openapi.yaml`
   - Đảm bảo cấu trúc và format đúng

2. **Đồng bộ sang tiếng Việt:**
   - Cập nhật file `openapi/vi/openapi.yaml`
   - Dịch các phần sau:
     - `info.title` và `info.description`
     - `info.contact.name`
     - `servers[].description`
     - `paths[].summary` và `paths[].description`
     - `tags[].description`
     - `components.schemas[].description`

3. **Kiểm tra:**
   - Mở `scalar/index.html` và kiểm tra cả hai ngôn ngữ
   - Đảm bảo không có lỗi YAML
   - Kiểm tra tính nhất quán giữa hai file

## Cho Khách hàng / Người dùng

### Truy cập tài liệu API

Bạn có thể xem tài liệu API theo các cách sau:

1. **Scalar Documentation** (Khuyến nghị): 
   - Mở file `scalar/index.html` trong trình duyệt
   - Chọn ngôn ngữ phù hợp (English hoặc Tiếng Việt)
   - Tài liệu sẽ tự động cập nhật theo ngôn ngữ đã chọn

2. **Swagger UI**: 
   - Truy cập `http://your-server:port/swagger` khi API server đang chạy
   - Hiện tại chỉ hỗ trợ tiếng Anh

### Chuyển đổi ngôn ngữ

Trong giao diện Scalar:
- Click vào dropdown "Language / Ngôn ngữ" ở góc trên bên phải
- Chọn "English" hoặc "Tiếng Việt"
- Tài liệu sẽ tự động reload với ngôn ngữ đã chọn
- Ngôn ngữ đã chọn sẽ được lưu và tự động áp dụng cho lần truy cập sau

### Các endpoint chính

- **Core API**: `/v1/core/*` - Các endpoint cốt lõi (health, version, system, config, logs, instances, solutions, groups, models, video, fonts, node)
- **SecuRT API**: Các endpoint liên quan đến AI, Lines, Area
- **ONVIF API**: Các endpoint liên quan đến ONVIF (nếu có)

Chi tiết đầy đủ xem trong file `openapi.yaml` hoặc qua Scalar documentation.

**Hướng dẫn thao tác (cho người vận hành / non-dev):**
- **Cấu hình bind (chỉ local hay public) và restart server:** [CONFIG_BIND_AND_RESTART_MANUAL_TEST.md](../tests/manual/Core_API/CONFIG_BIND_AND_RESTART_MANUAL_TEST.md) — xem/sửa `system.web_server` (bind_mode, ip_address, port), dùng `auto_restart=true` để áp dụng ngay.

## Cấu trúc tagGroups

API được tổ chức thành 3 topic chính:

1. **Core API**: Core, Logs, Config, Instances, Solutions, Groups, Models, Video, Fonts, Node
2. **SecuRT API**: AI, Lines, Area
3. **ONVIF API**: (sẽ được thêm khi có)

## Liên hệ

Nếu có câu hỏi hoặc cần hỗ trợ, vui lòng liên hệ:
- **OmniAPI Support**: Xem thông tin trong `openapi.yaml`

