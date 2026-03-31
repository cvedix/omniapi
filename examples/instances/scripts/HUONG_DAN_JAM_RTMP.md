# Hướng Dẫn Test Instance với Jam Node và RTMP Stream

## Câu Hỏi

Làm thế nào để test start một instance xử lý với jam node rồi stream output kết quả ra một luồng RTMP để kiểm tra kết quả xử lý?

## Trả Lời

Có 2 cách để thực hiện:

### Cách 1: Sử dụng Script Tự Động (Khuyến nghị)

Chạy script `test_jam_rtmp.sh`:

```bash
cd examples/instances/scripts
./test_jam_rtmp.sh
```

Script sẽ tự động:
1. Tạo instance với solution `ba_jam_default`
2. Thêm jam zone vào instance
3. Cấu hình RTMP stream output
4. Khởi động instance
5. Hiển thị hướng dẫn kiểm tra kết quả

**Tùy chọn:** Có thể chỉ định BASE_URL và RTMP_URL:
```bash
./test_jam_rtmp.sh http://localhost:8848 rtmp://localhost:1935/live/jam_test
```

### Cách 2: Thực Hiện Thủ Công

#### Bước 1: Tạo Instance với BA Jam Solution

```bash
curl -X POST http://localhost:8848/v1/core/instance \
  -H 'Content-Type: application/json' \
  -d '{
    "name": "jam_test_instance",
    "solution": "ba_jam_default",
    "autoStart": false,
    "additionalParams": {
      "RTSP_URL": "rtsp://localhost:8554/live/stream1"
    }
  }'
```

Lưu `instanceId` từ response.

#### Bước 2: Thêm Jam Zones

```bash
INSTANCE_ID="your-instance-id"

curl -X POST http://localhost:8848/v1/core/instance/${INSTANCE_ID}/jams \
  -H 'Content-Type: application/json' \
  -d '{
    "name": "Downtown Jam Zone",
    "roi": [
      {"x": 0, "y": 100},
      {"x": 1920, "y": 100},
      {"x": 1920, "y": 400},
      {"x": 0, "y": 400}
    ],
    "enabled": true,
    "check_interval_frames": 20,
    "check_min_hit_frames": 50,
    "check_max_distance": 8,
    "check_min_stops": 8,
    "check_notify_interval": 10
  }'
```

#### Bước 3: Cấu Hình RTMP Stream Output

```bash
curl -X POST http://localhost:8848/v1/core/instance/${INSTANCE_ID}/output/stream \
  -H 'Content-Type: application/json' \
  -d '{
    "enabled": true,
    "uri": "rtmp://localhost:1935/live/jam_test"
  }'
```

#### Bước 4: Khởi Động Instance

```bash
curl -X POST http://localhost:8848/v1/core/instance/${INSTANCE_ID}/start
```

#### Bước 5: Kiểm Tra Kết Quả

**Xem RTMP stream:**
```bash
ffplay rtmp://localhost:1935/live/jam_test
```

**Kiểm tra output qua API:**
```bash
curl http://localhost:8848/v1/core/instance/${INSTANCE_ID}/output | jq '.'
```

## Yêu Cầu Tiên Quyết

1. **RTMP Server:** Cần có RTMP server đang chạy (ví dụ: MediaMTX)
   ```bash
   # Kiểm tra MediaMTX
   netstat -tlnp | grep 1935
   ```

2. **Input Source:** Cần có input source hợp lệ (RTSP stream hoặc video file)
   - RTSP: `rtsp://host:port/path`
   - File: Đường dẫn đến file video

3. **API Server:** OmniAPI phải đang chạy trên port 8848 (hoặc port bạn chỉ định)

## Giải Thích Workflow

1. **Tạo Instance:** Tạo một instance mới với solution `ba_jam_default` - solution này có sẵn pipeline với jam detection node.

2. **Thêm Jam Zones:** Định nghĩa các vùng (ROI) để phát hiện kẹt xe. Mỗi zone có các tham số:
   - `roi`: Vùng phát hiện (polygon)
   - `check_interval_frames`: Tần suất kiểm tra
   - `check_min_hit_frames`: Số frame tối thiểu để xác nhận
   - `check_max_distance`: Khoảng cách tối đa giữa các object
   - `check_min_stops`: Số object dừng tối thiểu để phát hiện jam

3. **Cấu Hình RTMP:** Cấu hình instance để stream output ra RTMP server. Instance sẽ tự động restart để áp dụng cấu hình.

4. **Start Instance:** Khởi động pipeline để bắt đầu xử lý. Pipeline sẽ:
   - Đọc input (RTSP/file)
   - Chạy detector để phát hiện objects
   - Tracking objects
   - Phân tích jam zones
   - Vẽ OSD (overlay) lên frame
   - Stream ra RTMP

5. **Kiểm Tra:** Xem stream qua RTMP hoặc kiểm tra output qua API.

## API Endpoints Quan Trọng

- `POST /v1/core/instance` - Tạo instance
- `POST /v1/core/instance/{id}/jams` - Thêm jam zones
- `POST /v1/core/instance/{id}/output/stream` - Cấu hình RTMP output
- `POST /v1/core/instance/{id}/start` - Khởi động instance
- `GET /v1/core/instance/{id}/output` - Lấy kết quả xử lý
- `GET /v1/core/instance/{id}/statistics` - Lấy thống kê

## Troubleshooting

**Instance không start được:**
- Kiểm tra input source (RTSP_URL/FILE_PATH) có hợp lệ không
- Kiểm tra logs của API server
- Kiểm tra solution có tồn tại: `GET /v1/core/solution`

**RTMP stream không hoạt động:**
- Kiểm tra RTMP server có đang chạy không
- Kiểm tra firewall
- Test RTMP server với ffmpeg trước

**Jam zones không hoạt động:**
- Kiểm tra jam zones đã được thêm chưa
- Kiểm tra instance có đang chạy không
- Đợi instance restart sau khi thêm jam zones

## Tài Liệu Tham Khảo

- Script chi tiết: `test_jam_rtmp.sh`
- README đầy đủ: `README_JAM_RTMP.md`
- API Documentation: `docs/API_document.md`
