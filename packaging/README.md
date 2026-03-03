# 📦 Packaging Directory

Thư mục này chứa các scripts và tài liệu liên quan đến việc build Debian package (.deb).

## 📁 Cấu Trúc

```
packaging/
├── scripts/           # Build scripts
│   ├── build_deb.sh   # Script chính để build .deb package
│   ├── create_vm.sh   # Script tạo VM VirtualBox (command line)
│   └── vm_manage.sh  # Script quản lý VM (start/stop/info)
├── docker-test/       # Docker test environment
│   ├── Dockerfile.test
│   ├── docker-compose.test.yml
│   └── test_deb_docker.sh
└── docs/              # Tài liệu hướng dẫn
    ├── BUILD_DEB.md   # Hướng dẫn build .deb package
    └── VM_TEST_GUIDE.md  # Hướng dẫn test trong VirtualBox VM
```

## 🚀 Quick Start

### Build Package

Xem [docs/BUILD_DEB.md](docs/BUILD_DEB.md) để biết chi tiết về cách build .deb package.

```bash
# Build package
./build_deb.sh
# hoặc
./packaging/scripts/build_deb.sh

# Cài đặt
sudo dpkg -i edgeos-api-*.deb
```

### Test Package trong Docker

Sau khi build package, bạn có thể test cài đặt trong Docker container:

```bash
# Sử dụng script tự động
./packaging/docker-test/test_deb_docker.sh

# Hoặc xem hướng dẫn chi tiết
cat packaging/docker-test/DOCKER_TEST_README.md
```

Xem [docker-test/README.md](docker-test/README.md) để biết thêm thông tin.

### Test Package trong VirtualBox VM

Để test package trong môi trường VM thực tế hơn:

#### Tạo VM bằng Command Line (Nhanh)

```bash
# Tạo VM tự động với script
./packaging/scripts/create_vm.sh Ubuntu-Test 4096 50000 2 \
    ~/Downloads/ubuntu-22.04.3-desktop-amd64.iso \
    /home/cvedix/Data/project/edgeos-api

# Khởi động VM
./packaging/scripts/vm_manage.sh start Ubuntu-Test

# Quản lý VM
./packaging/scripts/vm_manage.sh stop Ubuntu-Test    # Dừng VM
./packaging/scripts/vm_manage.sh info Ubuntu-Test     # Xem thông tin
./packaging/scripts/vm_manage.sh list                 # Liệt kê VM
```

#### Hướng Dẫn Chi Tiết

```bash
# Xem hướng dẫn chi tiết
cat packaging/docs/VM_TEST_GUIDE.md
```

Hướng dẫn bao gồm:
- Tạo VM trong VirtualBox (GUI và Command Line)
- Cài đặt Ubuntu (Server hoặc Desktop)
- Copy file `.deb` vào VM (Shared Folders / Drag & Drop / SCP)
- Test cài đặt và chạy application

Xem [docs/VM_TEST_GUIDE.md](docs/VM_TEST_GUIDE.md) để biết thêm thông tin.
