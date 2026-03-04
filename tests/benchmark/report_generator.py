#!/usr/bin/env python3
"""
Generate a human-readable HTML benchmark report from metrics CSV and MQTT events JSON.
Suitable for non-developers: summary cards, charts, event table, detection-to-MQTT stats.
"""
from __future__ import annotations

import csv
import json
import statistics
from pathlib import Path
from datetime import datetime
from typing import List, Optional


def _num(s, default=None):
    try:
        return float(s) if s else default
    except (TypeError, ValueError):
        return default


def _read_csv(path: Path) -> List[dict]:
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            rows.append(row)
    return rows


def _read_events(path: Path) -> List[dict]:
    if not path.exists():
        return []
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("events", data) if isinstance(data, dict) else data


def _safe_mean(values: List[float]) -> Optional[float]:
    usable = [v for v in values if v is not None]
    return statistics.mean(usable) if usable else None


def _safe_median(values: List[float]) -> Optional[float]:
    usable = [v for v in values if v is not None]
    return statistics.median(usable) if usable else None


def _percentile(sorted_values: List[float], p: float) -> Optional[float]:
    if not sorted_values:
        return None
    k = (len(sorted_values) - 1) * p / 100
    f = int(k)
    c = f + 1 if f + 1 < len(sorted_values) else f
    return sorted_values[f] + (k - f) * (sorted_values[c] - sorted_values[f])


