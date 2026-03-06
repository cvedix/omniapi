# Postman Collection Builder

Thư mục này chứa script để tự động build Postman collection từ OpenAPI specification.

## Yêu cầu

- Node.js và npm (để chạy công cụ `openapi-to-postmanv2`)
- File OpenAPI tại `../openapi.yaml`

## Cách sử dụng

### Option 1: Sử dụng Bash script (khuyến nghị)

```bash
cd /home/cvedix/Data/DEV/DEV_1/api/api-specs/postman
./build-collection.sh
```

### Option 2: Sử dụng Python script

```bash
cd /home/cvedix/Data/DEV/DEV_1/api/api-specs/postman
python3 build-collection.py
```

## Output

Script sẽ tạo file `api.collection.json` trong thư mục này, có thể import trực tiếp vào Postman.

## Cài đặt công cụ

Nếu chưa có `openapi-to-postmanv2`, script sẽ tự động cài đặt:

```bash
npm install -g openapi-to-postmanv2
```

## Lưu ý

- Script sẽ tự động kiểm tra và cài đặt dependencies nếu cần
- File output sẽ được ghi đè mỗi lần chạy script
- Đảm bảo file `../openapi.yaml` tồn tại và hợp lệ

