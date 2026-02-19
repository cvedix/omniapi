# OpenAPI Specifications - Split Structure

Các file OpenAPI đã được tách thành các file nhỏ hơn - **mỗi API endpoint một file riêng** để dễ quản lý và chỉnh sửa.

## Cấu trúc thư mục

```
openapi/
├── en/                          # English version
│   ├── openapi.yaml            # File chính (được merge tự động)
│   ├── paths/                  # Các file endpoint được tổ chức theo tag
│   │   ├── core/              # Core API endpoints
│   │   │   ├── core_health.yaml
│   │   │   ├── core_version.yaml
│   │   │   ├── core_system_info.yaml
│   │   │   └── ...
│   │   ├── config/            # Config API endpoints
│   │   │   ├── core_config.yaml
│   │   │   └── ...
│   │   ├── ai/                # AI API endpoints
│   │   │   ├── core_ai_process.yaml
│   │   │   ├── core_ai_batch.yaml
│   │   │   └── ...
│   │   ├── instances/         # Instances API endpoints
│   │   ├── models/            # Models API endpoints
│   │   ├── video/             # Video API endpoints
│   │   ├── onvif/             # ONVIF API endpoints
│   │   └── ...                # Các tag khác
│   └── components/
│       └── schemas.yaml       # Component schemas
│
├── vi/                          # Vietnamese version (cấu trúc tương tự)
│   ├── openapi.yaml
│   ├── paths/
│   │   └── <tag>/
│   │       └── <endpoint>.yaml
│   └── components/
│
└── shared/                      # Shared files (nếu có)
    ├── schemas.yaml
    └── responses.yaml
```

## Cách sử dụng

### Chỉnh sửa một API endpoint

1. **Tìm file endpoint cần chỉnh sửa:**
   - Mỗi endpoint có một file riêng trong thư mục `paths/<tag>/`
   - Tên file dựa trên path của endpoint
   - Ví dụ: Endpoint `/v1/core/ai/process` → `paths/ai/core_ai_process.yaml`

2. **Chỉnh sửa file endpoint:**
   ```bash
   # Ví dụ: Chỉnh sửa endpoint AI process
   vim api-specs/openapi/en/paths/ai/core_ai_process.yaml
   ```

3. **Sau khi chỉnh sửa, merge lại file chính:**
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/en
   ```
   Hoặc cho tiếng Việt:
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/vi
   ```

4. **File `openapi.yaml` sẽ được cập nhật tự động** với thay đổi từ file endpoint.

### Chỉnh sửa schemas

1. **Chỉnh sửa file schemas:**
   - Mở `en/components/schemas.yaml` (hoặc `vi/components/schemas.yaml`)

2. **Merge lại file chính:**
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/en
   ```

### Tách lại file (nếu cần)

Nếu bạn muốn tách lại file từ đầu (ví dụ sau khi chỉnh sửa trực tiếp file `openapi.yaml`):

```bash
python3 scripts/split_openapi.py api-specs/openapi/en/openapi.yaml api-specs/openapi/en en
```

**Lưu ý:** Script sẽ tự động tạo backup file `openapi.yaml.backup` trước khi tách.

## Lợi ích

1. **Dễ quản lý:** Mỗi API endpoint có file riêng, rất dễ tìm và chỉnh sửa
2. **Giảm xung đột:** Khi làm việc nhóm, ít xung đột hơn vì mỗi người có thể làm việc trên endpoint khác nhau
3. **Dễ đọc:** File rất nhỏ, chỉ chứa một endpoint, dễ đọc và hiểu
4. **Tổ chức tốt:** Endpoints được tổ chức theo tag trong các thư mục con
5. **Tương thích:** File `openapi.yaml` chính vẫn được merge đầy đủ, hoạt động tốt với Swagger/Scalar

## Tương thích với Swagger/Scalar

File `openapi.yaml` chính được merge đầy đủ từ tất cả các file endpoint, vì vậy:
- ✅ Swagger UI vẫn hoạt động bình thường
- ✅ Scalar documentation vẫn hiển thị đầy đủ
- ✅ Tất cả các tool khác vẫn hoạt động tốt

## Quy trình làm việc khuyến nghị

1. **Chỉnh sửa file endpoint** trong thư mục `paths/<tag>/` (không chỉnh sửa trực tiếp `openapi.yaml`)
2. **Chạy script merge** sau khi chỉnh sửa:
   ```bash
   python3 scripts/merge_openapi.py api-specs/openapi/en
   ```
3. **Kiểm tra** trên Swagger/Scalar để đảm bảo mọi thứ hoạt động đúng
4. **Commit** cả file endpoint và file `openapi.yaml` đã merge

## Tìm file endpoint

Để tìm file endpoint cần chỉnh sửa:

```bash
# Tìm tất cả endpoint trong một tag
ls api-specs/openapi/en/paths/ai/

# Tìm file endpoint theo tên
find api-specs/openapi/en/paths -name "*process*"

# Xem tất cả các tag
ls api-specs/openapi/en/paths/
```

## Scripts

- `scripts/split_openapi.py`: Tách file OpenAPI lớn thành các file nhỏ
- `scripts/merge_openapi.py`: Merge các file paths lại thành file `openapi.yaml` chính
