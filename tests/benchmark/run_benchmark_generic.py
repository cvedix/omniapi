#!/usr/bin/env python3
"""
Benchmark runner cho bất kỳ instance Edge AI API.

- Instance có output MQTT (additionalParams.output.MQTT_BROKER_URL): thu thêm thời gian từ
  phát hiện đến gửi MQTT (detection-to-MQTT), đồng thời vẫn thu FPS, latency pipeline, tài nguyên.
- Instance không có MQTT: chỉ benchmark thời gian từ nhận frame → detect → đẩy output
  (FPS, latency, frames_processed từ API /statistics) và tài nguyên hệ thống (CPU, RAM, load).
- Tạo instance từ config (hoặc --reuse-existing), start, thu metrics, sinh báo cáo HTML.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

try:
    import requests
except ImportError:
    print("Install dependencies: pip install -r requirements.txt", file=sys.stderr)
    sys.exit(1)

try:
    import paho.mqtt.client as mqtt
    _have_paho = True
except ImportError:
    mqtt = None
    _have_paho = False

# Paths
BENCHMARK_DIR = Path(__file__).resolve().parent
CONFIG_DIR = BENCHMARK_DIR / "config"
DEFAULT_INSTANCE_CONFIG = CONFIG_DIR / "instance_benchmark.json"
OUTPUT_DIR = BENCHMARK_DIR / "output"

# API
DEFAULT_BASE_URL = os.environ.get("EDGE_AI_API_URL", "http://localhost:8080")

BENCHMARK_CONFIG = {
    "duration_sec": 120,
    "poll_interval_sec": 2.0,
    "wait_ready_timeout_sec": 120,
    "wait_ready_poll_sec": 2,
    "wait_running_timeout_sec": 60,
    "wait_running_poll_sec": 2,
    "api_request_timeout": 10,
    "api_create_timeout": 30,
    "api_start_timeout": 60,
    "api_list_timeout": 15,
    "mqtt_keepalive_sec": 60,
    "collector_join_timeout_multiplier": 2,
}

WAIT_READY_TIMEOUT_SEC = BENCHMARK_CONFIG["wait_ready_timeout_sec"]
WAIT_READY_POLL_SEC = BENCHMARK_CONFIG["wait_ready_poll_sec"]
WAIT_RUNNING_TIMEOUT_SEC = BENCHMARK_CONFIG["wait_running_timeout_sec"]
WAIT_RUNNING_POLL_SEC = BENCHMARK_CONFIG["wait_running_poll_sec"]
DEFAULT_POLL_INTERVAL_SEC = BENCHMARK_CONFIG["poll_interval_sec"]
DEFAULT_DURATION_SEC = BENCHMARK_CONFIG["duration_sec"]


def load_instance_config(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def find_instance_by_name(base_url: str, name: str) -> Optional[str]:
    try:
        r = requests.get(f"{base_url}/v1/core/instance", timeout=BENCHMARK_CONFIG["api_list_timeout"])
        if r.status_code != 200:
            return None
        data = r.json()
        instances = data if isinstance(data, list) else data.get("instances", data.get("items", []))
        for inst in instances:
            if inst.get("displayName") == name or inst.get("name") == name or inst.get("instanceId", "").startswith(name):
                return inst.get("instanceId")
    except Exception:
        pass
    return None


def create_instance(base_url: str, config: dict) -> Optional[str]:
    payload = {k: v for k, v in config.items() if k != "instanceId"}
    try:
        r = requests.post(
            f"{base_url}/v1/core/instance",
            json=payload,
            headers={"Content-Type": "application/json"},
            timeout=BENCHMARK_CONFIG["api_create_timeout"],
        )
        if r.status_code in (200, 201):
            return r.json().get("instanceId")
    except Exception:
        pass
    return None


def wait_ready(base_url: str, instance_id: str, timeout_sec: int, poll_sec: float) -> bool:
    url = f"{base_url}/v1/core/instance/{instance_id}"
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            resp = requests.get(url, timeout=BENCHMARK_CONFIG["api_request_timeout"])
            if resp.status_code != 200:
                time.sleep(poll_sec)
                continue
            data = resp.json()
            if not data.get("building", False) and data.get("status") == "ready":
                return True
            if data.get("status") == "error":
                return False
        except Exception:
            pass
        time.sleep(poll_sec)
    return False


def start_instance(base_url: str, instance_id: str) -> tuple[bool, bool]:
    url = f"{base_url}/v1/core/instance/{instance_id}/start"
    try:
        r = requests.post(url, headers={"Content-Type": "application/json"}, timeout=BENCHMARK_CONFIG["api_start_timeout"])
        if r.status_code in (200, 201, 202):
            need_poll = r.status_code == 202
            if r.status_code in (200, 201) and r.text:
                try:
                    data = r.json()
                    if data.get("status") == "starting":
                        need_poll = True
                except Exception:
                    pass
            return True, need_poll
    except Exception:
        pass
    return False, False


def wait_running(base_url: str, instance_id: str, timeout_sec: int, poll_sec: float) -> bool:
    url = f"{base_url}/v1/core/instance/{instance_id}"
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            resp = requests.get(url, timeout=BENCHMARK_CONFIG["api_request_timeout"])
            if resp.status_code == 200 and resp.json().get("running") is True:
                return True
        except Exception:
            pass
        time.sleep(poll_sec)
    return False


def stop_instance(base_url: str, instance_id: str) -> bool:
    url = f"{base_url}/v1/core/instance/{instance_id}/stop"
    try:
        r = requests.post(url, headers={"Content-Type": "application/json"}, timeout=BENCHMARK_CONFIG["api_request_timeout"])
        return r.status_code in (200, 202)
    except Exception:
        return False


def fetch_statistics(base_url: str, instance_id: str) -> Optional[dict]:
    url = f"{base_url}/v1/core/instance/{instance_id}/statistics"
    try:
        r = requests.get(url, timeout=BENCHMARK_CONFIG["api_request_timeout"])
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return None


def fetch_system_status(base_url: str) -> Optional[dict]:
    url = f"{base_url}/v1/core/system/status"
    try:
        r = requests.get(url, timeout=BENCHMARK_CONFIG["api_request_timeout"])
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return None


def fetch_system_info(base_url: str) -> Optional[dict]:
    url = f"{base_url}/v1/core/system/info"
    try:
        r = requests.get(url, timeout=BENCHMARK_CONFIG["api_request_timeout"])
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return None


def get_mqtt_config_from_params(additional_params: dict) -> Optional[tuple]:
    """Lấy broker, port, topic từ additionalParams.output (nếu có MQTT_BROKER_URL)."""
    out = additional_params.get("output") or {}
    if isinstance(out, str):
        try:
            out = json.loads(out)
        except Exception:
            return None
    broker = (out.get("MQTT_BROKER_URL") or "").strip()
    if not broker:
        return None
    port = 1883
    try:
        port = int(out.get("MQTT_PORT") or 1883)
    except Exception:
        pass
    topic = (out.get("MQTT_TOPIC") or "events").strip()
    user = (out.get("MQTT_USERNAME") or "").strip()
    password = (out.get("MQTT_PASSWORD") or "").strip()
    return broker, port, topic, user, password


mqtt_events: list[dict] = []
mqtt_events_lock = threading.Lock()


def _on_mqtt_connect(client, userdata, flags, rc):
    if rc == 0 and userdata:
        client.subscribe(userdata.get("topic", "events"), qos=1)


def _on_mqtt_message(client, userdata, msg):
    received_ts_ms = int(time.time() * 1000)
    payload = msg.payload.decode("utf-8", errors="replace")
    event = {"received_ts_ms": received_ts_ms, "payload_raw": payload}
    try:
        pl = json.loads(payload)
        event["payload"] = pl
        obj = pl[0] if isinstance(pl, list) and len(pl) > 0 else pl
        if not isinstance(obj, dict):
            obj = pl
        if "detection_to_mqtt_ms" in obj and obj["detection_to_mqtt_ms"] is not None:
            try:
                event["detection_to_mqtt_ms"] = int(obj["detection_to_mqtt_ms"])
                event["detection_ts_ms"] = obj.get("detection_ts_ms")
                event["detection_to_mqtt_source"] = "server"
            except (TypeError, ValueError):
                pass
        else:
            for key in (
                "detection_ts_ms", "system_timestamp", "timestamp",
                "event_time", "detection_time", "time", "ts",
            ):
                if key not in obj or obj[key] is None:
                    continue
                try:
                    t = obj[key]
                    if isinstance(t, str):
                        t = t.strip()
                        detection_ts_ms = int(t) if t.isdigit() else int(float(t) * 1000)
                    elif isinstance(t, (int, float)):
                        detection_ts_ms = int(t) if t > 1e12 else int(t * 1000)
                    else:
                        detection_ts_ms = int(float(t) * 1000)
                    event["detection_ts_ms"] = detection_ts_ms
                    event["detection_to_mqtt_ms"] = received_ts_ms - detection_ts_ms
                    event["detection_to_mqtt_source"] = "client_estimated"
                except (TypeError, ValueError):
                    pass
                else:
                    break
    except json.JSONDecodeError:
        pass
    with mqtt_events_lock:
        mqtt_events.append(event)


def run_mqtt_subscriber(broker: str, port: int, topic: str, username: str, password: str) -> threading.Thread:
    def run():
        client = mqtt.Client()
        if username or password:
            client.username_pw_set(username or None, password or None)
        client.user_data_set({"topic": topic})
        client.on_connect = _on_mqtt_connect
        client.on_message = _on_mqtt_message
        try:
            client.connect(broker, port, BENCHMARK_CONFIG["mqtt_keepalive_sec"])
            client.loop_forever()
        except Exception:
            pass

    t = threading.Thread(target=run, daemon=True)
    t.start()
    return t


def collect_metrics_loop(
    base_url: str,
    instance_id: str,
    display_name: str,
    solution_name: str,
    csv_path: Path,
    poll_interval_sec: float,
    stop_event: threading.Event,
    system_info_once: dict | None,
) -> None:
    columns = [
        "timestamp", "instance_id", "display_name", "solution", "running",
        "fps", "cpu_usage_percent", "ram_used_mb", "ram_total_mb",
        "current_framerate", "dropped_frames_count", "frames_processed", "latency",
        "resolution", "source_framerate",
        "load_1min", "load_5min", "load_15min", "uptime_seconds",
    ]
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
        w.writeheader()

    while not stop_event.is_set():
        row = {
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "instance_id": instance_id,
            "display_name": display_name,
            "solution": solution_name or "unknown",
            "running": "",
            "fps": "",
            "cpu_usage_percent": "",
            "ram_used_mb": "",
            "ram_total_mb": "",
            "current_framerate": "",
            "dropped_frames_count": "",
            "frames_processed": "",
            "latency": "",
            "resolution": "",
            "source_framerate": "",
            "load_1min": "",
            "load_5min": "",
            "load_15min": "",
            "uptime_seconds": "",
        }
        stats = fetch_statistics(base_url, instance_id)
        if stats:
            row["fps"] = stats.get("current_framerate", stats.get("fps", ""))
            row["current_framerate"] = stats.get("current_framerate", "")
            row["latency"] = stats.get("latency", "")
            row["dropped_frames_count"] = stats.get("dropped_frames_count", "")
            row["frames_processed"] = stats.get("frames_processed", "")
            row["resolution"] = stats.get("resolution", "")
            row["source_framerate"] = stats.get("source_framerate", "")
            row["running"] = "True"
        status = fetch_system_status(base_url)
        if status:
            cpu = status.get("cpu") or {}
            row["cpu_usage_percent"] = cpu.get("usage_percent", "")
            ram = status.get("ram") or {}
            row["ram_used_mb"] = ram.get("used_mib", "")
            row["ram_total_mb"] = ram.get("total_mib", "")
            la = status.get("load_average") or {}
            row["load_1min"] = la.get("1min", "")
            row["load_5min"] = la.get("5min", "")
            row["load_15min"] = la.get("15min", "")
            row["uptime_seconds"] = status.get("uptime_seconds", "")

        with open(csv_path, "a", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
            w.writerow(row)

        stop_event.wait(poll_interval_sec)


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark bất kỳ instance Edge AI API. Có MQTT: thu thêm detection→MQTT. Không MQTT: chỉ FPS, latency pipeline, tài nguyên."
    )
    parser.add_argument("--config", type=Path, default=DEFAULT_INSTANCE_CONFIG, help="Đường dẫn file config instance JSON (bất kỳ solution)")
    parser.add_argument("--base-url", default=os.environ.get("EDGE_AI_API_URL", "http://localhost:8080"), help="API base URL")
    parser.add_argument("--duration", type=int, default=DEFAULT_DURATION_SEC, help="Thời gian thu metrics (giây)")
    parser.add_argument("--poll-interval", type=float, default=DEFAULT_POLL_INTERVAL_SEC, help="Khoảng cách mỗi lần poll (giây)")
    parser.add_argument("--no-mqtt", action="store_true", help="Không subscribe MQTT dù config có MQTT (chỉ pipeline metrics)")
    parser.add_argument("--skip-report", action="store_true", help="Không tạo báo cáo HTML")
    parser.add_argument("--no-start", action="store_true", help="Instance đã chạy sẵn; không gọi start/stop")
    parser.add_argument("--reuse-existing", action="store_true", help="Tìm instance theo tên trong config, dùng nếu có")
    parser.add_argument("--report-only", action="store_true", help="Chỉ tạo lại báo cáo HTML từ CSV + events đã có")
    parser.add_argument("--csv", type=Path, help="CSV (dùng với --report-only)")
    parser.add_argument("--events", type=Path, help="Events JSON (dùng với --report-only)")
    parser.add_argument("--output-dir", type=Path, default=OUTPUT_DIR, help="Thư mục output")
    args = parser.parse_args()

    if args.report_only:
        args.output_dir = args.output_dir.resolve() if not args.output_dir.is_absolute() else args.output_dir
        args.output_dir.mkdir(parents=True, exist_ok=True)
        if args.csv is not None and args.events is not None:
            csv_path = args.csv.resolve() if not args.csv.is_absolute() else Path(args.csv)
            events_path = args.events.resolve() if not args.events.is_absolute() else Path(args.events)
        else:
            csv_files = sorted(args.output_dir.glob("benchmark_metrics_*.csv"), key=lambda p: p.stat().st_mtime, reverse=True)
            if not csv_files:
                print("Không tìm thấy file CSV trong", args.output_dir, file=sys.stderr)
                sys.exit(1)
            csv_path = csv_files[0]
            suffix = csv_path.stem.replace("benchmark_metrics_", "")
            events_path = args.output_dir / f"benchmark_mqtt_events_{suffix}.json"
            if not events_path.exists():
                events_path = args.output_dir / "benchmark_mqtt_events.json"
        html_path = csv_path.parent / (csv_path.stem.replace("benchmark_metrics_", "benchmark_report_") + ".html")
        instance_name = ""
        instance_id = ""
        duration_sec = 0
        poll_sec = BENCHMARK_CONFIG["poll_interval_sec"]
        if csv_path.exists():
            with open(csv_path, "r", encoding="utf-8") as f:
                rows = list(csv.DictReader(f))
                if rows:
                    instance_name = rows[0].get("display_name", "")
                    instance_id = rows[0].get("instance_id", "")
                    duration_sec = max(0, (len(rows) - 1) * int(poll_sec))
        from report_generator import generate_report
        generate_report(csv_path, events_path, html_path, instance_name, instance_id, duration_sec)
        print(f"Report: {html_path}")
        return 0

    base_url = args.base_url.rstrip("/")
    if not base_url.startswith("http"):
        base_url = "http://" + base_url

    config_path = args.config
    if not config_path.is_absolute():
        config_path = BENCHMARK_DIR / config_path
    if not config_path.exists():
        print(f"Config not found: {config_path}", file=sys.stderr)
        sys.exit(1)

    config = load_instance_config(config_path)
    instance_name = config.get("name") or config.get("displayName") or "instance"

    args.output_dir.mkdir(parents=True, exist_ok=True)
    ts_suffix = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = args.output_dir / f"benchmark_metrics_{ts_suffix}.csv"
    events_path = args.output_dir / f"benchmark_mqtt_events_{ts_suffix}.json"
    html_path = args.output_dir / f"benchmark_report_{ts_suffix}.html"

    if args.reuse_existing:
        instance_id = find_instance_by_name(base_url, instance_name)
        if instance_id:
            print(f"Using existing instance: {instance_id}")
        else:
            print(f"Instance '{instance_name}' not found, creating...")
            instance_id = create_instance(base_url, config)
            if not instance_id:
                print("Failed to create instance", file=sys.stderr)
                sys.exit(2)
            print(f"Created instanceId: {instance_id}")
            if not wait_ready(base_url, instance_id, WAIT_READY_TIMEOUT_SEC, WAIT_READY_POLL_SEC):
                print("Instance not ready after create", file=sys.stderr)
                sys.exit(3)
    else:
        print("Creating new instance...")
        instance_id = create_instance(base_url, config)
        if not instance_id:
            print("Failed to create instance", file=sys.stderr)
            sys.exit(2)
        print(f"Created instanceId: {instance_id}")
        if not wait_ready(base_url, instance_id, WAIT_READY_TIMEOUT_SEC, WAIT_READY_POLL_SEC):
            print("Instance not ready after create", file=sys.stderr)
            sys.exit(3)

    already_running = False
    try:
        r = requests.get(base_url + "/v1/core/instance/" + instance_id, timeout=BENCHMARK_CONFIG["api_request_timeout"])
        if r.status_code == 200:
            info = r.json()
            display_name = info.get("displayName") or instance_name
            solution_name = info.get("solutionName") or info.get("solutionId") or "unknown"
            already_running = info.get("running") is True
        else:
            display_name, solution_name = instance_name, "unknown"
    except Exception:
        display_name, solution_name = instance_name, "unknown"

    mqtt_thread = None
    mqtt_cfg = get_mqtt_config_from_params(config.get("additionalParams") or {})
    if not args.no_mqtt and _have_paho and mqtt_cfg:
        broker, port, topic, user, password = mqtt_cfg
        print(f"Subscribing to MQTT: {broker}:{port} topic={topic}")
        with mqtt_events_lock:
            mqtt_events.clear()
        mqtt_thread = run_mqtt_subscriber(broker, port, topic, user, password)
        time.sleep(1)
    else:
        if not mqtt_cfg:
            print("Instance không có MQTT output — chỉ thu pipeline metrics (FPS, latency từ API).")

    if args.no_start:
        print("--no-start: bỏ qua start/stop, chỉ thu metrics.")
    elif already_running:
        print("Instance already running. Collecting metrics...")
    else:
        print("Starting instance...")
        ok, need_poll = start_instance(base_url, instance_id)
        if not ok:
            print("Failed to start instance", file=sys.stderr)
            sys.exit(4)
        if need_poll:
            if wait_running(base_url, instance_id, WAIT_RUNNING_TIMEOUT_SEC, WAIT_RUNNING_POLL_SEC):
                print("Instance running. Collecting metrics...")
            else:
                print("Warning: API chưa báo running. Tiếp tục thu metrics...")
        else:
            print("Instance running. Collecting metrics...")

    system_info_once = fetch_system_info(base_url)

    stop_event = threading.Event()
    collector = threading.Thread(
        target=collect_metrics_loop,
        args=(base_url, instance_id, display_name, solution_name, csv_path, args.poll_interval, stop_event, system_info_once),
        daemon=True,
    )
    collector.start()

    try:
        time.sleep(args.duration)
    except KeyboardInterrupt:
        print("\nStopped by user")
    finally:
        stop_event.set()
        collector.join(timeout=args.poll_interval * BENCHMARK_CONFIG["collector_join_timeout_multiplier"])

    if not args.no_start:
        print("Stopping instance...")
        stop_instance(base_url, instance_id)
    else:
        print("--no-start: không gọi stop instance.")

    with mqtt_events_lock:
        events_copy = list(mqtt_events)
    with open(events_path, "w", encoding="utf-8") as f:
        json.dump({"events": events_copy, "count": len(events_copy), "instance_id": instance_id}, f, ensure_ascii=False, indent=2)

    print(f"Metrics CSV: {csv_path}")
    print(f"MQTT events: {events_path} ({len(events_copy)} events)")

    if not args.skip_report:
        from report_generator import generate_report
        generate_report(
            csv_path=csv_path,
            events_path=events_path,
            html_path=html_path,
            instance_name=display_name,
            instance_id=instance_id,
            duration_sec=args.duration,
            system_info=system_info_once,
        )
        print(f"Report: {html_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
