#!/bin/bash
# Clean all edgeos-worker processes and their socket files.
# Use after stopping omniapi (Ctrl+C) to avoid stale workers and "Worker not ready" / mutex timeout.
# Socket dir: EDGE_AI_SOCKET_DIR (default /opt/omniapi/run, fallback /tmp when permission denied).

set -e

SOCKET_DIR="${EDGE_AI_SOCKET_DIR:-/tmp}"
EXTRA_SOCKET_DIR="/opt/omniapi/run"

echo "=== Cleaning edgeos workers and sockets ==="

# 1) Kill all edgeos-worker processes (match by name or path ./bin/edgeos-worker)
if pgrep -f "edgeos-worker" >/dev/null 2>&1; then
  echo "Sending SIGTERM to edgeos-worker processes..."
  pkill -f "edgeos-worker" 2>/dev/null || true
  sleep 2
  if pgrep -f "edgeos-worker" >/dev/null 2>&1; then
    echo "Some workers still running, sending SIGKILL..."
    pkill -9 -f "edgeos-worker" 2>/dev/null || true
    sleep 1
  fi
  echo "Done killing edgeos-worker processes."
else
  echo "No edgeos-worker processes found."
fi

# 2) Remove socket files (both common locations)
removed=0
for dir in "$SOCKET_DIR" "$EXTRA_SOCKET_DIR"; do
  if [ -d "$dir" ]; then
    for f in "$dir"/edgeos_worker_*.sock; do
      if [ -e "$f" ]; then
        rm -f "$f" && echo "Removed socket: $f" && removed=$((removed+1))
      fi
    done
  fi
done

if [ "$removed" -eq 0 ]; then
  echo "No worker socket files found in $SOCKET_DIR or $EXTRA_SOCKET_DIR."
else
  echo "Removed $removed socket file(s)."
fi

echo "=== Clean done ==="
