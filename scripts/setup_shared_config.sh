#!/bin/bash
# ============================================
# Setup Shared Config.json Script
# ============================================
# This script creates a symlink from production config.json
# to development config.json so both environments use the same file
#
# Usage:
#   sudo ./scripts/setup_shared_config.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV_CONFIG="$PROJECT_ROOT/config.json"
PROD_CONFIG="/opt/edge_ai_api/config/config.json"
BACKUP_DIR="/opt/edge_ai_api/config/backups"

echo "=========================================="
echo "Setting up shared config.json"
echo "=========================================="
echo "Development config: $DEV_CONFIG"
echo "Production config:  $PROD_CONFIG"
echo ""

# Check if development config exists
if [ ! -f "$DEV_CONFIG" ]; then
    echo "❌ Error: Development config not found: $DEV_CONFIG"
    exit 1
fi

# Create backup directory if it doesn't exist
if [ ! -d "$BACKUP_DIR" ]; then
    echo "Creating backup directory: $BACKUP_DIR"
    mkdir -p "$BACKUP_DIR"
fi

# Backup existing production config if it exists and is not a symlink
if [ -f "$PROD_CONFIG" ] && [ ! -L "$PROD_CONFIG" ]; then
    BACKUP_FILE="$BACKUP_DIR/config.json.backup.$(date +%Y%m%d_%H%M%S)"
    echo "Backing up existing production config to: $BACKUP_FILE"
    cp "$PROD_CONFIG" "$BACKUP_FILE"
    echo "✓ Backup created"
fi

# Remove existing file/symlink
if [ -e "$PROD_CONFIG" ]; then
    echo "Removing existing config: $PROD_CONFIG"
    rm "$PROD_CONFIG"
fi

# Create symlink
echo "Creating symlink: $PROD_CONFIG -> $DEV_CONFIG"
ln -s "$DEV_CONFIG" "$PROD_CONFIG"

# Verify symlink
if [ -L "$PROD_CONFIG" ]; then
    echo "✓ Symlink created successfully"
    echo ""
    echo "Verification:"
    ls -la "$PROD_CONFIG"
    echo ""
    echo "✅ Setup complete!"
    echo ""
    echo "Both development and production will now use:"
    echo "  $DEV_CONFIG"
    echo ""
    echo "Note: Any changes to this file will affect both environments."
else
    echo "❌ Error: Failed to create symlink"
    exit 1
fi
