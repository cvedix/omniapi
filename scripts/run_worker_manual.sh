#!/bin/bash
# Chạy worker thủ công để xem lỗi khi spawn (crash, thiếu lib, ...).
# Nếu worker chạy OK sẽ in "Ready and listening" rồi chờ; Ctrl+C để thoát.
# Nếu worker crash ngay, lỗi in ra màn hình.

set -e

ID="${1:-test-manual-$(date +%s)}"
SOCKET="${EDGE_AI_SOCKET_DIR:-/tmp}/edgeos_worker_${ID}.sock"
CONFIG="${2:-{}}"

WORKER="${EDGE_AI_WORKER_PATH:-}"
if [[ -z "$WORKER" ]]; then
  for d in "./build/bin/edgeos-worker" "./bin/edgeos-worker"; do
    if [[ -x "$d" ]]; then WORKER="$d"; break; fi
  done
fi
if [[ -z "$WORKER" ]]; then
  echo "Không tìm thấy edgeos-worker. Set EDGE_AI_WORKER_PATH hoặc chạy từ build/: ./scripts/run_worker_manual.sh"
  exit 1
fi

echo "Instance ID: $ID"
echo "Socket:      $SOCKET"
echo "Worker:      $WORKER"
echo "Config:      $CONFIG"
echo ""
echo "Chạy worker (Ctrl+C để thoát)..."
exec "$WORKER" --instance-id "$ID" --socket "$SOCKET" --config "$CONFIG"
