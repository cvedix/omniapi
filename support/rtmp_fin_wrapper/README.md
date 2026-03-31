# LD_PRELOAD wrapper: TCP FIN on socket close

Khi **không có quyền sửa SDK** (cvedix), có thể dùng workaround tạm thời: thư viện này override `close()` và gọi `shutdown(fd, SHUT_RDWR)` trước khi `close()` cho mọi **socket TCP** trong process (gồm socket rtmp_des do SDK đóng).

## Build

Từ thư mục build của project:

```bash
cd build
cmake ..
make
```

File `.so` nằm tại: `build/lib/libclose_fin.so`

## Cách dùng

**Chỉ worker** (mỗi instance chạy trong worker, rtmp_des ở trong worker):

```bash
LD_PRELOAD=/path/to/build/lib/libclose_fin.so ./bin/edgeos-worker
```

**Cả API + worker** (worker spawn bởi API sẽ kế thừa `LD_PRELOAD`):

```bash
LD_PRELOAD=/path/to/build/lib/libclose_fin.so ./bin/omniapi
```

Ví dụ từ thư mục **gốc project** (omniapi):

```bash
LD_PRELOAD=$PWD/build/lib/libclose_fin.so EDGE_AI_EXECUTION_MODE=subprocess ./build/bin/omniapi
```

**Nếu chạy từ trong thư mục `build/`** thì không thêm `/build` (vì `$PWD` đã là build):

```bash
LD_PRELOAD=$PWD/lib/libclose_fin.so EDGE_AI_EXECUTION_MODE=subprocess ./bin/omniapi
```

Kết hợp test hot-swap (delay 5s sau khi pipeline mới đã chạy):

```bash
# Từ gốc project:
LD_PRELOAD=$PWD/build/lib/libclose_fin.so EDGE_AI_EXECUTION_MODE=subprocess EDGE_AI_HOTSWAP_DELAY_SEC=5 ./build/bin/omniapi
# Từ trong build/:
LD_PRELOAD=$PWD/lib/libclose_fin.so EDGE_AI_EXECUTION_MODE=subprocess EDGE_AI_HOTSWAP_DELAY_SEC=5 ./bin/omniapi
```

## Lưu ý

- Áp dụng cho **mọi** socket TCP trong process (IPC, RTMP, …). Code hiện tại của omniapi (unix_socket.cpp) đã gọi `shutdown`+`close` đúng cách; wrapper chỉ thêm bước tương tự cho các fd mà SDK chỉ gọi `close()`.
- Cách làm chuẩn vẫn là SDK (cvedix_rtmp_des_node) trong stop/destructor gọi `shutdown(socket_fd, SHUT_RDWR); close(socket_fd);` (xem ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md §12).
- Nếu sau này SDK đã sửa đúng, có thể bỏ `LD_PRELOAD` để không dùng wrapper nữa.
