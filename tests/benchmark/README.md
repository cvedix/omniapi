# Benchmark – Edge AI Instance

Công cụ chạy benchmark cho instance Edge AI API: thu thập FPS, CPU, RAM, latency, load; với instance có output MQTT thì thu thêm **thời gian từ phát hiện đến gửi MQTT**. Báo cáo HTML dễ đọc cho người không chuyên dev.

- **`run_benchmark.py`** – Benchmark instance ba_crossline MQTT (config mặc định `instance_benchmark.json`).
- **`run_benchmark_generic.py`** – Benchmark **bất kỳ instance nào** (truyền `--config` tới file JSON của instance):
  - Instance có MQTT (additionalParams.output.MQTT_BROKER_URL): thu thêm detection → MQTT.
  - Instance không có MQTT: chỉ thu **pipeline** (nhận frame → detect → output) qua API `/statistics` và tài nguyên hệ thống.

## Cấu trúc thư mục

```
tests/benchmark/
├── config/
│   └── instance_benchmark.json   # Cấu hình instance mặc định (ba_crossline RTMP + MQTT)
├── output/                         # Thư mục ra (CSV, events JSON, HTML) – tạo tự động
├── run_benchmark.py               # Benchmark ba_crossline MQTT
├── run_benchmark_generic.py       # Benchmark bất kỳ instance (--config <path>)
├── report_generator.py            # Sinh file HTML từ CSV + events
├── requirements.txt
└── README.md
```

## Cài đặt

**Trên Debian/Ubuntu:** Nếu dùng virtualenv, cần cài `python3-venv` trước (chỉ cần làm một lần):

```bash
sudo apt install python3.12-venv
```

**Cách 1 – Virtual environment (khuyến nghị):**

```bash
cd /home/cvedix/Data/project/omniapi/tests/benchmark
python3 -m venv venv
source venv/bin/activate    # Windows: venv\Scripts\activate
pip install -r requirements.txt
```

**Cách 2 – Cài cho user (không dùng venv):**

```bash
cd /home/cvedix/Data/project/omniapi/tests/benchmark
pip install --user -r requirements.txt
# Nếu lệnh pip/python không tìm thấy sau khi cài: export PATH=$HOME/.local/bin:$PATH
```

Sau khi cài (cách 1), mỗi lần chạy benchmark nhớ `source venv/bin/activate` trong thư mục `tests/benchmark` rồi chạy `python run_benchmark.py`.

## Chạy benchmark

1. **Đảm bảo Edge AI API đang chạy** (ví dụ `http://localhost:8080`).

2. Chạy với tham số mặc định (duration 120 giây, poll 2 giây, có MQTT, có báo cáo HTML):

```bash
python run_benchmark.py
```

3. Tuỳ chọn:

```bash
python run_benchmark.py --base-url http://192.168.1.100:8080
python run_benchmark.py --duration 300 --poll-interval 1
python run_benchmark.py --no-mqtt          # Không subscribe MQTT (không có số liệu detection → MQTT)
python run_benchmark.py --skip-report     # Không tạo file HTML
python run_benchmark.py --no-start        # Instance đã chạy sẵn: không gọi start/stop, chỉ thu metrics
python run_benchmark.py --reuse-existing  # Tìm instance theo tên, dùng nếu có (mặc định: mỗi lần chạy tạo instance mới)
python run_benchmark.py --report-only     # Chỉ tạo lại báo cáo HTML từ kết quả vừa thu thập (dùng CSV/events mới nhất trong output/)
python run_benchmark.py --report-only --csv output/benchmark_metrics_20260304_152951.csv --events output/benchmark_mqtt_events_20260304_152951.json  # Chỉ định file cụ thể
python run_benchmark.py --output-dir ./my_output
python run_benchmark.py --config ./config/instance_benchmark.json
```

**Khi API không báo instance “running” kịp thời** (instance thực tế đang chạy trong worker): dùng `--no-start` khi instance đã được start từ trước, script sẽ chỉ thu metrics và không gọi stop. Nếu không dùng `--no-start`, script vẫn tiếp tục thu metrics sau khi timeout đợi running (chỉ in cảnh báo).

**Benchmark bất kỳ instance (generic):**

```bash
python run_benchmark_generic.py --config path/to/instance_config.json
python run_benchmark_generic.py --config examples/instances/ba_crossline/yolo/example_ba_crossline_rtsp_mqtt.json --duration 60
```

Cùng các tuỳ chọn `--base-url`, `--duration`, `--no-mqtt`, `--no-start`, `--reuse-existing`, `--report-only`, v.v. như `run_benchmark.py`. Instance không có MQTT sẽ chỉ thu FPS, latency, frames_processed từ API và tài nguyên hệ thống.

