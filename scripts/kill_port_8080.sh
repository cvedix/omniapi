#!/bin/bash
# Script to kill processes using port 8080

echo "=== Checking processes using port 8080 ==="

# Find PIDs using port 8080
PIDS=$(sudo lsof -ti :8080 2>/dev/null)

if [ -z "$PIDS" ]; then
    echo "No processes found using port 8080"
    exit 0
fi

echo "Found processes using port 8080:"
for PID in $PIDS; do
    ps -fp $PID 2>/dev/null || echo "  PID $PID (process may have exited)"
done

echo ""
echo "=== Attempting to kill processes ==="

# Try graceful shutdown first
for PID in $PIDS; do
    if ps -p $PID > /dev/null 2>&1; then
        echo "Sending SIGTERM to PID $PID..."
        sudo kill -TERM $PID 2>/dev/null
    fi
done

# Wait a bit
sleep 2

# Check if any processes are still running
REMAINING=""
for PID in $PIDS; do
    if ps -p $PID > /dev/null 2>&1; then
        REMAINING="$REMAINING $PID"
    fi
done

if [ -n "$REMAINING" ]; then
    echo "Some processes still running, sending SIGKILL..."
    for PID in $REMAINING; do
        echo "Sending SIGKILL to PID $PID..."
        sudo kill -9 $PID 2>/dev/null
    fi
    sleep 1
fi

# Final check
echo ""
echo "=== Final check ==="
REMAINING_PIDS=$(sudo lsof -ti :8080 2>/dev/null)
if [ -z "$REMAINING_PIDS" ]; then
    echo "✅ Port 8080 is now free"
    exit 0
else
    echo "⚠️  Some processes may still be using port 8080:"
    for PID in $REMAINING_PIDS; do
        ps -fp $PID 2>/dev/null || echo "  PID $PID"
    done
    exit 1
fi