def generate_report(
    csv_path: Path,
    events_path: Path,
    html_path: Path,
    instance_name: str = "",
    instance_id: str = "",
    duration_sec: int = 0,
    system_info: Optional[dict] = None,
) -> None:
    rows = _read_csv(csv_path) if csv_path.exists() else []
    events = _read_events(events_path)

    # Metrics from CSV
    fps_list = []
    cpu_list = []
    ram_used_list = []
    ram_total_list = []
    latency_list = []
    load_1_list = []
    load_5_list = []
    load_15_list = []
    for r in rows:
        v = _num(r.get("current_framerate") or r.get("fps"))
        if v is not None:
            fps_list.append(v)
        v = _num(r.get("cpu_usage_percent"))
        if v is not None:
            cpu_list.append(v)
        v = _num(r.get("ram_used_mb"))
        if v is not None:
            ram_used_list.append(v)
        v = _num(r.get("ram_total_mb"))
        if v is not None:
            ram_total_list.append(v)
        v = _num(r.get("latency"))
        if v is not None:
            latency_list.append(v)
        v = _num(r.get("load_1min"))
        if v is not None:
            load_1_list.append(v)
        v = _num(r.get("load_5min"))
        if v is not None:
            load_5_list.append(v)
        v = _num(r.get("load_15min"))
        if v is not None:
            load_15_list.append(v)

    fps_avg = _safe_mean(fps_list)
    fps_min = min(fps_list) if fps_list else None
    fps_max = max(fps_list) if fps_list else None
    cpu_avg = _safe_mean(cpu_list)
    ram_avg = _safe_mean(ram_used_list)
    ram_total_sample = ram_total_list[0] if ram_total_list else None
    latency_avg = _safe_mean(latency_list)
    load_1_avg = _safe_mean(load_1_list)
    load_5_avg = _safe_mean(load_5_list)
    load_15_avg = _safe_mean(load_15_list)

    # Detection-to-MQTT: ưu tiên số liệu đo tại server (chính xác 100%), fallback ước lượng client
    d2m_list_server = []
    d2m_list_client = []
    for e in events:
        if "detection_to_mqtt_ms" not in e or e["detection_to_mqtt_ms"] is None:
            continue
        val = float(e["detection_to_mqtt_ms"])
        if e.get("detection_to_mqtt_source") == "server":
            d2m_list_server.append(val)
        else:
            d2m_list_client.append(val)
    # Dùng số liệu server làm chính (chính xác 100%); nếu không có thì dùng client và ghi rõ là ước lượng
    d2m_list = d2m_list_server if d2m_list_server else d2m_list_client
    d2m_sorted = sorted(d2m_list) if d2m_list else []
    d2m_mean = _safe_mean(d2m_list)
    d2m_median = _safe_median(d2m_list)
    d2m_min = min(d2m_list) if d2m_list else None
    d2m_max = max(d2m_list) if d2m_list else None
    d2m_p95 = _percentile(d2m_sorted, 95) if d2m_sorted else None
    d2m_is_server = bool(d2m_list_server)

    # GPU / hardware summary from system_info
    gpu_html = ""
    if system_info:
        gpus = system_info.get("gpu") or []
        if isinstance(gpus, list) and gpus:
            gpu_html = "<p><strong>GPU:</strong> " + "; ".join(
                (g.get("vendor") or "") + " " + (g.get("model") or "") for g in gpus
            ) + "</p>"
        cpu_info = system_info.get("cpu") or {}
        if cpu_info:
            cores = cpu_info.get("logical_cores") or cpu_info.get("physical_cores")
            if cores is not None:
                gpu_html += f"<p><strong>CPU cores:</strong> {cores}</p>"

    # Build time-series data for simple inline chart (comma-separated for JS)
    ts_labels = [r.get("timestamp", "") for r in rows[:120]]  # limit points
    ts_fps = [str(_num(r.get("current_framerate") or r.get("fps")) or "0") for r in rows[:120]]
    ts_cpu = [str(_num(r.get("cpu_usage_percent")) or "0") for r in rows[:120]]
    ts_latency = [str(_num(r.get("latency")) or "0") for r in rows[:120]]

    title = "Báo cáo Benchmark - Edge AI Instance"
    generated = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    html = f"""<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <style>
    * {{ box-sizing: border-box; }}
    body {{ font-family: 'Segoe UI', system-ui, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; color: #222; }}
    .container {{ max-width: 1000px; margin: 0 auto; background: #fff; padding: 24px; border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); }}
    h1 {{ margin: 0 0 8px 0; font-size: 1.5rem; color: #1a1a1a; }}
    .meta {{ color: #666; font-size: 0.9rem; margin-bottom: 24px; }}
    .cards {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 12px; margin-bottom: 24px; }}
    .card {{ background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border-radius: 8px; padding: 16px; text-align: center; border: 1px solid #dee2e6; }}
    .card .value {{ font-size: 1.5rem; font-weight: 700; color: #0d6efd; }}
    .card .label {{ font-size: 0.75rem; color: #666; margin-top: 4px; }}
    .section {{ margin-bottom: 28px; }}
    .section h2 {{ font-size: 1.1rem; margin: 0 0 12px 0; color: #333; border-bottom: 2px solid #0d6efd; padding-bottom: 6px; }}
    table {{ width: 100%; border-collapse: collapse; font-size: 0.9rem; }}
    th, td {{ padding: 10px 12px; text-align: left; border-bottom: 1px solid #eee; }}
    th {{ background: #f8f9fa; font-weight: 600; color: #495057; }}
    tr:hover {{ background: #f8f9fa; }}
    .chart {{ height: 180px; margin: 16px 0; background: #f8f9fa; border-radius: 8px; padding: 12px; }}
    .chart-title {{ font-size: 0.85rem; color: #666; margin-bottom: 8px; }}
    .foot {{ margin-top: 24px; font-size: 0.8rem; color: #888; }}
    .highlight {{ background: #e7f1ff; padding: 12px; border-radius: 8px; margin: 12px 0; }}
  </style>
</head>
<body>
  <div class="container">
    <h1>{title}</h1>
    <div class="meta">
      Instance: <strong>{instance_name or "N/A"}</strong> | ID: <code>{instance_id or "N/A"}</code><br>
      Thời gian chạy benchmark: <strong>{duration_sec}</strong> giây | Số mẫu metrics: <strong>{len(rows)}</strong><br>
      Báo cáo tạo lúc: {generated}
      {f'<div>{gpu_html}</div>' if gpu_html else ''}
    </div>

    <div class="section">
      <h2>📊 Tổng quan hiệu năng</h2>
      <div class="cards">
        <div class="card"><div class="value">{f"{fps_avg:.2f}" if fps_avg is not None else "—"}</div><div class="label">FPS (trung bình)</div></div>
        <div class="card"><div class="value">{f"{fps_min:.2f}" if fps_min is not None else "—"} / {f"{fps_max:.2f}" if fps_max is not None else "—"}</div><div class="label">FPS (min / max)</div></div>
        <div class="card"><div class="value">{f"{cpu_avg:.1f}%" if cpu_avg is not None else "—"}</div><div class="label">CPU (%)</div></div>
        <div class="card"><div class="value">{f"{ram_avg:.0f}" if ram_avg is not None else "—"} MB</div><div class="label">RAM đã dùng (MB)</div></div>
        <div class="card"><div class="value">{f"{latency_avg:.0f}" if latency_avg is not None else "—"} ms</div><div class="label">Độ trễ (latency)</div></div>
        <div class="card"><div class="value">{f"{load_1_avg:.2f}" if load_1_avg is not None else "—"}</div><div class="label">Load 1 phút</div></div>
      </div>
    </div>

    <div class="section">
      <h2>⏱ Thời gian từ phát hiện đến gửi MQTT</h2>
      <p>Thời gian từ lúc hệ thống có kết quả crossline đến lúc gửi MQTT. Càng thấp càng tốt.</p>
      {"<p class=\"highlight\"><strong>✓ Chính xác 100%</strong>: Số liệu đo tại server (payload chứa <code>detection_to_mqtt_ms</code>). Số event dùng: " + str(len(d2m_list_server)) + ".</p>" if d2m_is_server else ""}
      {"<p class=\"highlight\">⚠ <strong>Ước lượng</strong>: Payload không có <code>detection_to_mqtt_ms</code>, dùng timestamp trong payload (bao gồm cả độ trễ mạng). Để có số liệu chính xác 100%, dùng bản build C++ đã bổ sung đo thời gian trong broker.</p>" if d2m_list and not d2m_is_server else ""}
      <div class="cards">
        <div class="card"><div class="value">{f"{d2m_mean:.0f}" if d2m_mean is not None else "—"} ms</div><div class="label">Trung bình</div></div>
        <div class="card"><div class="value">{f"{d2m_median:.0f}" if d2m_median is not None else "—"} ms</div><div class="label">Trung vị</div></div>
        <div class="card"><div class="value">{f"{d2m_min:.0f}" if d2m_min is not None else "—"} ms</div><div class="label">Min</div></div>
        <div class="card"><div class="value">{f"{d2m_max:.0f}" if d2m_max is not None else "—"} ms</div><div class="label">Max</div></div>
        <div class="card"><div class="value">{f"{d2m_p95:.0f}" if d2m_p95 is not None else "—"} ms</div><div class="label">P95</div></div>
        <div class="card"><div class="value">{len(d2m_list)}</div><div class="label">Số event (dùng cho TB)</div></div>
      </div>
      {"<p class=\"highlight\">Chưa có số liệu: payload MQTT không chứa timestamp. Chạy với bản build C++ có <code>detection_ts_ms</code>/<code>detection_to_mqtt_ms</code> để có kết quả chính xác 100%.</p>" if not d2m_list and events else ""}
    </div>

    <div class="section">
      <h2>📈 Diễn biến theo thời gian</h2>
      <div class="chart">
        <div class="chart-title">FPS</div>
        <canvas id="chartFps" width="900" height="160"></canvas>
      </div>
      <div class="chart">
        <div class="chart-title">CPU (%)</div>
        <canvas id="chartCpu" width="900" height="160"></canvas>
      </div>
      <div class="chart">
        <div class="chart-title">Latency (ms)</div>
        <canvas id="chartLatency" width="900" height="160"></canvas>
      </div>
    </div>

    <div class="section">
      <h2>📋 Chi tiết từng sự kiện MQTT (Detection → MQTT)</h2>
      <table>
        <thead><tr><th>#</th><th>Thời điểm nhận (ms)</th><th>Độ trễ (ms)</th><th>Nguồn</th></tr></thead>
        <tbody>
"""
    for i, e in enumerate(events[:200], 1):  # limit 200 rows
        recv = e.get("received_ts_ms", "")
        d2m = e.get("detection_to_mqtt_ms")
        d2m_str = f"{d2m:.0f}" if d2m is not None else "—"
        src = e.get("detection_to_mqtt_source", "")
        src_label = "Server (chính xác 100%)" if src == "server" else ("Ước lượng (client)" if src == "client_estimated" else "—")
        html += f"          <tr><td>{i}</td><td>{recv}</td><td>{d2m_str}</td><td>{src_label}</td></tr>\n"
    if len(events) > 200:
        html += f"          <tr><td colspan=\"4\">… và {len(events) - 200} sự kiện khác (xem file JSON).</td></tr>\n"
    html += """        </tbody>
      </table>
    </div>

    <div class="foot">
      Dữ liệu: file CSV metrics và file JSON sự kiện MQTT. Có thể mở file CSV trong Excel hoặc dùng dashboard trong thư mục monitor/ để xem thêm.
    </div>
  </div>

  <script>
    function drawLineChart(canvasId, values, color) {
      var c = document.getElementById(canvasId);
      if (!c) return;
      var ctx = c.getContext('2d');
      var w = c.width, h = c.height;
      var arr = values.map(function(v) { return parseFloat(v) || 0; });
      if (arr.length === 0) return;
      var max = Math.max.apply(null, arr);
      var min = Math.min.apply(null, arr);
      if (max === min) max = min + 1;
      ctx.clearRect(0, 0, w, h);
      ctx.strokeStyle = color || '#0d6efd';
      ctx.lineWidth = 2;
      ctx.beginPath();
      var step = (w - 20) / (arr.length - 1) || w;
      for (var i = 0; i < arr.length; i++) {
        var x = 10 + (arr.length > 1 ? (i / (arr.length - 1)) * (w - 20) : 0);
        var y = h - 20 - ((arr[i] - min) / (max - min)) * (h - 30);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
    var tsFps = [""" + ",".join(ts_fps) + """];
    var tsCpu = [""" + ",".join(ts_cpu) + """];
    var tsLatency = [""" + ",".join(ts_latency) + """];
    drawLineChart('chartFps', tsFps, '#0d6efd');
    drawLineChart('chartCpu', tsCpu, '#198754');
    drawLineChart('chartLatency', tsLatency, '#fd7e14');
  </script>
</body>
</html>
"""
    html_path.parent.mkdir(parents=True, exist_ok=True)
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(html)


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 4:
        print("Usage: report_generator.py <csv_path> <events_json_path> <html_path> [instance_name] [instance_id] [duration_sec]")
        sys.exit(1)
    csv_path = Path(sys.argv[1])
    events_path = Path(sys.argv[2])
    html_path = Path(sys.argv[3])
    instance_name = sys.argv[4] if len(sys.argv) > 4 else ""
    instance_id = sys.argv[5] if len(sys.argv) > 5 else ""
    duration_sec = int(sys.argv[6]) if len(sys.argv) > 6 else 0
    generate_report(csv_path, events_path, html_path, instance_name, instance_id, duration_sec)
