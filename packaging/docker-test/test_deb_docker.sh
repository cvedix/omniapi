#!/bin/bash
# Script để test cài đặt .deb package trong Docker
# Chạy từ thư mục packaging/docker-test/

set -e

# Lấy đường dẫn script và project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEB_FILE="edge-ai-api-with-sdk-2026.0.1.21-amd64.deb"
DEB_PATH="$PROJECT_ROOT/${DEB_FILE}"

echo "=== Test Debian Package trong Docker ==="
echo ""

# Kiểm tra file .deb có tồn tại không
if [ ! -f "$DEB_PATH" ]; then
    echo "❌ Lỗi: Không tìm thấy file $DEB_FILE"
    echo "   Đường dẫn: $DEB_PATH"
    exit 1
fi

echo "✓ Tìm thấy file: $DEB_FILE"
echo ""

# Kiểm tra Docker có cài đặt không
if ! command -v docker &> /dev/null; then
    echo "❌ Docker chưa được cài đặt!"
    echo "   Cài đặt với: sudo apt-get install docker.io docker-compose-plugin"
    exit 1
fi

# Kiểm tra Docker Compose (V2 plugin hoặc standalone)
if docker compose version &> /dev/null; then
    DOCKER_COMPOSE_CMD="docker compose"
elif command -v docker-compose &> /dev/null; then
    DOCKER_COMPOSE_CMD="docker-compose"
else
    echo "❌ Docker Compose chưa được cài đặt!"
    echo "   Cài đặt với: sudo apt-get install docker-compose-plugin"
    exit 1
fi

echo "✓ Docker đã được cài đặt"
echo "✓ Docker Compose đã được cài đặt"
echo ""

# Menu lựa chọn
echo "Chọn phương thức test:"
echo "  1) Build và chạy với docker compose (khuyến nghị)"
echo "  2) Build và chạy với docker run"
echo "  3) Chỉ build image (không chạy)"
echo ""
read -p "Lựa chọn (1-3): " choice

case $choice in
    1)
        echo ""
        echo "=== Build và chạy với docker compose ==="
        cd "$SCRIPT_DIR"
        $DOCKER_COMPOSE_CMD -f docker-compose.test.yml build
        echo ""
        echo "✓ Build thành công!"
        echo ""
        echo "Để chạy container:"
        echo "  cd $SCRIPT_DIR"
        echo "  $DOCKER_COMPOSE_CMD -f docker-compose.test.yml up -d"
        echo ""
        echo "Để vào container:"
        echo "  $DOCKER_COMPOSE_CMD -f docker-compose.test.yml exec edge-ai-api-test bash"
        echo ""
        echo "Để xem logs:"
        echo "  $DOCKER_COMPOSE_CMD -f docker-compose.test.yml logs -f"
        echo ""
        echo "Để dừng:"
        echo "  $DOCKER_COMPOSE_CMD -f docker-compose.test.yml down"
        ;;
    2)
        echo ""
        echo "=== Build image ==="
        cd "$PROJECT_ROOT"
        docker build -f packaging/docker-test/Dockerfile.test -t edge-ai-api-test:latest .
        echo ""
        echo "✓ Build thành công!"
        echo ""
        echo "Để chạy container:"
        echo "  docker run -it --privileged --name edge-ai-api-test \\"
        echo "    -v /sys/fs/cgroup:/sys/fs/cgroup:ro \\"
        echo "    -p 8080:8080 \\"
        echo "    edge-ai-api-test:latest"
        echo ""
        echo "Hoặc chạy ở background:"
        echo "  docker run -d --privileged --name edge-ai-api-test \\"
        echo "    -v /sys/fs/cgroup:/sys/fs/cgroup:ro \\"
        echo "    -p 8080:8080 \\"
        echo "    edge-ai-api-test:latest"
        echo ""
        echo "Để vào container:"
        echo "  docker exec -it edge-ai-api-test bash"
        ;;
    3)
        echo ""
        echo "=== Build image ==="
        cd "$PROJECT_ROOT"
        docker build -f packaging/docker-test/Dockerfile.test -t edge-ai-api-test:latest .
        echo ""
        echo "✓ Build thành công!"
        echo ""
        echo "Image đã được tạo: edge-ai-api-test:latest"
        echo "Để chạy sau này, sử dụng lệnh ở option 2"
        ;;
    *)
        echo "❌ Lựa chọn không hợp lệ!"
        exit 1
        ;;
esac

echo ""
echo "=== Hướng dẫn test trong container ==="
echo ""
echo "1. Kiểm tra package đã được cài đặt:"
echo "   dpkg -l | grep edge-ai-api"
echo ""
echo "2. Kiểm tra executable:"
echo "   ls -la /usr/local/bin/edge_ai_api"
echo ""
echo "3. Kiểm tra service file:"
echo "   cat /etc/systemd/system/edgeos-api.service"
echo ""
echo "4. Test chạy trực tiếp (không dùng systemd):"
echo "   /usr/local/bin/edge_ai_api --help"
echo ""
echo "5. Test với systemd (trong container):"
echo "   systemctl daemon-reload"
echo "   systemctl start edge-ai-api"
echo "   systemctl status edge-ai-api"
echo ""
echo "6. Test API từ host:"
echo "   curl http://localhost:8080/v1/core/health"
echo "   curl http://localhost:8080/v1/core/version"
echo ""

