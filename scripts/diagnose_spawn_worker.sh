#!/bin/bash
# Diagnose "Failed to spawn worker for instance" - check executable, socket dir, limits.
# Run from project root or set EDGE_AI_WORKER_PATH and EDGE_AI_SOCKET_DIR as when running the API.

set -e

echo "=== Diagnose: Failed to spawn worker ==="
echo ""

# 1) Worker executable
WORKER_EXE="${EDGE_AI_WORKER_PATH:-edgeos-worker}"
if [[ "$WORKER_EXE" == /* ]]; then
  if [[ -x "$WORKER_EXE" ]]; then
    echo "✓ Worker executable (absolute): $WORKER_EXE"
  else
    echo "✗ Worker executable not found or not executable: $WORKER_EXE"
  fi
else
  # Same dir as omniapi (when run from build/bin)
  API_EXE=""
  for d in "./bin/omniapi" "./build/bin/omniapi" "/usr/local/bin/omniapi" "/usr/bin/omniapi" "/opt/omniapi/bin/omniapi"; do
    if [[ -x "$d" ]]; then API_EXE="$d"; break; fi
  done
  if [[ -n "$API_EXE" ]]; then
    EXE_DIR=$(dirname "$(realpath "$API_EXE" 2>/dev/null || echo "$API_EXE")")
    W_PATH="$EXE_DIR/$WORKER_EXE"
    if [[ -x "$W_PATH" ]]; then
      echo "✓ Worker executable (next to API): $W_PATH"
    else
      echo "✗ Worker not found next to API: $W_PATH"
    fi
  else
    echo "? omniapi not found in ./bin, ./build/bin, /usr/local/bin, /usr/bin, /opt/omniapi/bin"
  fi
  if command -v "$WORKER_EXE" &>/dev/null; then
    echo "✓ Worker in PATH: $(command -v "$WORKER_EXE")"
  else
    echo "? Worker not in PATH as: $WORKER_EXE"
  fi
fi
echo "  Fix: set EDGE_AI_WORKER_PATH to full path, e.g. $(pwd)/build/bin/edgeos-worker"
echo ""

# 2) Socket directory
SOCKET_DIR="${EDGE_AI_SOCKET_DIR:-/opt/omniapi/run}"
echo "Socket directory: $SOCKET_DIR"
if [[ -d "$SOCKET_DIR" ]]; then
  if [[ -w "$SOCKET_DIR" ]]; then
    echo "✓ Directory exists and is writable"
  else
    echo "✗ Directory exists but NOT writable (permission denied)"
    echo "  Fix: sudo chown \$USER $SOCKET_DIR  OR  export EDGE_AI_SOCKET_DIR=/tmp"
  fi
else
  echo "✗ Directory does not exist"
  echo "  Fix: sudo mkdir -p $SOCKET_DIR && sudo chown \$USER $SOCKET_DIR  OR  export EDGE_AI_SOCKET_DIR=/tmp"
fi
echo ""

# 3) Process limits
echo "Process limits:"
ulimit -u 2>/dev/null && echo "  max user processes: $(ulimit -u)" || echo "  (ulimit -u not available)"
echo "  Fix if too low: ulimit -u 4096"
echo ""

echo "=== When creating instance, check API server stderr for one of: ==="
echo "  - 'Worker executable not found' → set EDGE_AI_WORKER_PATH"
echo "  - 'Fork failed' → increase ulimit -u"
echo "  - 'Worker failed to become ready' → socket dir or worker crash (see worker log)"
echo ""
