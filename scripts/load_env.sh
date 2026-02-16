#!/bin/bash
# ============================================
# Load Environment Variables from .env file
# ============================================
#
# This script loads environment variables from .env file
# and runs the edge_ai_api server.
#
# Usage:
#   ./scripts/load_env.sh                    # Load .env and run server
#   ./scripts/load_env.sh /path/to/.env      # Use custom .env file
#   ./scripts/load_env.sh --load-only        # Only load env, don't run server
#   source ./scripts/load_env.sh --load-only # Load env into current shell
#
# ============================================

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Parse arguments
LOAD_ONLY=false
ENV_FILE=""

for arg in "$@"; do
    case "$arg" in
        --load-only|--load)
            LOAD_ONLY=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS] [.env_file]"
            echo ""
            echo "Options:"
            echo "  --load-only, --load    Only load environment variables, don't run server"
            echo "  --help, -h             Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                          # Load .env and run server"
            echo "  $0 /path/to/.env            # Use custom .env file"
            echo "  $0 --load-only              # Only load env variables"
            echo "  source $0 --load-only        # Load env into current shell"
            exit 0
            ;;
        *)
            if [ -z "$ENV_FILE" ] && [ ! "$arg" = "--load-only" ] && [ ! "$arg" = "--load" ]; then
                ENV_FILE="$arg"
            fi
            ;;
    esac
done

# Default .env file location if not specified
if [ -z "$ENV_FILE" ]; then
    ENV_FILE="$PROJECT_ROOT/.env"
fi

# Check if .env file exists
if [ ! -f "$ENV_FILE" ]; then
    echo "Warning: .env file not found at $ENV_FILE"
    echo "Creating from .env.example..."

    if [ -f "$PROJECT_ROOT/.env.example" ]; then
        cp "$PROJECT_ROOT/.env.example" "$ENV_FILE"
        echo "Created .env file from .env.example"
        echo "Please edit $ENV_FILE and set your values"
        exit 1
    else
        echo "Error: .env.example not found. Cannot create .env file."
        exit 1
    fi
fi

# Load environment variables
echo "=========================================="
echo "Loading environment variables from $ENV_FILE"
echo "=========================================="
set -a  # Automatically export all variables
source "$ENV_FILE"
set +a  # Stop automatically exporting

# Config file path - Auto-detection will find config.json in order:
# 1. ./config.json (current directory - development)
# 2. /opt/edge_ai_api/config/config.json (production)
# 3. /etc/edge_ai_api/config.json (system)
# Can be overridden by CONFIG_FILE in .env file
if [ -n "$CONFIG_FILE" ]; then
    echo "[Config] Using config.json from CONFIG_FILE: $CONFIG_FILE"
else
    echo "[Config] Using auto-detection (will find config.json automatically)"
fi

# Change to project root
cd "$PROJECT_ROOT" || exit 1

# Display loaded configuration
echo ""
echo "Configuration Summary:"
echo "----------------------"

# Server Configuration
echo "Server:"
echo "  API_HOST=${API_HOST:-0.0.0.0} (default: 0.0.0.0)"
echo "  API_PORT=${API_PORT:-8080} (default: 8080)"
echo "  THREAD_NUM=${THREAD_NUM:-0} (default: 0 = auto)"

# Logging Configuration
echo "Logging:"
echo "  LOG_LEVEL=${LOG_LEVEL:-INFO} (default: INFO)"
echo "  LOG_DIR=${LOG_DIR:-./logs} (default: ./logs)"
echo "  LOG_RETENTION_DAYS=${LOG_RETENTION_DAYS:-30} (default: 30)"
echo "  LOG_MAX_DISK_USAGE_PERCENT=${LOG_MAX_DISK_USAGE_PERCENT:-85}% (default: 85%)"
echo "  LOG_CLEANUP_INTERVAL_HOURS=${LOG_CLEANUP_INTERVAL_HOURS:-24} (default: 24)"

# Instance Management
echo "Instance Management:"
echo "  INSTANCES_DIR=${INSTANCES_DIR:-/opt/edge_ai_api/instances} (default: /opt/edge_ai_api/instances)"
echo "  MODELS_DIR=${MODELS_DIR:-./models} (default: ./models)"

# Watchdog & Health Monitor
echo "Monitoring:"
echo "  WATCHDOG_CHECK_INTERVAL_MS=${WATCHDOG_CHECK_INTERVAL_MS:-5000} (default: 5000)"
echo "  WATCHDOG_TIMEOUT_MS=${WATCHDOG_TIMEOUT_MS:-30000} (default: 30000)"
echo "  HEALTH_MONITOR_INTERVAL_MS=${HEALTH_MONITOR_INTERVAL_MS:-1000} (default: 1000)"

# RTSP Configuration (only show if set)
if [ -n "$GST_RTSP_PROTOCOLS" ] || [ -n "$RTSP_TRANSPORT" ]; then
    echo "RTSP:"
    [ -n "$GST_RTSP_PROTOCOLS" ] && echo "  GST_RTSP_PROTOCOLS=${GST_RTSP_PROTOCOLS}"
    [ -n "$RTSP_TRANSPORT" ] && echo "  RTSP_TRANSPORT=${RTSP_TRANSPORT}"
fi

# Performance Settings (only show if set)
if [ -n "$KEEPALIVE_REQUESTS" ] || [ -n "$KEEPALIVE_TIMEOUT" ] || [ -n "$ENABLE_REUSE_PORT" ]; then
    echo "Performance:"
    [ -n "$KEEPALIVE_REQUESTS" ] && echo "  KEEPALIVE_REQUESTS=${KEEPALIVE_REQUESTS}"
    [ -n "$KEEPALIVE_TIMEOUT" ] && echo "  KEEPALIVE_TIMEOUT=${KEEPALIVE_TIMEOUT}"
    [ -n "$ENABLE_REUSE_PORT" ] && echo "  ENABLE_REUSE_PORT=${ENABLE_REUSE_PORT}"
