# Hướng Dẫn Sử Dụng Recognition API

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Các Endpoint](#các-endpoint)
3. [Chi Tiết Từng Method](#chi-tiết-từng-method)
4. [Examples và Cách Test](#examples-và-cách-test)
5. [Error Handling](#error-handling)

---

## Tổng Quan

Recognition API cung cấp các chức năng nhận diện và quản lý khuôn mặt, bao gồm:
- **Đăng ký khuôn mặt** (Register Face): Lưu trữ thông tin khuôn mặt để training
- **Nhận diện khuôn mặt** (Recognize Face): Nhận diện khuôn mặt trong ảnh
- **Tìm kiếm khuôn mặt** (Search Face): Tìm kiếm khuôn mặt tương tự trong database
- **Quản lý subjects**: Liệt kê, xóa, đổi tên subjects
- **Cấu hình Database**: Cấu hình kết nối MySQL/PostgreSQL

### Base URL
```
http://localhost:8080/v1/recognition
```

### Định Dạng Ảnh Hỗ Trợ
- JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP
- Kích thước tối đa: **5MB**

### Cách Gửi Ảnh
1. **Multipart/form-data** (Khuyến nghị): Upload file trực tiếp
2. **Application/json**: Gửi ảnh dưới dạng base64-encoded string

---

## Các Endpoint

| Method | Endpoint | Mô Tả |
|--------|----------|-------|
| POST | `/v1/recognition/recognize` | Nhận diện khuôn mặt trong ảnh |
| POST | `/v1/recognition/faces` | Đăng ký khuôn mặt mới |
| GET | `/v1/recognition/faces` | Liệt kê tất cả subjects (có pagination) |
| DELETE | `/v1/recognition/faces/{image_id}` | Xóa subject theo image_id hoặc subject name |
| POST | `/v1/recognition/faces/delete` | Xóa nhiều subjects cùng lúc |
| DELETE | `/v1/recognition/faces/all` | Xóa tất cả subjects |
| PUT | `/v1/recognition/subjects/{subject}` | Đổi tên subject |
| POST | `/v1/recognition/search` | Tìm kiếm khuôn mặt tương tự |
| POST | `/v1/recognition/face-database/connection` | Cấu hình database connection |
| GET | `/v1/recognition/face-database/connection` | Lấy cấu hình database hiện tại |
| DELETE | `/v1/recognition/face-database/connection` | Xóa cấu hình database |

---

## Chi Tiết Từng Method

### 1. POST `/v1/recognition/faces` - Đăng Ký Khuôn Mặt

#### Mô Tả
Đăng ký một khuôn mặt mới vào hệ thống. Hệ thống sẽ:
1. **Phát hiện khuôn mặt** (Face Detection Node) - Bắt buộc
2. Kiểm tra xem ảnh có chứa khuôn mặt không
3. Nếu không phát hiện được khuôn mặt → **Từ chối** và yêu cầu upload ảnh khác
4. Nếu phát hiện được → Lưu trữ thông tin khuôn mặt

#### Parameters

| Parameter | Type | Required | Default | Mô Tả |
|-----------|------|----------|---------|-------|
| `subject` | string | ✅ Yes | - | Tên subject (người) cần đăng ký |
| `det_prob_threshold` | float | ❌ No | 0.5 | Ngưỡng phát hiện khuôn mặt (0.0 - 1.0) |

#### Request Body

**Multipart/form-data:**
```bash
file: [binary image file]
```

**Application/json:**
```json
{
  "file": "base64-encoded-image-string"
}
```

#### Response Success (200)
```json
{
  "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
  "subject": "john_doe"
}
```

#### Response Error (400)
```json
{
  "error": "Registration failed",
  "message": "No face detected in the uploaded image. Please upload a different image that contains a clear, detectable face. The image must show a person's face clearly."
}
```

**Các lỗi có thể gặp:**
- `Missing required query parameter: subject`
- `Invalid image format or corrupted image data`
- `No face detected in the uploaded image` - **Ảnh không có khuôn mặt, bắt buộc phải upload ảnh khác**
- `No face detected with sufficient confidence` - Khuôn mặt phát hiện được nhưng confidence quá thấp

---

### 2. POST `/v1/recognition/recognize` - Nhận Diện Khuôn Mặt

#### Mô Tả
Nhận diện khuôn mặt trong ảnh đã upload. Trả về:
- Vị trí khuôn mặt (bounding box)
- Landmarks (5 điểm trên khuôn mặt)
- Danh sách subjects được nhận diện (với similarity score)
- Thời gian xử lý

#### Parameters

| Parameter | Type | Required | Default | Mô Tả |
|-----------|------|----------|---------|-------|
| `limit` | integer | ❌ No | 0 | Số lượng khuôn mặt tối đa (0 = không giới hạn) |
| `prediction_count` | integer | ❌ No | 1 | Số lượng predictions trả về cho mỗi khuôn mặt |
| `det_prob_threshold` | float | ❌ No | 0.5 | Ngưỡng phát hiện (0.0 - 1.0) |
| `threshold` | float | ❌ No | - | (Mới) Ngưỡng similarity tối thiểu để **giữ lại** kết quả subjects (0.0 - 1.0). Nếu không truyền, API trả top-N kể cả similarity thấp (backward compatible) |
| `face_plugins` | string | ❌ No | - | Face plugins (comma-separated) |
| `status` | string | ❌ No | - | Status filter |
| `detect_faces` | boolean | ❌ No | true | Có phát hiện khuôn mặt hay không |

#### Request Body
Tương tự như Register Face

#### Response Success (200)
```json
{
  "result": [
    {
      "box": {
        "probability": 1.0,
        "x_min": 548,
        "y_min": 295,
        "x_max": 1420,
        "y_max": 1368
      },
      "landmarks": [
        [814, 713],
        [1104, 829],
        [832, 937],
        [704, 1030],
        [1017, 1133]
      ],
      "subjects": [
        {
          "similarity": 0.97858,
          "subject": "john_doe"
        }
      ],
      "execution_time": {
        "detector": 117.0,
        "calculator": 45.0
      }
    }
  ]
}
```

---

### 3. GET `/v1/recognition/faces` - Liệt Kê Subjects

#### Mô Tả
Lấy danh sách tất cả subjects đã đăng ký với pagination support.

#### Parameters

| Parameter | Type | Required | Default | Mô Tả |
|-----------|------|----------|---------|-------|
| `page` | integer | ❌ No | 0 | Số trang (bắt đầu từ 0) |
| `size` | integer | ❌ No | 20 | Số lượng items mỗi trang (1-100) |
| `subject` | string | ❌ No | - | Lọc theo subject name (để trống = tất cả) |

#### Response Success (200)
```json
{
  "faces": [
    {
      "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
      "subject": "john_doe"
    },
    {
      "image_id": "7c246g6c-b476-5633-c2g2-5d0bd3ee1839",
      "subject": "jane_smith"
    }
  ],
  "page_number": 0,
  "page_size": 20,
  "total_pages": 2,
  "total_elements": 12
}
```

---

### 4. DELETE `/v1/recognition/faces/{image_id}` - Xóa Subject

#### Mô Tả
Xóa một subject theo image_id hoặc subject name.
- Nếu là **image_id**: Xóa chỉ khuôn mặt đó
- Nếu là **subject name**: Xóa tất cả khuôn mặt của subject đó

#### Parameters

| Parameter | Type | Required | Mô Tả |
|-----------|------|----------|-------|
| `image_id` | string (path) | ✅ Yes | Image ID hoặc subject name |

#### Response Success (200)

**Xóa một khuôn mặt:**
```json
{
  "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
  "subject": "john_doe"
}
```

**Xóa tất cả khuôn mặt của subject:**
```json
{
  "subject": "john_doe",
  "deleted_count": 3,
  "image_ids": [
    "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
    "7c246g6c-b476-5633-c2g2-5d0bd3ee1839",
    "8d357h7d-c587-6744-d3h3-6e1ce4ff2950"
  ]
}
```

---

### 5. POST `/v1/recognition/faces/delete` - Xóa Nhiều Subjects

#### Mô Tả
Xóa nhiều subjects cùng lúc bằng cách gửi danh sách image_ids.

#### Request Body
```json
[
  "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
  "7c246g6c-b476-5633-c2g2-5d0bd3ee1839"
]
```

#### Response Success (200)
```json
{
  "deleted": [
    {
      "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
      "subject": "john_doe"
    },
    {
      "image_id": "7c246g6c-b476-5633-c2g2-5d0bd3ee1839",
      "subject": "jane_smith"
    }
  ]
}
```

---

### 6. DELETE `/v1/recognition/faces/all` - Xóa Tất Cả

#### Mô Tả
⚠️ **CẢNH BÁO**: Xóa tất cả subjects trong database. Hành động này không thể hoàn tác!

#### Response Success (200)
```json
{
  "deleted_count": 15,
  "message": "All face subjects deleted successfully"
}
```

---

### 7. PUT `/v1/recognition/subjects/{subject}` - Đổi Tên Subject

#### Mô Tả
Đổi tên một subject. Nếu tên mới đã tồn tại, các subjects sẽ được merge.

#### Parameters

| Parameter | Type | Required | Mô Tả |
|-----------|------|----------|-------|
| `subject` | string (path) | ✅ Yes | Tên subject hiện tại |

#### Request Body
```json
{
  "subject": "new_subject_name"
}
```

#### Response Success (200)
```json
{
  "updated": "true",
  "old_subject": "john_doe",
  "new_subject": "john_doe_new",
  "message": "Subject renamed successfully"
}
```

---

### 8. POST `/v1/recognition/search` - Tìm Kiếm Khuôn Mặt

#### Mô Tả
Tìm kiếm khuôn mặt tương tự trong database. Trả về danh sách matches được sắp xếp theo similarity (cao nhất trước).
Lưu ý: Query embedding được trích xuất với augmentation nhẹ (original + horizontal flip) để ổn định hơn trong điều kiện ánh sáng/góc khác nhau.

#### Parameters

| Parameter | Type | Required | Default | Mô Tả |
|-----------|------|----------|---------|-------|
| `threshold` | float | ❌ No | 0.5 | Ngưỡng similarity tối thiểu (0.0 - 1.0) |
| `limit` | integer | ❌ No | 0 | Số lượng kết quả tối đa (0 = không giới hạn) |
| `det_prob_threshold` | float | ❌ No | 0.5 | Ngưỡng phát hiện khuôn mặt |

#### Response Success (200)
```json
{
  "result": [
    {
      "image_id": "abc123-def456-ghi789",
      "subject": "John Doe",
      "similarity": 0.95,
      "face_image": ""
    },
    {
      "image_id": "xyz789-uvw456-rst123",
      "subject": "Jane Smith",
      "similarity": 0.82,
      "face_image": ""
    }
  ],
  "faces_found": 2,
  "threshold": 0.5
}
```

---

### 9. POST `/v1/recognition/face-database/connection` - Cấu Hình Database

#### Mô Tả
Cấu hình kết nối MySQL hoặc PostgreSQL để lưu trữ face data thay vì file `face_database.txt`.

#### Request Body

**MySQL:**
```json
{
  "type": "mysql",
  "host": "localhost",
  "port": 3306,
  "database": "face_db",
  "username": "face_user",
  "password": "secure_password",
  "charset": "utf8mb4"
}
```

**PostgreSQL:**
```json
{
  "type": "postgresql",
  "host": "localhost",
  "port": 5432,
  "database": "face_db",
  "username": "face_user",
  "password": "secure_password"
}
```

**Tắt database (dùng file):**
```json
{
  "enabled": false
}
```

#### Response Success (200)
```json
{
  "message": "Face database connection configured successfully",
  "config": {
    "type": "mysql",
    "host": "localhost",
    "port": 3306,
    "database": "face_db",
    "username": "face_user",
    "charset": "utf8mb4"
  },
  "note": "Database connection will be used instead of face_database.txt file"
}
```

---

### 10. GET `/v1/recognition/face-database/connection` - Lấy Cấu Hình Database

#### Mô Tả
Lấy cấu hình database hiện tại.

#### Response Success (200)
```json
{
  "enabled": true,
  "config": {
    "type": "mysql",
    "host": "localhost",
    "port": 3306,
    "database": "face_db",
    "username": "face_user",
    "charset": "utf8mb4"
  }
}
```

---

## Examples và Cách Test

### 1. Test với cURL

#### Đăng ký khuôn mặt (Multipart)
```bash
curl -X POST "http://localhost:8080/v1/recognition/faces?subject=john_doe&det_prob_threshold=0.5" \
  -F "file=@/path/to/face_image.jpg"
```

#### Đăng ký khuôn mặt (Base64)
```bash
# Encode image to base64
IMAGE_B64=$(base64 -w 0 /path/to/face_image.jpg)

curl -X POST "http://localhost:8080/v1/recognition/faces?subject=john_doe" \
  -H "Content-Type: application/json" \
  -d "{\"file\": \"$IMAGE_B64\"}"
```

#### Nhận diện khuôn mặt
```bash
curl -X POST "http://localhost:8080/v1/recognition/recognize?det_prob_threshold=0.5&prediction_count=3" \
  -F "file=@/path/to/test_image.jpg"
```

#### Liệt kê subjects
```bash
curl "http://localhost:8080/v1/recognition/faces?page=0&size=20"
```

#### Xóa subject
```bash
curl -X DELETE "http://localhost:8080/v1/recognition/faces/6b135f5b-a365-4522-b1f1-4c9ac2dd0728"
```

#### Tìm kiếm khuôn mặt
```bash
curl -X POST "http://localhost:8080/v1/recognition/search?threshold=0.7&limit=10" \
  -F "file=@/path/to/search_image.jpg"
```

---

### 2. Test với Python

Tạo file `test_recognition.py`:

```python
import requests
import base64
import json

BASE_URL = "http://localhost:8080/v1/recognition"

# 1. Đăng ký khuôn mặt (Multipart)
def register_face_multipart(subject_name, image_path):
    url = f"{BASE_URL}/faces"
    params = {
        "subject": subject_name,
        "det_prob_threshold": 0.5
    }
    with open(image_path, "rb") as f:
        files = {"file": f}
        response = requests.post(url, params=params, files=files)
    return response.json()

# 2. Đăng ký khuôn mặt (Base64)
def register_face_base64(subject_name, image_path):
    url = f"{BASE_URL}/faces"
    params = {"subject": subject_name}
    
    with open(image_path, "rb") as f:
        image_b64 = base64.b64encode(f.read()).decode("utf-8")
    
    data = {"file": image_b64}
    headers = {"Content-Type": "application/json"}
    response = requests.post(url, params=params, json=data, headers=headers)
    return response.json()

# 3. Nhận diện khuôn mặt
def recognize_face(image_path, det_threshold=0.5, prediction_count=3):
    url = f"{BASE_URL}/recognize"
    params = {
        "det_prob_threshold": det_threshold,
        "prediction_count": prediction_count
    }
    with open(image_path, "rb") as f:
        files = {"file": f}
        response = requests.post(url, params=params, files=files)
    return response.json()

# 4. Liệt kê subjects
def list_faces(page=0, size=20, subject_filter=None):
    url = f"{BASE_URL}/faces"
    params = {"page": page, "size": size}
    if subject_filter:
        params["subject"] = subject_filter
    response = requests.get(url, params=params)
    return response.json()

# 5. Xóa subject
def delete_face(image_id_or_subject):
    url = f"{BASE_URL}/faces/{image_id_or_subject}"
    response = requests.delete(url)
    return response.json()

# 6. Tìm kiếm khuôn mặt
def search_face(image_path, threshold=0.5, limit=10):
    url = f"{BASE_URL}/search"
    params = {
        "threshold": threshold,
        "limit": limit
    }
    with open(image_path, "rb") as f:
        files = {"file": f}
        response = requests.post(url, params=params, files=files)
    return response.json()

# 7. Đổi tên subject
def rename_subject(old_name, new_name):
    url = f"{BASE_URL}/subjects/{old_name}"
    data = {"subject": new_name}
    response = requests.put(url, json=data)
    return response.json()

# 8. Cấu hình database
def configure_database(config):
    url = f"{BASE_URL}/face-database/connection"
    response = requests.post(url, json=config)
    return response.json()

# Example usage
if __name__ == "__main__":
    # Đăng ký khuôn mặt
    result = register_face_multipart("john_doe", "face.jpg")
    print("Register result:", json.dumps(result, indent=2))
    
    # Nhận diện khuôn mặt
    result = recognize_face("test_image.jpg")
    print("Recognition result:", json.dumps(result, indent=2))
    
    # Liệt kê subjects
    result = list_faces(page=0, size=20)
    print("List faces:", json.dumps(result, indent=2))
```

Chạy test:
```bash
python test_recognition.py
```

---

### 3. Test với JavaScript/Node.js

Tạo file `test_recognition.js`:

```javascript
const axios = require('axios');
const FormData = require('form-data');
const fs = require('fs');

const BASE_URL = 'http://localhost:8080/v1/recognition';

// 1. Đăng ký khuôn mặt
async function registerFace(subjectName, imagePath) {
  const form = new FormData();
  form.append('file', fs.createReadStream(imagePath));
  
  const response = await axios.post(
    `${BASE_URL}/faces?subject=${subjectName}&det_prob_threshold=0.5`,
    form,
    { headers: form.getHeaders() }
  );
  return response.data;
}

// 2. Nhận diện khuôn mặt
async function recognizeFace(imagePath) {
  const form = new FormData();
  form.append('file', fs.createReadStream(imagePath));
  
  const response = await axios.post(
    `${BASE_URL}/recognize?det_prob_threshold=0.5&prediction_count=3`,
    form,
    { headers: form.getHeaders() }
  );
  return response.data;
}

// 3. Liệt kê subjects
async function listFaces(page = 0, size = 20) {
  const response = await axios.get(
    `${BASE_URL}/faces?page=${page}&size=${size}`
  );
  return response.data;
}

// 4. Tìm kiếm khuôn mặt
async function searchFace(imagePath, threshold = 0.5) {
  const form = new FormData();
  form.append('file', fs.createReadStream(imagePath));
  
  const response = await axios.post(
    `${BASE_URL}/search?threshold=${threshold}`,
    form,
    { headers: form.getHeaders() }
  );
  return response.data;
}

// Example usage
(async () => {
  try {
    // Đăng ký
    const registerResult = await registerFace('john_doe', './face.jpg');
    console.log('Register:', JSON.stringify(registerResult, null, 2));
    
    // Nhận diện
    const recognizeResult = await recognizeFace('./test_image.jpg');
    console.log('Recognize:', JSON.stringify(recognizeResult, null, 2));
    
    // Liệt kê
    const listResult = await listFaces();
    console.log('List:', JSON.stringify(listResult, null, 2));
  } catch (error) {
    console.error('Error:', error.response?.data || error.message);
  }
})();
```

Cài đặt dependencies:
```bash
npm install axios form-data
```

Chạy test:
```bash
node test_recognition.js
```

---

### 4. Test với Postman

1. Import collection từ file `api-specs/postman/api.collection.json`
2. Tìm folder "Recognition"
3. Chọn request cần test
4. Thêm file ảnh vào body (form-data)
5. Điền các parameters cần thiết
6. Click "Send"

---

### 5. Test Face Detection Validation

Để test tính năng **Face Detection Node** (từ chối ảnh không có khuôn mặt):

#### Test Case 1: Ảnh có khuôn mặt (Thành công)
```bash
curl -X POST "http://localhost:8080/v1/recognition/faces?subject=test_person" \
  -F "file=@face_image.jpg"
```

**Expected Response (200):**
```json
{
  "image_id": "uuid-here",
  "subject": "test_person"
}
```

#### Test Case 2: Ảnh không có khuôn mặt (Bị từ chối)
```bash
curl -X POST "http://localhost:8080/v1/recognition/faces?subject=test_person" \
  -F "file=@landscape_image.jpg"
```

**Expected Response (400):**
```json
{
  "error": "Registration failed",
  "message": "No face detected in the uploaded image. Please upload a different image that contains a clear, detectable face. The image must show a person's face clearly."
}
```

#### Test Case 3: Ảnh có khuôn mặt nhưng confidence thấp (Bị từ chối)
```bash
curl -X POST "http://localhost:8080/v1/recognition/faces?subject=test_person&det_prob_threshold=0.9" \
  -F "file=@blurry_face_image.jpg"
```

**Expected Response (400):**
```json
{
  "error": "Registration failed",
  "message": "No face detected with sufficient confidence in the uploaded image. Please upload a different image with a clearer, more visible face."
}
```

---

## Error Handling

### Các Mã Lỗi Thường Gặp

| Status Code | Mô Tả | Giải Pháp |
|-------------|-------|-----------|
| 400 | Bad Request | Kiểm tra parameters, format ảnh, kích thước file |
| 401 | Unauthorized | Kiểm tra API key (nếu có) |
| 404 | Not Found | Kiểm tra image_id hoặc subject name có tồn tại |
| 500 | Internal Server Error | Kiểm tra logs server, model files |

### Cấu Trúc Error Response

```json
{
  "error": "Error type",
  "message": "Detailed error message"
}
```

### Các Lỗi Cụ Thể

#### 1. Missing Subject Parameter
```json
{
  "error": "Invalid request",
  "message": "Missing required query parameter: subject"
}
```

#### 2. Invalid Image Format
```json
{
  "error": "Invalid request",
  "message": "Invalid image format or corrupted image data"
}
```

#### 3. No Face Detected (Face Detection Node)
```json
{
  "error": "Registration failed",
  "message": "No face detected in the uploaded image. Please upload a different image that contains a clear, detectable face. The image must show a person's face clearly."
}
```

#### 4. Face Detected but Low Confidence
```json
{
  "error": "Registration failed",
  "message": "No face detected with sufficient confidence in the uploaded image. Please upload a different image with a clearer, more visible face."
}
```

#### 5. Subject Not Found
```json
{
  "error": "Not found",
  "message": "Face subject not found"
}
```

---

## Best Practices

### 1. Đăng Ký Khuôn Mặt
- ✅ Sử dụng ảnh có khuôn mặt rõ ràng, đủ ánh sáng
- ✅ Ảnh nên chứa **chỉ một khuôn mặt**
- ✅ Khuôn mặt nên chiếm ít nhất 30-40% diện tích ảnh
- ❌ Tránh ảnh mờ, tối, hoặc khuôn mặt quá nhỏ
- ❌ Tránh ảnh có nhiều khuôn mặt (chỉ lấy khuôn mặt đầu tiên)

### 2. Nhận Diện Khuôn Mặt
- Điều chỉnh `det_prob_threshold` theo môi trường:
  - **0.3-0.5**: Môi trường có nhiều ánh sáng, khuôn mặt rõ
  - **0.5-0.7**: Môi trường bình thường
  - **0.7-0.9**: Môi trường tối hoặc khuôn mặt mờ

### 3. Quản Lý Database
- Nên sử dụng database (MySQL/PostgreSQL) cho production
- Backup database thường xuyên
- Sử dụng pagination khi liệt kê subjects (tránh load quá nhiều)

### 4. Performance
- Giới hạn kích thước ảnh (tối đa 5MB)
- Sử dụng `limit` parameter khi recognize để giới hạn số lượng khuôn mặt
- Cache kết quả recognition nếu có thể

---

## Troubleshooting

### Vấn Đề: Không phát hiện được khuôn mặt

**Nguyên nhân:**
- Ảnh không có khuôn mặt
- Khuôn mặt quá nhỏ hoặc mờ
- `det_prob_threshold` quá cao

**Giải pháp:**
1. Kiểm tra ảnh có chứa khuôn mặt rõ ràng
2. Giảm `det_prob_threshold` xuống 0.3-0.4
3. Sử dụng ảnh có độ phân giải cao hơn

### Vấn Đề: Recognition không chính xác

**Nguyên nhân:**
- Chưa đăng ký đủ ảnh cho subject
- Ảnh đăng ký không đa dạng (góc độ, ánh sáng)

**Giải pháp:**
1. Đăng ký nhiều ảnh cho mỗi subject (3-5 ảnh)
2. Sử dụng ảnh với các góc độ và điều kiện ánh sáng khác nhau
3. Điều chỉnh `threshold` trong search

### Vấn Đề: Database connection failed

**Nguyên nhân:**
- Thông tin kết nối sai
- Database chưa được tạo
- Tables chưa được tạo

**Giải pháp:**
1. Kiểm tra thông tin host, port, username, password
2. Tạo database và tables theo schema trong documentation
3. Xem logs server để biết lỗi cụ thể

---

## Tài Liệu Tham Khảo

- [Face Database Connection Guide](./FACE_DATABASE_CONNECTION.md)
- [API Documentation](./API.md)
- [OpenAPI Specification](../api-specs/openapi.yaml)

---

## Changelog

### Version 1.0
- ✅ Thêm Face Detection Node bắt buộc trước khi đăng ký
- ✅ Từ chối ảnh không có khuôn mặt với thông báo rõ ràng
- ✅ Validation confidence threshold cho khuôn mặt phát hiện được

