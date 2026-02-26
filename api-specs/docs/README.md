# API documentation (Markdown)

Thư mục này chứa tài liệu API dạng Markdown, được sinh từ OpenAPI spec.

## Cách tạo tài liệu

Chạy script với **option 2** để merge spec và xuất file Markdown tổng:

```bash
# Tiếng Anh
python3 scripts/merge_openapi.py api-specs/openapi/en 2

# Tiếng Việt
python3 scripts/merge_openapi.py api-specs/openapi/vi 2
```

Kết quả:
- `api-specs/docs/en/API.md` — tài liệu API tiếng Anh
- `api-specs/docs/vi/API.md` — tài liệu API tiếng Việt

## Option script merge_openapi.py

| Option | Mô tả |
|--------|--------|
| **1** (mặc định) | Merge các file paths thành một file YAML tổng: `openapi.yaml` trong thư mục spec (vd: `api-specs/openapi/en/openapi.yaml`) |
| **2** | Merge spec và tạo file Markdown tổng trong thư mục này: `api-specs/docs/<locale>/API.md` |

Ví dụ:

```bash
# Option 1: chỉ tạo/cập nhật openapi.yaml
python3 scripts/merge_openapi.py api-specs/openapi/en
python3 scripts/merge_openapi.py api-specs/openapi/en 1

# Option 2: tạo/cập nhật API.md trong api-specs/docs
python3 scripts/merge_openapi.py api-specs/openapi/en 2
python3 scripts/merge_openapi.py api-specs/openapi/vi 2
```
