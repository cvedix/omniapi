# Phân tích: "open rtsp failed" khi stream vẫn ổn định

## Nguồn log

Message **"open rtsp failed, try again... (attempt N)"** và **"open rtsp failed 5 times, waiting 2000 ms before retry..."** xuất phát từ **CVEDIX SDK** (omniruntime), không nằm trong repo api.

- **File nguồn (trong SDK):** `/workspace/omniruntime/nodes/src/cvedix_rtsp_src_node.cpp` (khoảng dòng 140–145).
- **Repo api:** Chỉ link tới SDK (`cvedix_rtsp_src_node.h` / `libcvedix_core.so`), không có source của node RTSP.

## Tại sao stream ổn định mà vẫn báo failed?

### 1. Retry khi khởi động (khả năng cao)

- Node RTSP trong SDK **retry nhiều lần** khi mở stream.
- Các lần đầu có thể **fail** (timeout, server chưa sẵn sàng, chưa nhận frame) → log "open rtsp failed, try again... (attempt 1..5)".
- Sau đó **một lần retry thành công** → stream chạy ổn định.
- Log chỉ phản ánh **từng lần thử thất bại**, không có nghĩa là **cả phiên stream đang fail**. Nếu sau các dòng "failed" bạn vẫn thấy stream chạy (FPS, output RTMP/RTSP bình thường) thì đây là trường hợp này.

### 2. Điều kiện "open success" trong SDK quá chặt

- SDK có thể coi "open" thành công chỉ khi:
  - Đã nhận frame đầu, hoặc
  - Pipeline đã ở trạng thái PLAYING trong một **timeout ngắn**.
- Nếu stream chậm trả frame đầu (ví dụ keyframe lâu), **vài attempt đầu** có thể bị coi là "open failed" dù stream thực tế ổn sau đó.

### 3. RTSP server / converter chậm sẵn sàng

- Nếu nguồn là RTSP qua MediaMTX hoặc converter (RTMP→RTSP), stream có thể **chưa sẵn sàng ngay** khi instance start.
- Vài lần mở đầu → fail → **sau vài retry (và 2000 ms chờ)** → lần mở sau thành công → stream ổn định.

## Hành động đề xuất

### Trong repo omniruntime (SDK)

1. Mở `nodes/src/cvedix_rtsp_src_node.cpp`, tìm đoạn quanh dòng **140–145** (log "open rtsp failed").
2. Xem rõ:
   - Điều kiện nào được coi là **"open failed"** (timeout, lỗi GStreamer, chưa có frame, …).
   - Timeout cho mỗi lần "open" và số lần retry.
3. Cân nhắc:
   - **Nới timeout** cho lần mở đầu (nếu quá ngắn so với thời gian stream thực tế sẵn sàng).
   - Chỉ log "open rtsp failed" ở mức **DEBUG** sau khi đã retry thành công, hoặc đổi message thành **"open rtsp retrying... (attempt N)"** để tránh hiểu nhầm là stream chết.

### Trong api (tạm thời)

- Giảm log ồn từ SDK: set **`CVEDIX_LOG_LEVEL=ERROR`** (ẩn WARN). Chỉ nên dùng khi debug; có thể che mất cảnh báo hữu ích khác.
- Ghi chú trong tài liệu/tests: log "open rtsp failed" trong giai đoạn start là **bình thường** nếu sau đó stream chạy ổn (retry thành công).

## Kết luận

- **"open rtsp failed"** trong log là từ **CVEDIX SDK** (cvedix_rtsp_src_node), mô tả **từng lần thử mở RTSP thất bại** trước khi retry thành công.
- Stream **ổn định sau đó** thường có nghĩa retry đã thành công; cần chỉnh **logic/điều kiện "open" và cách log** trong SDK (omniruntime) nếu muốn giảm cảnh báo hoặc làm rõ trạng thái.
