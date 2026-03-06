# Camera Handler Architecture

## Tổng Quan

Đã refactor code để tách xử lý cho từng loại camera ra file riêng, sử dụng **Strategy Pattern** và **Factory Pattern**.

## Cấu Trúc

### 1. Base Interface

**`include/core/onvif_camera_handler.h`**
- Interface chung cho tất cả camera handlers
- Methods:
  - `getDeviceInformation()` - Lấy thông tin camera
  - `getProfiles()` - Lấy danh sách profiles
  - `getStreamUri()` - Lấy stream URI
  - `getProfileConfiguration()` - Lấy cấu hình profile
  - `getMediaServiceUrl()` - Lấy media service URL
  - `supports()` - Kiểm tra handler có support camera này không

### 2. Factory Pattern

**`include/core/onvif_camera_handler_factory.h`**
- Tự động chọn handler phù hợp dựa trên camera manufacturer/model
- Singleton pattern
- Methods:
  - `getHandler(camera)` - Lấy handler cho camera cụ thể
  - `getHandler(cameraId)` - Lấy handler từ camera ID
  - `registerHandler()` - Đăng ký custom handler

### 3. Implementations

#### Generic Handler
**`include/core/onvif_camera_handlers/onvif_generic_handler.h`**
- Handler mặc định cho tất cả cameras
- Sử dụng Basic authentication
- Standard ONVIF SOAP format
- Fallback khi không có handler cụ thể

#### Tapo Handler
**`include/core/onvif_camera_handlers/onvif_tapo_handler.h`**
- Handler riêng cho Tapo cameras
- Đặc điểm:
  - WS-Security headers trong SOAP request
  - Timeout dài hơn (15s)
  - Detect Tapo camera qua manufacturer/model/port
  - Có thể implement Digest auth trong tương lai

## Cách Sử Dụng

### Trong Code

```cpp
#include "core/onvif_camera_handler_factory.h"

// Lấy handler cho camera
auto &factory = ONVIFCameraHandlerFactory::getInstance();
auto handler = factory.getHandler(cameraId);

// Sử dụng handler
std::vector<std::string> profiles = handler->getProfiles(camera, username, password);
std::string streamUri = handler->getStreamUri(camera, profileToken, username, password);
```

### Thêm Handler Mới

1. Tạo file header:
```cpp
// include/core/onvif_camera_handlers/onvif_hikvision_handler.h
class ONVIFHikvisionHandler : public ONVIFCameraHandler {
  // Implement các methods
};
```

2. Tạo file implementation:
```cpp
// src/core/onvif_camera_handlers/onvif_hikvision_handler.cpp
// Implement logic riêng cho Hikvision
```

3. Đăng ký trong Factory:
```cpp
// Trong onvif_camera_handler_factory.cpp constructor
handlers_.push_back(std::make_shared<ONVIFHikvisionHandler>());
```

4. Thêm vào CMakeLists.txt:
```cmake
src/core/onvif_camera_handlers/onvif_hikvision_handler.cpp
```

## Lợi Ích

1. **Tách biệt logic**: Mỗi loại camera có code riêng, dễ maintain
2. **Dễ mở rộng**: Thêm camera mới chỉ cần tạo handler mới
3. **Tự động chọn**: Factory tự động chọn handler phù hợp
4. **Fallback**: Luôn có Generic handler làm fallback
5. **Test dễ dàng**: Có thể test từng handler riêng

## Files Created

### Headers
- `include/core/onvif_camera_handler.h`
- `include/core/onvif_camera_handler_factory.h`
- `include/core/onvif_camera_handlers/onvif_generic_handler.h`
- `include/core/onvif_camera_handlers/onvif_tapo_handler.h`

### Implementations
- `src/core/onvif_camera_handler_factory.cpp`
- `src/core/onvif_camera_handlers/onvif_generic_handler.cpp`
- `src/core/onvif_camera_handlers/onvif_tapo_handler.cpp`

## Next Steps

1. ⏳ **Refactor ONVIFStreamManager**: Cập nhật để sử dụng factory pattern
2. ⏳ **Refactor ONVIFDiscovery**: Cập nhật để sử dụng handlers
3. ⏳ **Add more handlers**: Hikvision, Dahua, Axis, etc.
4. ⏳ **Implement Digest auth**: Trong Tapo handler hoặc tạo auth helper

