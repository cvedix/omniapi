# Auto-Version Increment Feature

## Tổng Quan

Build script tự động tăng version mỗi lần build package. Version được lưu trong file `VERSION` và tự động cập nhật trong tất cả các file liên quan.

## Cách Hoạt Động

### 1. Version Format
Version format: `MAJOR.MINOR.PATCH.BUILD`
- **MAJOR**: Năm (2026)
- **MINOR**: Major release (0)
- **PATCH**: Minor release (1)
- **BUILD**: Auto-increment number (1, 2, 3, ...)

Ví dụ: `2026.0.1.1` → `2026.0.1.2` → `2026.0.1.3`

### 2. Auto-Increment Logic
Mỗi lần chạy `./build_deb.sh`, script sẽ:
1. Đọc version hiện tại từ file `VERSION` (hoặc CMakeLists.txt)
2. Tự động tăng số BUILD (số cuối cùng)
3. Cập nhật version trong:
   - `VERSION` file
   - `CMakeLists.txt`
   - `debian/changelog`

### 3. Files Được Cập Nhật Tự Động
- ✅ `VERSION` - File chính lưu version
- ✅ `CMakeLists.txt` - PROJECT_VERSION
- ✅ `debian/changelog` - Package version

## Sử Dụng

### Build với Auto-Increment (Mặc định)
```bash
./build_deb.sh
# Version tự động tăng: 2026.0.1.1 → 2026.0.1.2
```

### Build với Version Cụ Thể (Không auto-increment)
```bash
./build_deb.sh --version 2026.0.2.0
# Sử dụng version chỉ định, không tăng
```

### Build Không Tăng Version
```bash
./build_deb.sh --no-increment
# Giữ nguyên version hiện tại
```

### Xem Version Hiện Tại
```bash
cat VERSION
# Hoặc
grep "project(edgeos_api VERSION" CMakeLists.txt
```

## Ví Dụ

### Lần Build Đầu Tiên
```bash
$ cat VERSION
2026.0.1.1

$ ./build_deb.sh
==========================================
Auto-Incrementing Version
==========================================
Old version: 2026.0.1.1
New version: 2026.0.1.2
...
✓ Updated VERSION
✓ Updated CMakeLists.txt
✓ Updated debian/changelog
...
```

### Lần Build Thứ Hai
```bash
$ cat VERSION
2026.0.1.2

$ ./build_deb.sh
==========================================
Auto-Incrementing Version
==========================================
Old version: 2026.0.1.2
New version: 2026.0.1.3
...
```

## Manual Version Update

Nếu muốn thay đổi version thủ công (ví dụ: major/minor release):

```bash
# 1. Update VERSION file
echo "2026.0.2.0" > VERSION

# 2. Update CMakeLists.txt
sed -i 's/project(edgeos_api VERSION [0-9.]*)/project(edgeos_api VERSION 2026.0.2.0)/' CMakeLists.txt

# 3. Update debian/changelog
sed -i '1s/edgeos-api ([0-9.]*)/edgeos-api (2026.0.2.0)/' debian/changelog

# 4. Build với --no-increment để không tăng thêm
./build_deb.sh --no-increment
```

Hoặc đơn giản hơn:
```bash
./build_deb.sh --version 2026.0.2.0
```

## Lưu Ý

1. **Git Commit**: Nên commit file `VERSION` sau mỗi lần build để track version
2. **CI/CD**: Trong CI/CD, có thể dùng `--no-increment` nếu muốn control version từ bên ngoài
3. **Release**: Khi release, có thể set version cụ thể với `--version`

## Troubleshooting

### Version không tăng
- Kiểm tra file `VERSION` có tồn tại không
- Kiểm tra quyền ghi file
- Xem log output để biết version cũ và mới

### Version không đồng bộ
- Chạy lại `./build_deb.sh --no-increment` để sync
- Hoặc update thủ công các file

### Reset Version
```bash
echo "2026.0.1.1" > VERSION
./build_deb.sh --no-increment
```