4. Biến môi trường:

- `EDGE_AI_API_URL`: base URL API (mặc định `http://localhost:8080`).

5. **Cấu hình giá trị test trong code:** Trong `run_benchmark.py`, khối `BENCHMARK_CONFIG` ở đầu file chứa các biến có thể sửa (duration, poll interval, timeouts đợi ready/running, timeout gọi API, MQTT keepalive, …). Sửa trực tiếp trong file khi cần thay đổi giá trị mặc định mà không dùng tham số dòng lệnh.

## Instance dùng để benchmark

Theo file `config/instance_benchmark.json`:

- **Tên:** `ba_crossline_mqtt_with_lines_demo`
- **Solution:** `ba_crossline`
- **Nguồn:** RTMP (`RTMP_SRC_URL`)
- **Đích:** MQTT (broker, topic lấy từ `additionalParams.output`), có CrossingLines

**InstanceId:** Không hardcode – mỗi lần tạo instance API trả về một `instanceId` khác nhau. **Mặc định mỗi lần chạy `python run_benchmark.py` sẽ tạo instance mới** để test, dùng `instanceId` API trả về. Dùng `--reuse-existing` nếu muốn tìm instance theo tên trong config và dùng lại (nếu có).

## Số liệu thu thập

| Nguồn | Số liệu |
|-------|--------|
| **GET /v1/core/instance/{id}/statistics** | FPS, current_framerate, latency, frames_processed, dropped_frames_count, resolution, source_framerate |
| **GET /v1/core/system/status** | CPU %, RAM (used/total MiB), load_average (1/5/15 phút), uptime |
| **MQTT subscribe** (topic từ config) | Mỗi event: thời điểm nhận, payload; nếu payload có trường thời gian phát hiện → tính **detection_to_mqtt_ms** |

**Ảnh hưởng hiệu năng:** Đo thời gian detection → MQTT trong C++ chỉ thêm 1 lần đọc đồng hồ và chèn 2 trường vào JSON bằng thao tác chuỗi (không parse/serialize lại toàn bộ payload), nên **gần như không ảnh hưởng** đến FPS hay độ trễ chạy instance.

**Đảm bảo kết quả chính xác 100% (Detection → MQTT):**
- **Chính xác 100%:** Chỉ khi payload MQTT chứa trường **`detection_to_mqtt_ms`** do C++ broker gửi (đo tại server: từ lúc có kết quả crossline đến lúc gọi gửi MQTT). Báo cáo hiển thị "✓ Chính xác 100%" và cột "Nguồn" = "Server (chính xác 100%)".
- **Ước lượng:** Nếu payload không có `detection_to_mqtt_ms`, benchmark dùng `system_timestamp` hoặc các trường timestamp khác để tính độ trễ (server → client nhận, gồm mạng). Báo cáo ghi rõ "Ước lượng" và "Ước lượng (client)" trong bảng. Để có số liệu chính xác 100%, cần build và chạy bản C++ đã bổ sung đo thời gian trong broker (format_msg + broke_msg).

## Output

- **CSV:** `output/benchmark_metrics_YYYYMMDD_HHMMSS.csv` – từng dòng là một lần poll (timestamp, instance_id, fps, cpu, ram, latency, load, …).
- **Events JSON:** `output/benchmark_mqtt_events_YYYYMMDD_HHMMSS.json` – danh sách event MQTT (received_ts_ms, detection_to_mqtt_ms nếu có, payload).
- **HTML:** `output/benchmark_report_YYYYMMDD_HHMMSS.html` – báo cáo tổng hợp, dùng trình duyệt mở.

## Báo cáo HTML (cho người không chuyên dev)

- **Tổng quan:** FPS (trung bình, min, max), CPU %, RAM, latency, load.
- **Detection → MQTT:** trung bình, trung vị, min, max, P95 (ms) và số sự kiện.
- **Biểu đồ theo thời gian:** FPS, CPU, Latency.
- **Bảng chi tiết từng event:** số thứ tự, thời điểm nhận, độ trễ detection → MQTT (nếu có).

Có thể mở file HTML trực tiếp trong trình duyệt (file://...) hoặc copy vào server và xem qua HTTP.

## Tham khảo thêm

- **Monitor dashboard:** thư mục `monitor/` dùng CSV cùng format để vẽ dashboard (Streamlit). Có thể dùng CSV do benchmark tạo ra với dashboard đó.
- **API:** Start instance `POST /v1/core/instance/{id}/start`, thống kê `GET /v1/core/instance/{id}/statistics`, hệ thống `GET /v1/core/system/status`.