fi

echo ""
echo "=========================================="
echo ""

# Validate critical variables
echo "Validating configuration..."
VALIDATION_ERROR=0

# Validate API_PORT
if [ -n "$API_PORT" ]; then
    if ! [[ "$API_PORT" =~ ^[0-9]+$ ]] || [ "$API_PORT" -lt 1 ] || [ "$API_PORT" -gt 65535 ]; then
        echo "Error: API_PORT must be a number between 1 and 65535"
        VALIDATION_ERROR=1
    fi
fi

# Validate LOG_LEVEL
if [ -n "$LOG_LEVEL" ]; then
    LOG_LEVEL_UPPER=$(echo "$LOG_LEVEL" | tr '[:lower:]' '[:upper:]')
    case "$LOG_LEVEL_UPPER" in
        TRACE|DEBUG|INFO|WARN|WARNING|ERROR|FATAL|NONE)
            ;;
        *)
            echo "Warning: LOG_LEVEL='$LOG_LEVEL' is not a valid value (TRACE/DEBUG/INFO/WARN/ERROR/FATAL/NONE)"
            ;;
    esac
fi

# Validate LOG_MAX_DISK_USAGE_PERCENT
if [ -n "$LOG_MAX_DISK_USAGE_PERCENT" ]; then
    if ! [[ "$LOG_MAX_DISK_USAGE_PERCENT" =~ ^[0-9]+$ ]] || [ "$LOG_MAX_DISK_USAGE_PERCENT" -lt 50 ] || [ "$LOG_MAX_DISK_USAGE_PERCENT" -gt 95 ]; then
        echo "Warning: LOG_MAX_DISK_USAGE_PERCENT should be between 50 and 95"
    fi
fi

if [ $VALIDATION_ERROR -eq 1 ]; then
    echo ""
    echo "Please fix the errors above and try again."
    exit 1
fi

echo "Configuration validated successfully!"
echo ""

# If --load-only flag is set, skip build checks and exit
if [ "$LOAD_ONLY" = true ]; then
    echo "=========================================="
    echo "Environment variables loaded successfully!"
    echo "=========================================="
    echo ""
    echo "To run the server manually:"
    echo "  cd $PROJECT_ROOT"
    if [ -f "build/bin/edge_ai_api" ]; then
        echo "  ./build/bin/edge_ai_api"
    elif [ -f "build/edge_ai_api" ]; then
        echo "  ./build/edge_ai_api"
    else
        echo "  ./build/edge_ai_api  # (after building)"
    fi
    echo ""
    echo "Note: Use 'source $0 --load-only' to load variables into current shell session."
    return 0
fi

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "Error: build directory not found. Please build the project first."
    exit 1
fi

# Check if executable exists (try both locations)
EXECUTABLE=""
if [ -f "build/bin/edge_ai_api" ]; then
    EXECUTABLE="build/bin/edge_ai_api"
elif [ -f "build/edge_ai_api" ]; then
    EXECUTABLE="build/edge_ai_api"
else
    echo "Error: edge_ai_api executable not found."
    echo "Searched in: build/bin/edge_ai_api and build/edge_ai_api"
    echo "Please build the project first."
    exit 1
fi

# Validate critical variables
echo "Validating configuration..."
VALIDATION_ERROR=0

# Validate API_PORT
if [ -n "$API_PORT" ]; then
    if ! [[ "$API_PORT" =~ ^[0-9]+$ ]] || [ "$API_PORT" -lt 1 ] || [ "$API_PORT" -gt 65535 ]; then
        echo "Error: API_PORT must be a number between 1 and 65535"
        VALIDATION_ERROR=1
    fi
fi

# Validate LOG_LEVEL
if [ -n "$LOG_LEVEL" ]; then
    LOG_LEVEL_UPPER=$(echo "$LOG_LEVEL" | tr '[:lower:]' '[:upper:]')
    case "$LOG_LEVEL_UPPER" in
        TRACE|DEBUG|INFO|WARN|WARNING|ERROR|FATAL|NONE)
            ;;
        *)
            echo "Warning: LOG_LEVEL='$LOG_LEVEL' is not a valid value (TRACE/DEBUG/INFO/WARN/ERROR/FATAL/NONE)"
            ;;
    esac
fi

# Validate LOG_MAX_DISK_USAGE_PERCENT
if [ -n "$LOG_MAX_DISK_USAGE_PERCENT" ]; then
    if ! [[ "$LOG_MAX_DISK_USAGE_PERCENT" =~ ^[0-9]+$ ]] || [ "$LOG_MAX_DISK_USAGE_PERCENT" -lt 50 ] || [ "$LOG_MAX_DISK_USAGE_PERCENT" -gt 95 ]; then
        echo "Warning: LOG_MAX_DISK_USAGE_PERCENT should be between 50 and 95"
    fi
fi

if [ $VALIDATION_ERROR -eq 1 ]; then
    echo ""
    echo "Please fix the errors above and try again."
    exit 1
fi

echo "Configuration validated successfully!"
echo ""
echo "=========================================="
echo "Starting Edge AI API Server..."
echo "=========================================="
echo "Executable: $EXECUTABLE"
echo "Environment file: $ENV_FILE"
echo ""

# Run the server
cd "$(dirname "$EXECUTABLE")" || exit 1
./$(basename "$EXECUTABLE")
