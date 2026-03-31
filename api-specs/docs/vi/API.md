# Edge AI API

**Version:** 1.0.0

REST API cho các thao tác và quản lý Edge AI.

## Tính năng Ghi log

Máy chủ hỗ trợ các tính năng ghi log chi tiết có thể được bật qua các tham số dòng lệnh:

- **Ghi log API** (`--log-api` hoặc `--debug-api`): Ghi lại tất cả các yêu cầu và phản hồi API cùng với thời gian phản hồi
- **Ghi log Thực thi Instance** (`--log-instance` hoặc `--debug-instance`): Ghi lại các sự kiện vòng đời instance (start/stop/status)
- **Ghi log Đầu ra SDK** (`--log-sdk-output` hoặc `--debug-sdk-output`): Ghi lại đầu ra SDK khi các instance xử lý dữ liệu

Ví dụ sử dụng:
```bash
./omniapi --log-api --log-instance --log-sdk-output
```

Để biết thêm chi tiết, xem [Tài liệu Ghi log](../docs/LOGGING.md).

## Swagger UI

API này cung cấp Swagger UI để kiểm thử và khám phá tương tác:
- Truy cập Swagger UI tại `/swagger` hoặc `/v1/swagger`
- Đặc tả OpenAPI có sẵn tại `/openapi.yaml` hoặc `/v1/openapi.yaml`
- URL máy chủ được cập nhật tự động dựa trên các biến môi trường (`API_HOST` và `API_PORT`)

---

## AI

### `POST` /v1/core/ai/batch

**Summary:** Xử lý hàng loạt hình ảnh/khung hình

**Operation ID:** `processBatch`

Xử lý nhiều hình ảnh/khung hình trong một lô qua pipeline xử lý AI.
Hiện tại trả về 501 Not Implemented.

**Request body:**

- **application/json**
  - type: `object`
    - **images**: array — Mảng các hình ảnh được mã hóa Base64
    - **config**: string — Cấu hình xử lý (chuỗi JSON)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Công việc xử lý hàng loạt đã được gửi thành công |
| 400 | Yêu cầu không hợp lệ |
| 501 | Chưa triển khai |


### `GET` /v1/core/ai/metrics

**Summary:** Lấy số liệu xử lý AI

**Operation ID:** `getAIMetrics`

Trả về số liệu chi tiết về xử lý AI bao gồm thống kê hiệu suất,
thống kê cache và thông tin giới hạn tốc độ.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Số liệu xử lý AI |


### `POST` /v1/core/ai/process

**Summary:** Xử lý một hình ảnh/khung hình

**Operation ID:** `processImage`

Xử lý một hình ảnh/khung hình qua pipeline xử lý AI.
Hỗ trợ hàng đợi dựa trên độ ưu tiên và giới hạn tốc độ.

**Request body:**

- **application/json**
  - type: `object`
    - **image**: string — Dữ liệu hình ảnh được mã hóa Base64
    - **config**: string — Cấu hình xử lý (chuỗi JSON)
    - **priority**: string — Độ ưu tiên yêu cầu

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Công việc xử lý đã được gửi thành công |
| 400 | Yêu cầu không hợp lệ (JSON không hợp lệ hoặc thiếu trường bắt buộc) |
| 429 | Vượt giới hạn tốc độ (rate limit) |
| 500 | Lỗi máy chủ |
| 501 | Chưa triển khai |


### `GET` /v1/core/ai/status

**Summary:** Lấy trạng thái xử lý AI

**Operation ID:** `getAIStatus`

Trả về trạng thái hiện tại của hệ thống xử lý AI bao gồm kích thước hàng đợi,
dung lượng tối đa hàng đợi, khả năng sử dụng GPU và thông tin tài nguyên khác.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Trạng thái xử lý AI |



## Area Core

### `DELETE` /v1/core/instance/{instanceId}/jams

**Summary:** Xóa tất cả vùng kẹt xe (jam zones)

**Operation ID:** `deleteAllJams`

Xóa tất cả vùng kẹt xe (jam zones) của instance ba_jam.

**Cập nhật thời gian thực:** Các vùng được gỡ khỏi instance đang chạy khi có thể; nếu không sẽ khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa tất cả vùng kẹt xe thành công |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/jams

**Summary:** Lấy tất cả vùng kẹt xe (jam zones)

**Operation ID:** `getAllJams`

Trả về tất cả các vùng kẹt xe (jam zones) đã cấu hình cho instance ba_jam. Jam zones định nghĩa các ROI nơi thực hiện phát hiện ùn tắc giao thông.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách vùng kẹt xe (jam zones) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/jams

**Summary:** CORS preflight cho các endpoint jams

**Operation ID:** `jamsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/jams

**Summary:** Tạo một hoặc nhiều vùng kẹt xe (jam zones)

**Operation ID:** `createJam`

Tạo một hoặc nhiều vùng kẹt xe (jam zones) cho instance ba_jam. Jam zones dùng để phát hiện ùn tắc giao thông trong một ROI chỉ định.

**Định dạng yêu cầu:**
- Một vùng: Gửi một object (CreateJamRequest)
- Nhiều vùng: Gửi mảng các object [CreateJamRequest, ...]

**Cập nhật thời gian thực:** Các vùng được áp dụng cho instance đang chạy khi có thể; nếu không sẽ khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Request body:**

- **application/json**
  - type: `object`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Tạo vùng thành công. Trả về một object vùng với yêu cầu đơn, hoặc object kèm metadata với yêu cầu mảng. |
| 400 | Yêu cầu không hợp lệ (ROI hoặc tham số sai) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/instance/{instanceId}/jams/{jamId}

**Summary:** Xóa một vùng kẹt xe (jam zone) theo ID

**Operation ID:** `deleteJam`

Xóa một vùng kẹt xe (jam zone) theo ID.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| jamId | path | Yes | Định danh duy nhất của vùng kẹt xe cần xóa |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa vùng kẹt xe thành công |
| 404 | Không tìm thấy instance hoặc vùng |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/jams/{jamId}

**Summary:** Lấy một vùng kẹt xe (jam zone) theo ID

**Operation ID:** `getJam`

Trả về một vùng kẹt xe (jam zone) theo ID cho instance ba_jam.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| jamId | path | Yes | Định danh duy nhất của vùng kẹt xe cần lấy |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy vùng kẹt xe thành công |
| 404 | Không tìm thấy instance hoặc vùng |
| 500 | Lỗi máy chủ |


### `PUT` /v1/core/instance/{instanceId}/jams/{jamId}

**Summary:** Cập nhật một vùng kẹt xe (jam zone) theo ID

**Operation ID:** `updateJam`

Cập nhật vùng kẹt xe (jam zone) theo ID. Hỗ trợ cập nhật một phần.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| jamId | path | Yes | Định danh duy nhất của vùng kẹt xe cần cập nhật |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateJamRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật vùng kẹt xe thành công |
| 400 | Yêu cầu không hợp lệ (ROI hoặc tham số sai) |
| 404 | Không tìm thấy instance hoặc vùng |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/instance/{instanceId}/stops

**Summary:** Xóa tất cả vùng dừng (stop zones)

**Operation ID:** `deleteAllStops`

Xóa tất cả vùng dừng (stop zones) của instance ba_stop. Thay đổi yêu cầu khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa tất cả vùng dừng thành công |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/stops

**Summary:** Lấy tất cả vùng dừng (stop zones)

**Operation ID:** `getAllStops`

Trả về tất cả vùng dừng (stop zones) đã cấu hình cho instance ba_stop. Vùng dừng dùng để phát hiện đối tượng dừng trong ROI xác định trên luồng video.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách vùng dừng (stop zones) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/stops

**Summary:** CORS preflight cho các endpoint stops

**Operation ID:** `stopsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/stops

**Summary:** Tạo một hoặc nhiều vùng dừng (stop zones)

**Operation ID:** `createStop`

Tạo một hoặc nhiều vùng dừng (stop zones) cho instance ba_stop.

**Định dạng yêu cầu:**
- Một vùng: Gửi một object (CreateStopRequest)
- Nhiều vùng: Gửi mảng các object [CreateStopRequest, ...]

**Cập nhật thời gian thực:** Thay đổi vùng dừng yêu cầu khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Request body:**

- **application/json**
  - type: `object`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Tạo vùng thành công. Trả về một object vùng với yêu cầu đơn, hoặc object kèm metadata với yêu cầu mảng. |
| 400 | Yêu cầu không hợp lệ (tọa độ hoặc tham số sai) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** Xóa một vùng dừng (stop zone) theo ID

**Operation ID:** `deleteStop`

Xóa vùng dừng (stop zone) theo ID cho instance ba_stop. Thay đổi yêu cầu khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| stopId | path | Yes | Định danh duy nhất của vùng dừng cần xóa |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa vùng dừng thành công |
| 404 | Không tìm thấy instance hoặc vùng dừng |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** Lấy một vùng dừng (stop zone) theo ID

**Operation ID:** `getStop`

Trả về một vùng dừng (stop zone) theo ID cho instance ba_stop.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| stopId | path | Yes | Định danh duy nhất của vùng dừng cần lấy |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy vùng dừng thành công |
| 404 | Không tìm thấy instance hoặc vùng dừng |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** CORS preflight cho endpoint chi tiết vùng dừng

**Operation ID:** `stopOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** Cập nhật một vùng dừng (stop zone) theo ID

**Operation ID:** `updateStop`

Cập nhật vùng dừng (stop zone) theo ID cho instance ba_stop. Thay đổi yêu cầu khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| stopId | path | Yes | Định danh duy nhất của vùng dừng cần cập nhật |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateStopRequest

**Responses:**




## Area SecuRT

### `OPTIONS` /v1/securt/instance/{instanceId}/area/armedPerson

**Summary:** CORS preflight cho vùng armed person

**Operation ID:** `createArmedPersonAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/armedPerson

**Summary:** Tạo vùng người mang vũ khí (armed person area)

**Operation ID:** `createArmedPersonArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ArmedPersonAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng người mang vũ khí (armed person area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/armedPerson

**Summary:** Tạo vùng người mang vũ khí với ID (armed person area with ID)

**Operation ID:** `createArmedPersonAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ArmedPersonAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng người mang vũ khí (armed person area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/crossing

**Summary:** CORS preflight cho vùng crossing

**Operation ID:** `createCrossingAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/crossing

**Summary:** Tạo vùng vượt (crossing area)

**Operation ID:** `createCrossingArea`

Tạo vùng vượt (crossing area) mới cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vượt (crossing area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/crossing

**Summary:** Tạo vùng vượt với ID (crossing area with ID)

**Operation ID:** `createCrossingAreaWithId`

Tạo vùng vượt với ID cụ thể cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| areaId | path | Yes | Định danh vùng (area ID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vượt (crossing area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/crowdEstimation

**Summary:** CORS preflight cho vùng crowd estimation

**Operation ID:** `createCrowdEstimationAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/crowdEstimation

**Summary:** Tạo vùng ước lượng đám đông (crowd estimation area)

**Operation ID:** `createCrowdEstimationArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrowdEstimationAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng ước lượng đám đông (crowd estimation area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/crowdEstimation

**Summary:** Tạo vùng ước lượng đám đông với ID (crowd estimation area with ID)

**Operation ID:** `createCrowdEstimationAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrowdEstimationAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng ước lượng đám đông (crowd estimation area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/crowding

**Summary:** CORS preflight cho vùng crowding

**Operation ID:** `createCrowdingAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/crowding

**Summary:** Tạo vùng tụ tập (crowding area)

**Operation ID:** `createCrowdingArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrowdingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng tụ tập (crowding area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/crowding

**Summary:** Tạo vùng tụ tập với ID (crowding area with ID)

**Operation ID:** `createCrowdingAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrowdingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng tụ tập (crowding area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/dwelling

**Summary:** CORS preflight cho vùng dwelling

**Operation ID:** `createDwellingAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/dwelling

**Summary:** Tạo vùng cư trú (dwelling area)

**Operation ID:** `createDwellingArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/DwellingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng trú ngụ (dwelling area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/dwelling

**Summary:** Tạo vùng cư trú với ID (dwelling area with ID)

**Operation ID:** `createDwellingAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/DwellingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng trú ngụ (dwelling area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/faceCovered

**Summary:** CORS preflight cho vùng face covered

**Operation ID:** `createFaceCoveredAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/faceCovered

**Summary:** Tạo vùng che mặt (face covered - thử nghiệm)

**Operation ID:** `createFaceCoveredArea`

Tạo vùng che mặt (face covered area) mới cho instance SecuRT. Tính năng thử nghiệm.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/FaceCoveredAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng che mặt (face covered area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/faceCovered

**Summary:** Tạo vùng che mặt với ID (face covered - thử nghiệm)

**Operation ID:** `createFaceCoveredAreaWithId`

Tạo vùng che mặt với ID cụ thể cho instance SecuRT. Tính năng thử nghiệm.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| areaId | path | Yes | Định danh vùng (area ID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/FaceCoveredAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng che mặt (face covered area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/fallenPerson

**Summary:** CORS preflight cho vùng fallen person

**Operation ID:** `createFallenPersonAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/fallenPerson

**Summary:** Tạo vùng người ngã (fallen person area)

**Operation ID:** `createFallenPersonArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/FallenPersonAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng người ngã (fallen person area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/fallenPerson

**Summary:** Tạo vùng người ngã với ID (fallen person area with ID)

**Operation ID:** `createFallenPersonAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/FallenPersonAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng người ngã (fallen person area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/intrusion

**Summary:** CORS preflight cho vùng intrusion

**Operation ID:** `createIntrusionAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/intrusion

**Summary:** Tạo vùng xâm nhập (intrusion area)

**Operation ID:** `createIntrusionArea`

Tạo vùng xâm nhập (intrusion area) mới cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/IntrusionAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng xâm nhập (intrusion area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/intrusion

**Summary:** Tạo vùng xâm nhập với ID (intrusion area with ID)

**Operation ID:** `createIntrusionAreaWithId`

Tạo vùng xâm nhập với ID cụ thể cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/IntrusionAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng xâm nhập (intrusion area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/loitering

**Summary:** CORS preflight cho vùng loitering

**Operation ID:** `createLoiteringAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/loitering

**Summary:** Tạo vùng lượn (loitering area)

**Operation ID:** `createLoiteringArea`

Tạo vùng lượn vòng (loitering area) mới cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/LoiteringAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng lượn vòng (loitering area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/loitering

**Summary:** Tạo vùng lượn với ID (loitering area with ID)

**Operation ID:** `createLoiteringAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/LoiteringAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng lượn vòng (loitering area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/objectEnterExit

**Summary:** CORS preflight cho vùng object enter/exit

**Operation ID:** `createObjectEnterExitAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/objectEnterExit

**Summary:** Tạo vùng vật vào/ra (object enter/exit area)

**Operation ID:** `createObjectEnterExitArea`

Tạo vùng vật vào/ra (object enter/exit area) mới cho solution BA Area Enter/Exit. Phát hiện vật vào/ra area.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectEnterExitAreaWrite
  - example: `{'name': 'Entrance Area', 'coordinates': [{'x': 50, 'y': 150}, {'x': 250, 'y': 150}, {'x': 250, 'y': 350}, {'x': 50, 'y': 350}], 'classes': ['Person', 'Vehicle'], 'color': [0, 220, 0, 255], 'alertOnEnter': True, 'alertOnExit': True}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vật vào/ra (object enter/exit area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/objectEnterExit

**Summary:** Tạo vùng vật vào/ra với ID (object enter/exit area with ID)

**Operation ID:** `createObjectEnterExitAreaWithId`

Tạo vùng vật vào/ra với ID vùng cụ thể cho solution BA Area Enter/Exit.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| areaId | path | Yes | Định danh vùng (area ID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectEnterExitAreaWrite
  - example: `{'name': 'Restricted Zone', 'coordinates': [{'x': 350, 'y': 160}, {'x': 550, 'y': 160}, {'x': 550, 'y': 360}, {'x': 350, 'y': 360}], 'classes': ['Person'], 'color': [0, 0, 220, 255], 'alertOnEnter': True, 'alertOnExit': False}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vật vào/ra (object enter/exit area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/objectLeft

**Summary:** CORS preflight cho vùng object left

**Operation ID:** `createObjectLeftAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/objectLeft

**Summary:** Tạo vùng vật bỏ quên (object left area)

**Operation ID:** `createObjectLeftArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectLeftAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vật bỏ quên (object left area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/objectLeft

**Summary:** Tạo vùng vật bỏ quên với ID (object left area with ID)

**Operation ID:** `createObjectLeftAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectLeftAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vật bỏ quên (object left area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/objectRemoved

**Summary:** CORS preflight cho vùng object removed

**Operation ID:** `createObjectRemovedAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/objectRemoved

**Summary:** Tạo vùng vật bị dời (object removed area)

**Operation ID:** `createObjectRemovedArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectRemovedAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vật bị dời (object removed area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/objectRemoved

**Summary:** Tạo vùng vật bị dời với ID (object removed area with ID)

**Operation ID:** `createObjectRemovedAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectRemovedAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng vật bị dời (object removed area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/occupancy

**Summary:** CORS preflight cho vùng occupancy

**Operation ID:** `createOccupancyAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/occupancy

**Summary:** Tạo vùng chiếm dụng (occupancy area)

**Operation ID:** `createOccupancyArea`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/OccupancyAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng chiếm dụng (occupancy area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/occupancy

**Summary:** Tạo vùng chiếm dụng với ID (occupancy area with ID)

**Operation ID:** `createOccupancyAreaWithId`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes |  |

| areaId | path | Yes |  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/OccupancyAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng chiếm dụng (occupancy area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/vehicleGuard

**Summary:** CORS preflight cho vùng vehicle guard

**Operation ID:** `createVehicleGuardAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/area/vehicleGuard

**Summary:** Tạo vùng bảo vệ xe (vehicle guard - thử nghiệm)

**Operation ID:** `createVehicleGuardArea`

Tạo vùng bảo vệ xe (vehicle guard area) mới cho instance SecuRT. Tính năng thử nghiệm.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/VehicleGuardAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng canh xe (vehicle guard area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}/area/vehicleGuard

**Summary:** Tạo vùng bảo vệ xe với ID (vehicle guard - thử nghiệm)

**Operation ID:** `createVehicleGuardAreaWithId`

Tạo vùng bảo vệ xe với ID cụ thể cho instance SecuRT. Tính năng thử nghiệm.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| areaId | path | Yes | Định danh vùng (area ID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/VehicleGuardAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo vùng canh xe (vehicle guard area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Vùng đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/securt/instance/{instanceId}/area/{areaId}

**Summary:** Xóa vùng theo ID

**Operation ID:** `deleteArea`

Xóa một vùng cụ thể theo ID

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| areaId | path | Yes | Định danh vùng (area ID) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã xóa vùng thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy vùng hoặc instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/{areaId}

**Summary:** CORS preflight cho xóa vùng

**Operation ID:** `deleteAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `DELETE` /v1/securt/instance/{instanceId}/areas

**Summary:** Xóa tất cả vùng phân tích của instance SecuRT

**Operation ID:** `deleteAllSecuRTAreas`

Xóa tất cả vùng phân tích (analytics areas) của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã xóa tất cả vùng thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/areas

**Summary:** Lấy tất cả vùng phân tích (analytics areas) của instance SecuRT

**Operation ID:** `getSecuRTAreas`

Trả về tất cả vùng phân tích đã cấu hình cho instance SecuRT, gồm vùng vượt (crossing), xâm nhập (intrusion), lượn (loitering), tụ tập (crowding), chiếm dụng (occupancy), ước lượng đám đông (crowd estimation), cư trú (dwelling), người mang vũ khí (armed person), vật bỏ quên (object left), vật bị dời (object removed), người ngã (fallen person), bảo vệ xe (vehicle guard - thử nghiệm), che mặt (face covered - thử nghiệm).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy vùng phân tích thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/areas

**Summary:** CORS preflight cho areas

**Operation ID:** `getSecuRTAreasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |



## Config

### `DELETE` /v1/core/config

**Summary:** Xóa phần cấu hình (tham số truy vấn)

**Operation ID:** `deleteConfigSectionQuery`

Xóa một phần cụ thể của cấu hình hệ thống tại đường dẫn đã cho.

**Định dạng Đường dẫn:**
- Hỗ trợ cả dấu gạch chéo `/` và dấu chấm `.` làm dấu phân cách
- **Khuyến nghị:** Sử dụng dấu gạch chéo `/` để tương thích tốt hơn
- Ví dụ:
  - `system/max_running_instances` (với dấu gạch chéo - khuyến nghị)
  - `system.max_running_instances` (với dấu chấm)
  - `system/web_server/port` (đường dẫn lồng nhau với dấu gạch chéo)
  - `system.web_server.port` (đường dẫn lồng nhau với dấu chấm)

Cấu hình được lưu vào file cấu hình sau khi xóa.

**Tự động Khởi động lại:**
- Theo mặc định, máy chủ sẽ KHÔNG tự động khởi động lại sau khi xóa cấu hình
- Để bật khởi động lại tự động, đặt `auto_restart=true` làm tham số truy vấn
- Khi `auto_restart=true`, máy chủ sẽ khởi động lại sau 3 giây nếu cấu hình web server thay đổi

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | query | Yes | Đường dẫn cấu hình. Hỗ trợ cả `/` (dấu gạch chéo) và `.` (dấu chấm) làm dấu phân cách. **Khuyến nghị:** Sử dụng dấu gạch chéo `/` để tương thích tốt hơn. Ví dụ: `system/max_running_instances`, `system.web_server`, `system/web_server/port`  |

| auto_restart | query | No | Nếu đặt là `true`, máy chủ sẽ tự động khởi động lại sau khi xóa cấu hình. Mặc định là `false` (không khởi động lại tự động).  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phần cấu hình đã được xóa thành công |
| 400 | Yêu cầu không hợp lệ (đường dẫn trống) |
| 404 | Không tìm thấy phần cấu hình |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/config

**Summary:** Lấy cấu hình hệ thống

**Operation ID:** `getConfig`

Trả về cấu hình hệ thống. Nếu tham số truy vấn `path` được cung cấp, trả về phần cấu hình cụ thể.
Nếu không, trả về cấu hình hệ thống đầy đủ.

**Định dạng Đường dẫn:**
- Sử dụng dấu gạch chéo `/` hoặc dấu chấm `.` để điều hướng các khóa cấu hình lồng nhau
- Ví dụ: `system/max_running_instances`, `system.max_running_instances`, `gstreamer/decode_pipelines/auto`

**Khuyến nghị:** Sử dụng tham số truy vấn với dấu gạch chéo để tương thích tốt hơn.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | query | No | Đường dẫn cấu hình để lấy một phần cụ thể. Sử dụng `/` hoặc `.` làm dấu phân cách. Ví dụ: `system/max_running_instances`, `system.web_server`, `gstreamer/decode_pipelines/auto`  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình hệ thống đầy đủ |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/config

**Summary:** CORS preflight cho cấu hình

**Operation ID:** `configOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PATCH` /v1/core/config

**Summary:** Cập nhật phần cấu hình (tham số truy vấn)

**Operation ID:** `updateConfigSectionQuery`

Cập nhật một phần cụ thể của cấu hình hệ thống tại đường dẫn đã cho.

**Định dạng Đường dẫn:**
- Sử dụng dấu gạch chéo `/` hoặc dấu chấm `.` làm dấu phân cách
- Ví dụ: `system/max_running_instances`, `system.max_running_instances`

Chỉ các trường được cung cấp trong phần sẽ được cập nhật.
Cấu hình được lưu vào file cấu hình sau khi cập nhật.

**Tự động Khởi động lại:**
- Theo mặc định, máy chủ sẽ KHÔNG tự động khởi động lại sau khi thay đổi cấu hình
- Để bật khởi động lại tự động, đặt `auto_restart=true` làm tham số truy vấn hoặc trong body JSON
- Khi `auto_restart=true`, máy chủ sẽ khởi động lại sau 3 giây nếu cấu hình web server thay đổi

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | query | Yes | Đường dẫn cấu hình sử dụng `/` hoặc `.` làm dấu phân cách |

| auto_restart | query | No | Nếu đặt là `true`, máy chủ sẽ tự động khởi động lại sau khi cập nhật cấu hình. Mặc định là `false` (không tự động khởi động lại). Cũng có thể đặt trong body JSON như trường `auto_restart`.  |

**Request body:**

- **application/json**
  - type: `object` — Giá trị phần cấu hình để cập nhật. Có thể tùy chọn bao gồm trường `auto_restart` (boolean) để kích hoạt khởi động lại máy chủ.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phần cấu hình đã được cập nhật thành công |
| 400 | Yêu cầu không hợp lệ hoặc xác thực thất bại |
| 500 | Lỗi máy chủ |


### `POST` /v1/core/config

**Summary:** Tạo hoặc cập nhật cấu hình (gộp)

**Operation ID:** `createOrUpdateConfig`

Cập nhật cấu hình hệ thống bằng cách gộp JSON được cung cấp với cấu hình hiện có.
Chỉ các trường được cung cấp sẽ được cập nhật; các trường khác giữ nguyên.
Cấu hình được lưu vào file cấu hình sau khi cập nhật.

**Tự động Khởi động lại:**
- Theo mặc định, máy chủ sẽ KHÔNG tự động khởi động lại sau khi thay đổi cấu hình
- Để bật khởi động lại tự động, đặt `auto_restart=true` làm tham số truy vấn hoặc trong body JSON
- Khi `auto_restart=true`, máy chủ sẽ khởi động lại sau 3 giây nếu cấu hình web server thay đổi

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| auto_restart | query | No | Nếu đặt là `true`, máy chủ sẽ tự động khởi động lại sau khi cập nhật cấu hình. Mặc định là `false` (không tự động khởi động lại). Cũng có thể đặt trong body JSON như trường `auto_restart`.  |

**Request body:**

- **application/json**
  - type: `object` — Đối tượng cấu hình để gộp. Có thể tùy chọn bao gồm trường `auto_restart` (boolean) để kích hoạt khởi động lại máy chủ.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình đã được cập nhật thành công |
| 400 | Yêu cầu không hợp lệ hoặc xác thực thất bại |
| 500 | Lỗi máy chủ |


### `PUT` /v1/core/config

**Summary:** Thay thế toàn bộ cấu hình

**Operation ID:** `replaceConfig`

Thay thế toàn bộ cấu hình hệ thống bằng JSON được cung cấp.
Tất cả cấu hình hiện có sẽ được thay thế bằng các giá trị mới.
Cấu hình được lưu vào file cấu hình sau khi thay thế.

**Tự động Khởi động lại:**
- Theo mặc định, máy chủ sẽ KHÔNG tự động khởi động lại sau khi thay đổi cấu hình
- Để bật khởi động lại tự động, đặt `auto_restart=true` làm tham số truy vấn hoặc trong body JSON
- Khi `auto_restart=true`, máy chủ sẽ khởi động lại sau 3 giây nếu cấu hình web server thay đổi

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| auto_restart | query | No | Nếu đặt là `true`, máy chủ sẽ tự động khởi động lại sau khi thay thế cấu hình. Mặc định là `false` (không tự động khởi động lại). Cũng có thể đặt trong body JSON như trường `auto_restart`.  |

**Request body:**

- **application/json**
  - type: `object` — Đối tượng cấu hình đầy đủ để thay thế cấu hình hiện có. Có thể tùy chọn bao gồm trường `auto_restart` (boolean) để kích hoạt khởi động lại máy chủ.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình đã được thay thế thành công |
| 400 | Yêu cầu không hợp lệ hoặc xác thực thất bại |
| 500 | Lỗi máy chủ |


### `POST` /v1/core/config/reset

**Summary:** Đặt lại cấu hình về mặc định

**Operation ID:** `resetConfig`

Đặt lại toàn bộ cấu hình hệ thống về giá trị mặc định.
Tất cả cấu hình hiện có sẽ được thay thế bằng giá trị mặc định.
Cấu hình được lưu vào file cấu hình sau khi đặt lại.

**Cảnh báo:** Thao tác này sẽ thay thế tất cả cấu hình bằng giá trị mặc định.
Hãy cân nhắc sao lưu cấu hình của bạn trước khi đặt lại.

**Tự động Khởi động lại:**
- Theo mặc định, máy chủ sẽ KHÔNG tự động khởi động lại sau khi đặt lại cấu hình
- Để bật khởi động lại tự động, đặt `auto_restart=true` làm tham số truy vấn hoặc trong body JSON
- Khi `auto_restart=true`, máy chủ sẽ khởi động lại sau 3 giây nếu cấu hình web server thay đổi

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| auto_restart | query | No | Nếu đặt là `true`, máy chủ sẽ tự động khởi động lại sau khi đặt lại cấu hình. Mặc định là `false` (không tự động khởi động lại). Cũng có thể đặt trong body JSON như trường `auto_restart`.  |

**Request body:**

- **application/json**
  - type: `object` — Body JSON tùy chọn. Có thể bao gồm trường `auto_restart` (boolean) để kích hoạt khởi động lại máy chủ.
    - **auto_restart**: boolean — Nếu true, máy chủ sẽ khởi động lại sau khi đặt lại

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình đã được đặt lại thành công |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/config/{path}

**Summary:** Xóa phần cấu hình (tham số đường dẫn)

**Operation ID:** `deleteConfigSection`

Xóa một phần cụ thể của cấu hình hệ thống tại đường dẫn đã cho.

**Định dạng Đường dẫn:**
- Sử dụng dấu chấm `.` làm dấu phân cách (ví dụ: `system.max_running_instances`)
- Dấu gạch chéo `/` với mã hóa URL (`%2F`) KHÔNG được hỗ trợ

**Khuyến nghị:** Sử dụng tham số truy vấn thay thế: `DELETE /v1/core/config?path=system/max_running_instances`

Cấu hình được lưu vào file cấu hình sau khi xóa.

**Tự động Khởi động lại:**
- Theo mặc định, máy chủ sẽ KHÔNG tự động khởi động lại sau khi xóa cấu hình
- Để bật khởi động lại tự động, đặt `auto_restart=true` làm tham số truy vấn
- Khi `auto_restart=true`, máy chủ sẽ khởi động lại sau 3 giây nếu cấu hình web server thay đổi

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Đường dẫn cấu hình sử dụng dấu chấm làm dấu phân cách. Examples: `system.max_running_instances`, `system.web_server` Note: Forward slashes are NOT supported in path parameter (use query parameter instead)  |

| auto_restart | query | No | Nếu đặt là `true`, máy chủ sẽ tự động khởi động lại sau khi xóa cấu hình. Mặc định là `false` (không khởi động lại tự động).  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phần cấu hình đã được xóa thành công |
| 400 | Yêu cầu không hợp lệ (đường dẫn trống) |
| 404 | Không tìm thấy phần cấu hình |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/config/{path}

**Summary:** Lấy phần cấu hình (tham số đường dẫn)

**Operation ID:** `getConfigSection`

Trả về một phần cụ thể của cấu hình hệ thống tại đường dẫn đã cho.

**Định dạng Đường dẫn:**
- Sử dụng dấu chấm `.` làm dấu phân cách (ví dụ: `system.max_running_instances`)
- Dấu gạch chéo `/` với mã hóa URL (`%2F`) KHÔNG được hỗ trợ do giới hạn định tuyến Drogon

**Khuyến nghị:** Sử dụng tham số truy vấn thay thế: `GET /v1/core/config?path=system/max_running_instances`

Ví dụ đường dẫn: `system.max_running_instances`, `system.web_server`, `gstreamer.decode_pipelines.auto`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Đường dẫn cấu hình sử dụng dấu chấm làm dấu phân cách. Ví dụ: `system.max_running_instances`, `system.web_server`, `gstreamer.decode_pipelines.auto` Lưu ý: Dấu gạch chéo KHÔNG được hỗ trợ trong tham số đường dẫn (sử dụng tham số truy vấn thay thế)  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phần cấu hình |
| 400 | Yêu cầu không hợp lệ (đường dẫn trống) |
| 404 | Không tìm thấy phần cấu hình |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/config/{path}

**Summary:** CORS preflight cho đường dẫn cấu hình

**Operation ID:** `configPathOptions`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Đường dẫn cấu hình |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PATCH` /v1/core/config/{path}

**Summary:** Cập nhật phần cấu hình (tham số đường dẫn)

**Operation ID:** `updateConfigSection`

Cập nhật một phần cụ thể của cấu hình hệ thống tại đường dẫn đã cho.

**Định dạng Đường dẫn:**
- Sử dụng dấu chấm `.` làm dấu phân cách (ví dụ: `system.max_running_instances`)
- Dấu gạch chéo `/` với mã hóa URL (`%2F`) KHÔNG được hỗ trợ

**Khuyến nghị:** Sử dụng tham số truy vấn thay thế: `PATCH /v1/core/config?path=system/max_running_instances`

Chỉ các trường được cung cấp trong phần sẽ được cập nhật.
Cấu hình được lưu vào file cấu hình sau khi cập nhật.

**Tự động Khởi động lại:**
- Theo mặc định, máy chủ sẽ KHÔNG tự động khởi động lại sau khi thay đổi cấu hình
- Để bật khởi động lại tự động, đặt `auto_restart=true` làm tham số truy vấn hoặc trong body JSON
- Khi `auto_restart=true`, máy chủ sẽ khởi động lại sau 3 giây nếu cấu hình web server thay đổi

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Đường dẫn cấu hình sử dụng dấu chấm làm dấu phân cách. Examples: `system.max_running_instances`, `system.web_server` Note: Forward slashes are NOT supported in path parameter (use query parameter instead)  |

| auto_restart | query | No | Nếu đặt là `true`, máy chủ sẽ tự động khởi động lại sau khi cập nhật cấu hình. Mặc định là `false` (không tự động khởi động lại). Cũng có thể đặt trong body JSON như trường `auto_restart`.  |

**Request body:**

- **application/json**
  - type: `object` — Giá trị phần cấu hình để cập nhật

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phần cấu hình đã được cập nhật thành công |
| 400 | Yêu cầu không hợp lệ hoặc xác thực thất bại |
| 500 | Lỗi máy chủ |



## Core

### `GET` /hls/{instanceId}/segment_{segmentId}.ts

**Summary:** Lấy segment HLS

**Operation ID:** `getHlsSegment`

Lấy file segment video HLS (.ts) cho một instance. Endpoint phục vụ từng file segment được tham chiếu trong playlist HLS. Segment được tạo tự động khi bật đầu ra HLS cho instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| segmentId | path | Yes | ID segment (ví dụ: "0", "1", "2") |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | File segment video HLS |
| 404 | Không tìm thấy instance, HLS chưa bật, hoặc không tìm thấy segment |
| 500 | Lỗi máy chủ |


### `GET` /hls/{instanceId}/stream.m3u8

**Summary:** Lấy playlist HLS

**Operation ID:** `getHlsPlaylist`

Lấy file playlist HLS (HTTP Live Streaming) (.m3u8) cho một instance. Endpoint phục vụ file playlist HLS mà client dùng để phát video từ instance. Playlist chứa tham chiếu tới các segment video (.ts) được tạo tự động.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | File playlist HLS |
| 404 | Không tìm thấy instance hoặc HLS chưa bật |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/endpoints

**Summary:** Lấy thống kê endpoint

**Operation ID:** `getEndpointsStats`

Trả về thống kê cho mỗi endpoint API bao gồm số lượng yêu cầu, thời gian phản hồi,
tỷ lệ lỗi và các số liệu hiệu suất khác.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Thống kê endpoint |


### `GET` /v1/core/health

**Summary:** Kiểm tra trạng thái sức khỏe

**Operation ID:** `getHealth`

Trả về trạng thái sức khỏe của dịch vụ API

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Dịch vụ đang hoạt động tốt |
| 500 | Dịch vụ không hoạt động bình thường |


### `GET` /v1/core/license/check

**Summary:** Kiểm tra tính hợp lệ license

**Operation ID:** `checkLicense`

Kiểm tra license có hợp lệ và đang hoạt động hay không

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Kết quả kiểm tra license |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/license/check

**Summary:** CORS preflight cho kiểm tra license

**Operation ID:** `checkLicenseOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/license/info

**Summary:** Lấy thông tin license

**Operation ID:** `getLicenseInfo`

Lấy thông tin chi tiết về license hiện tại

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Thông tin license |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/license/info

**Summary:** CORS preflight cho thông tin license

**Operation ID:** `getLicenseInfoOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/metrics

**Summary:** Lấy số liệu định dạng Prometheus

**Operation ID:** `getMetrics`

Trả về số liệu hệ thống ở định dạng Prometheus để giám sát và cảnh báo.
Endpoint này thường được sử dụng bởi các scraper Prometheus.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Số liệu định dạng Prometheus |


### `GET` /v1/core/system/config

**Summary:** Lấy system configuration entities

**Operation ID:** `coreGetSystemConfigV1`

Trả về system configuration entities với fieldId, displayName, type, value, group, và availableValues

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Các thực thể cấu hình hệ thống |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/system/config

**Summary:** CORS preflight cho system config

**Operation ID:** `coreSystemConfigOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/core/system/config

**Summary:** Cập nhật system configuration

**Operation ID:** `corePutSystemConfigV1`

Cập nhật system configuration entities

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SystemConfigUpdateRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình đã được cập nhật thành công |
| 406 | Not Acceptable - Cấu hình không hợp lệ |


### `GET` /v1/core/system/decoders

**Summary:** Lấy thông tin decoders có sẵn

**Operation ID:** `coreGetDecodersV1`

Trả về thông tin về hardware và software decoders có sẵn trong hệ thống

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Thông tin decoders |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/system/decoders

**Summary:** CORS preflight cho system decoders

**Operation ID:** `coreSystemDecodersOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/system/info

**Summary:** Lấy thông tin phần cứng hệ thống

**Operation ID:** `getSystemInfo`

Trả về thông tin phần cứng chi tiết bao gồm CPU, GPU, RAM, Disk, Mainboard, OS và Battery

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Thông tin phần cứng hệ thống |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/system/info

**Summary:** CORS preflight cho thông tin hệ thống

**Operation ID:** `getSystemInfoOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/system/preferences

**Summary:** Lấy system preferences

**Operation ID:** `coreGetPreferencesV1`

Trả về system preferences từ rtconfig.json. Các settings này ảnh hưởng đến UI của VMS plugins và Web Panel.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | System preferences |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/system/preferences

**Summary:** CORS preflight cho system preferences

**Operation ID:** `coreSystemPreferencesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/system/registry

**Summary:** Lấy registry key value

**Operation ID:** `coreGetRegistryKeyValueV1`

Trả về registry key value từ hệ thống

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| key | query | Yes | Đường dẫn khóa registry |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Giá trị cặp khóa registry |
| 404 | Registry key không tìm thấy |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/system/registry

**Summary:** CORS preflight cho system registry

**Operation ID:** `coreSystemRegistryOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `OPTIONS` /v1/core/system/shutdown

**Summary:** CORS preflight cho system shutdown

**Operation ID:** `coreSystemShutdownOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/system/shutdown

**Summary:** Shutdown hệ thống

**Operation ID:** `corePostShutdownV1`

Khởi động shutdown hệ thống

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Shutdown đã được khởi động |


### `GET` /v1/core/system/status

**Summary:** Lấy trạng thái hệ thống

**Operation ID:** `getSystemStatus`

Trả về trạng thái hệ thống hiện tại bao gồm mức sử dụng CPU, mức sử dụng RAM, tải trung bình và thời gian hoạt động

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Thông tin trạng thái hệ thống |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/system/status

**Summary:** CORS preflight cho trạng thái hệ thống

**Operation ID:** `getSystemStatusOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/version

**Summary:** Lấy thông tin phiên bản

**Operation ID:** `getVersion`

Trả về thông tin phiên bản của dịch vụ API

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Thông tin phiên bản |


### `GET` /v1/core/watchdog

**Summary:** Lấy trạng thái watchdog

**Operation ID:** `getWatchdogStatus`

Trả về thống kê watchdog và giám sát sức khỏe bao gồm kiểm tra sức khỏe instance,
số lần khởi động lại và thông tin giám sát.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Thông tin trạng thái watchdog |



## Fonts

### `GET` /v1/core/font/list

**Summary:** Liệt kê file font đã tải lên

**Operation ID:** `listFonts`

Returns a list of font files that have been uploaded to the server.
- If `directory` parameter is not specified: Recursively searches and lists all font files
  in the entire fonts directory tree (including all subdirectories).
- If `directory` parameter is specified: Lists only files in that specific directory
  (non-recursive, does not search subdirectories).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Subdirectory path to list files from (e.g., "projects/myproject/fonts", "themes/custom"). If specified, only lists files in that directory (non-recursive). If not specified, recursively searches all subdirectories.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách file font |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/font/upload

**Summary:** CORS preflight cho tải font

**Operation ID:** `uploadFontOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/font/upload

**Summary:** Tải lên file font

**Operation ID:** `uploadFont`

Tải file font (TTF, OTF, WOFF, WOFF2, v.v.) lên máy chủ. File lưu trong thư mục fonts. Có thể chỉ định thư mục con qua tham số query `directory`. Thư mục được tạo tự động nếu chưa tồn tại. Nếu không chỉ định, file lưu tại thư mục fonts gốc.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Subdirectory path where the font file should be saved. Use forward slashes to separate directory levels (e.g., "projects/myproject/fonts", "themes/custom", "languages/vietnamese"). The directory will be created automatically if it doesn't exist. If not specified, files will be saved in the base fonts directory.  |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **files**: array — File font cần tải lên. Có thể chọn nhiều file (.ttf, .otf, .woff, .woff2, .eot, .ttc)
    - **file**: string — Một file font tải lên (thay thế cho mảng files)
- **application/octet-stream**
  - type: `string` — File font dạng dữ liệu nhị phân

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tải font lên thành công |
| 400 | Yêu cầu không hợp lệ (thiếu file, định dạng không hợp lệ, v.v.) |
| 401 | Chưa xác thực (thiếu hoặc API key không hợp lệ) |
| 409 | File đã tồn tại |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/font/{fontName}

**Summary:** Xóa file font

**Operation ID:** `deleteFont`

Xóa file font khỏi máy chủ. Có thể chỉ định thư mục con qua tham số query `directory` nếu file nằm trong thư mục con.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| fontName | path | Yes | Tên file font cần xóa |

| directory | query | No | Subdirectory path where the font file is located (e.g., "projects/myproject/fonts", "themes/custom"). If not specified, the file is assumed to be in the base fonts directory.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa file font thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy file font |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/font/{fontName}

**Summary:** CORS preflight cho thao tác font

**Operation ID:** `fontOptionsHandler`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/core/font/{fontName}

**Summary:** Đổi tên file font

**Operation ID:** `renameFont`

Đổi tên file font trên máy chủ. Có thể chỉ định thư mục con qua tham số query `directory` nếu file nằm trong thư mục con.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| fontName | path | Yes | Tên hiện tại của file font cần đổi tên |

| directory | query | No | Subdirectory path where the font file is located (e.g., "projects/myproject/fonts", "themes/custom"). If not specified, the file is assumed to be in the base fonts directory.  |

**Request body:**

- **application/json**
  - type: `object`
    - **newName**: string — Tên mới cho file font (phải có phần mở rộng font hợp lệ)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã đổi tên file font thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy file font |
| 409 | Tên mới đã tồn tại |
| 500 | Lỗi máy chủ |



## Groups

### `GET` /v1/core/groups

**Summary:** Liệt kê tất cả nhóm

**Operation ID:** `listGroups`

Trả về danh sách tất cả nhóm kèm thông tin tóm tắt (tên, mô tả, số instance, metadata). Phản hồi gồm: tổng số nhóm, số nhóm mặc định, số nhóm tùy chỉnh, thông tin tóm tắt từng nhóm (ID, tên, mô tả, số instance). Dùng để xem tổng quan mà không cần lấy chi tiết từng nhóm.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách nhóm |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/groups

**Summary:** CORS preflight cho groups

**Operation ID:** `listGroupsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/groups

**Summary:** Tạo nhóm mới

**Operation ID:** `createGroup`

Tạo nhóm mới để tổ chức instance. Thuộc tính: **groupId** (bắt buộc) định danh duy nhất; **groupName** (tùy chọn) tên hiển thị; **description** (tùy chọn) mô tả. Group ID phải duy nhất
- Group ID must match pattern: `^[A-Za-z0-9_-]+$`
- Group name must match pattern: `^[A-Za-z0-9 -_]+$`

**Persistence:**
- Groups are automatically saved to storage
- Group files are stored in `/var/lib/omniapi/groups` (configurable via `GROUPS_DIR` environment variable)

**Returns:** The created group information including timestamps.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateGroupRequest
  - example: `{'groupId': 'cameras', 'groupName': 'Security Cameras', 'description': 'Nhóm cho instance camera an ninh'}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo nhóm thành công |
| 400 | Yêu cầu không hợp lệ (validation thất bại hoặc nhóm đã tồn tại) |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/groups/{groupId}

**Summary:** Xóa nhóm

**Operation ID:** `deleteGroup`

Xóa nhóm khỏi hệ thống (vĩnh viễn). Điều kiện: nhóm tồn tại, không phải nhóm mặc định, không read-only, không còn instance nào (cần chuyển hoặc xóa instance trước)

**Behavior:**
- Group configuration will be removed from memory
- Group file will be deleted from storage
- All resources associated with the group will be released

**Note:** This operation cannot be undone. Once deleted, the group must be recreated using the create group endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Định danh nhóm (groupId) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa nhóm thành công |
| 400 | Xóa thất bại (nhóm còn instance, là nhóm mặc định, hoặc read-only) |
| 404 | Không tìm thấy nhóm |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/groups/{groupId}

**Summary:** Lấy chi tiết nhóm

**Operation ID:** `getGroup`

Trả về thông tin chi tiết của một nhóm: cấu hình (ID, tên, mô tả), metadata (isDefault, readOnly, timestamps), số instance, danh sách instance IDs trong nhóm. Nhóm phải tồn tại; nếu không tìm thấy trả về 404.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Định danh nhóm (groupId) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Chi tiết nhóm |
| 404 | Không tìm thấy nhóm |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/groups/{groupId}

**Summary:** CORS preflight cho group

**Operation ID:** `getGroupOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/core/groups/{groupId}

**Summary:** Cập nhật nhóm

**Operation ID:** `updateGroup`

Cập nhật thông tin nhóm. Chỉ các trường gửi lên được cập nhật (groupName, description). Nhóm phải tồn tại, không read-only; group ID không đổi. Timestamp cập nhật tự động.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Định danh nhóm (groupId) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateGroupRequest
  - example: `{'groupName': 'Updated Group Name', 'description': 'Mô tả đã cập nhật'}`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật nhóm thành công |
| 400 | Yêu cầu không hợp lệ (validation thất bại hoặc nhóm read-only) |
| 404 | Không tìm thấy nhóm |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/groups/{groupId}/instances

**Summary:** Lấy danh sách instance trong nhóm

**Operation ID:** `getGroupInstances`

Trả về danh sách instance thuộc một nhóm: group ID, danh sách instance (ID, tên hiển thị, solution, trạng thái chạy), tổng số instance. Nhóm phải tồn tại; không tìm thấy trả về 404.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Định danh nhóm (Group ID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách instance trong nhóm |
| 404 | Không tìm thấy nhóm |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/groups/{groupId}/instances

**Summary:** CORS preflight cho instance nhóm

**Operation ID:** `getGroupInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |



## Instances

### `DELETE` /api/v1/instances/{instance_id}/fps

**Summary:** Đặt lại FPS về mặc định

**Operation ID:** `resetInstanceFps`

Đặt lại cấu hình FPS của instance về giá trị mặc định (5 FPS).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instance_id | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã đặt lại cấu hình FPS về mặc định |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /api/v1/instances/{instance_id}/fps

**Summary:** Lấy cấu hình FPS hiện tại

**Operation ID:** `getInstanceFps`

Lấy cấu hình FPS hiện tại của một instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instance_id | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình FPS hiện tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /api/v1/instances/{instance_id}/fps

**Summary:** CORS preflight cho các endpoint FPS

**Operation ID:** `instanceFpsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /api/v1/instances/{instance_id}/fps

**Summary:** Thiết lập cấu hình FPS

**Operation ID:** `setInstanceFps`

Thiết lập hoặc cập nhật cấu hình FPS cho một instance. Giá trị FPS phải là số nguyên dương (lớn hơn 0).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instance_id | path | Yes | Định danh instance (UUID) |

**Request body:**

- **application/json**
  - type: `object`
    - **fps**: integer — Tốc độ xử lý frame mong muốn (phải lớn hơn 0)
  - example: `{'fps': 10}`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật cấu hình FPS thành công |
| 400 | Yêu cầu không hợp lệ - Giá trị FPS không hợp lệ (âm hoặc bằng 0) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/instance

**Summary:** Xóa tất cả instance

**Operation ID:** `deleteAllInstances`

Xóa tất cả instance trong hệ thống (vĩnh viễn), dừng pipeline nếu đang chạy. Các instance được xóa đồng thời; instance đang chạy sẽ bị dừng trước; cấu hình và file cấu hình bị xóa; tài nguyên được giải phóng. Phản hồi gồm tóm tắt và kết quả từng instance (thành công/thất bại). Thao tác không thể hoàn tác.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã hoàn thành thao tác xóa tất cả instance |
| 500 | Lỗi máy chủ |
| 503 | Dịch vụ không khả dụng (registry bận) |


### `GET` /v1/core/instance

**Summary:** Liệt kê tất cả instance

**Operation ID:** `listInstances`

Trả về danh sách tất cả instance AI kèm thông tin tóm tắt: trạng thái chạy, solution, cấu hình cơ bản. Phản hồi gồm tổng số instance, số đang chạy, số đã dừng, và thông tin tóm tắt từng instance (ID, tên hiển thị, trạng thái, solution). Dùng để xem tổng quan mà không cần lấy chi tiết từng instance.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance

**Summary:** CORS preflight cho liệt kê instance

**Operation ID:** `listInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance

**Summary:** Tạo instance AI mới

**Operation ID:** `createInstance`

Tạo và đăng ký instance AI mới với cấu hình chỉ định. Trả về instance ID (UUID) dùng để điều khiển instance. **Build pipeline bất đồng bộ (khi có solution):** API trả 201 ngay khi instance được đăng ký; pipeline được build nền (có thể 30+ giây). Response có **building**, **status**, **message**: building=true, status="building" — đang build, chưa gọi Start; building=false, status="ready" — đã sẵn sàng, có thể Start. - To know when the instance is ready, poll **GET /v1/core/instance/{instanceId}** until **status** is **"ready"** (or **building** is false), or wait for the build to complete before calling Start. If Start is called while **building** is true, the API returns an error: "Pipeline is still being built in background".

**Configuration Options:**
- **Basic Settings:** name, group, solution, persistent mode
- **Performance:** frame rate limits, input pixel limits, input orientation
- **Detection:** detector mode, sensitivity levels, sensor modality
- **Processing Modes:** metadata mode, statistics mode, diagnostics mode, debug mode
- **Auto Management:** auto-start, auto-restart, blocking readahead queue
- **Solution Parameters:** RTSP_URL, MODEL_PATH, FILE_PATH, RTMP_URL, etc. (via additionalParams)

**Solution Types:**
- `face_detection`: Face detection with RTSP input
- `face_detection_file`: Face detection with file input
- `object_detection`: Object detection (YOLO)
- `face_detection_rtmp`: Face detection with RTMP streaming
- Custom solutions registered via solution management endpoints

**Persistence:**
- If `persistent: true`, the instance will be saved to a JSON file in the instances directory
- Persistent instances are automatically loaded when the server restarts
- Instance configuration files are stored in `/var/lib/omniapi/instances` (configurable via `INSTANCES_DIR` environment variable)

**Auto Start:** Nếu autoStart=true, instance sẽ tự khởi động sau khi build pipeline xong (chạy nền). **Returns:** Thông tin instance đã tạo (UUID); khi có solution, response có building, status, message để client biết instance đã sẵn sàng Start chưa.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateInstanceRequest

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Instance registered successfully. When a solution is provided, the pipeline is built asynchronously; the response may include **building: true** and **status: "building"**. Poll GET /v1/core/instance/{instanceId} until **status** is **"ready"** before calling Start, or wait for the build to complete.  |
| 400 | Yêu cầu không hợp lệ (validation thất bại) |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/batch/restart

**Summary:** CORS preflight cho batch restart

**Operation ID:** `batchRestartInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/batch/restart

**Summary:** Khởi động lại nhiều instance đồng thời

**Operation ID:** `batchRestartInstances`

Khởi động lại nhiều instance đồng thời bằng cách dừng rồi khởi động. Mọi thao tác chạy song song để tối ưu hiệu năng.

**Request body:**

- **application/json**
  - type: `object`
    - **instanceIds**: array — Mảng định danh instance cần khởi động lại

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã hoàn thành thao tác khởi động lại hàng loạt |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/batch/start

**Summary:** CORS preflight cho batch start

**Operation ID:** `batchStartInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/batch/start

**Summary:** Khởi động nhiều instance đồng thời

**Operation ID:** `batchStartInstances`

Khởi động nhiều instance đồng thời. Các thao tác chạy song song để tối ưu performance.

**Behavior:**
- All instances are started in parallel, not sequentially
- Each instance is started independently
- Results are returned for each instance (success or failure)
- Instances that are already running will be marked as successful

**Request Format:**
- Provide an array of instance IDs in the `instanceIds` field
- All instance IDs must exist in the system

**Response:**
- Returns detailed results for each instance
- Includes total count, success count, and failed count
- Each result includes the instance ID, success status, running status, and any error messages

**Use Cases:**
- Start multiple instances after server restart
- Batch operations for managing multiple cameras or streams
- Efficiently start a group of related instances

**Note:** Starting multiple instances simultaneously may consume significant system resources. Monitor system performance when starting many instances at once.

**Request body:**

- **application/json**
  - type: `object`
    - **instanceIds**: array — Mảng định danh instance cần khởi động

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã hoàn thành thao tác khởi động hàng loạt |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/batch/stop

**Summary:** CORS preflight cho batch stop

**Operation ID:** `batchStopInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/batch/stop

**Summary:** Dừng nhiều instance đồng thời

**Operation ID:** `batchStopInstances`

Dừng nhiều instance đồng thời. Các thao tác chạy song song để tối ưu performance.

**Behavior:**
- All instances are stopped in parallel, not sequentially
- Each instance is stopped independently
- Results are returned for each instance (success or failure)
- Instances that are already stopped will be marked as successful

**Request Format:**
- Provide an array of instance IDs in the `instanceIds` field
- All instance IDs must exist in the system

**Response:**
- Returns detailed results for each instance
- Includes total count, success count, and failed count
- Each result includes the instance ID, success status, running status, and any error messages

**Use Cases:**
- Stop multiple instances for maintenance
- Batch operations for managing multiple cameras or streams
- Efficiently stop a group of related instances

**Note:** Stopping multiple instances simultaneously will release system resources. This is useful for managing system load.

**Request body:**

- **application/json**
  - type: `object`
    - **instanceIds**: array — Mảng định danh instance cần dừng

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã hoàn thành thao tác dừng hàng loạt |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/quick

**Summary:** CORS preflight cho tạo instance nhanh

**Operation ID:** `createQuickInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/quick

**Summary:** Tạo instance nhanh

**Operation ID:** `createQuickInstance`

Tạo instance AI mới với tham số đơn giản. Tự động ánh xạ loại solution sang solution ID và cung cấp giá trị mặc định. Endpoint dùng để tạo instance nhanh với cấu hình tối thiểu: - Ánh xạ solution types (ví dụ "face_detection", "ba_crossline") sang solution IDs - Điền giá trị mặc định cho tham số thiếu - Chuyển đường dẫn dev sang production

**Request body:**

- **application/json**
  - type: `object`
    - **name**: string — Tên hiển thị instance
    - **solutionType**: string — Loại solution (vd. \"face_detection\", \"ba_crossline\", \"ba_jam\", \"ba_stop\")
    - **group**: string — Tên nhóm instance
    - **persistent**: boolean — Có lưu cấu hình instance hay không
    - **autoStart**: boolean — Có tự động khởi động instance hay không
    - **input**: object — 
    - **inputType**: string — Loại input (thay thế cho input.type)
    - **output**: object — 
    - **outputType**: string — Loại output (thay thế cho output.type)

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo instance thành công |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/status/summary

**Summary:** Lấy tóm tắt trạng thái instance

**Operation ID:** `getStatusSummary`

Trả về tóm tắt trạng thái instance: tổng số instance đã cấu hình, số đang chạy, số đã dừng.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Tóm tắt trạng thái instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/status/summary

**Summary:** CORS preflight cho endpoint tóm tắt trạng thái

**Operation ID:** `statusSummaryOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `DELETE` /v1/core/instance/{instanceId}

**Summary:** Xóa một instance

**Operation ID:** `deleteInstance`

Xóa instance và dừng pipeline nếu đang chạy. Thao tác xóa vĩnh viễn instance khỏi the system.

**Behavior:**
- If the instance is currently running, it will be stopped first
- The instance configuration will be removed from memory
- If the instance is persistent, its configuration file will be deleted from storage
- All resources associated with the instance will be released

**Prerequisites:**
- Instance must exist
- Instance must not be read-only (system instances cannot be deleted)

**Note:** This operation cannot be undone. Once deleted, the instance must be recreated using the create instance endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa instance thành công |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}

**Summary:** Lấy thông tin chi tiết instance

**Operation ID:** `getInstance`

Trả về thông tin chi tiết của một instance: cấu hình đầy đủ, trạng thái, runtime information.

The response includes:
- Instance configuration (display name, group, solution, etc.)
- Runtime status (running, loaded, FPS, etc.)
- Configuration settings (detector mode, sensitivity, frame rate limits, etc.)
- Input/Output configuration
- Originator information

The instance must exist. If the instance is not found, a 404 error will be returned.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Chi tiết instance |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}

**Summary:** CORS preflight cho cập nhật instance

**Operation ID:** `updateInstanceOptions`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PATCH` /v1/core/instance/{instanceId}

**Summary:** Cập nhật instance với dữ liệu một phần (patch)

**Operation ID:** `corePatchInstanceV1`

Cập nhật instance với dữ liệu một phần (tương tự PUT nhưng dành cho partial updates) where only specific fields need to be changed.

**Difference from PUT:**
- **PATCH**: Explicitly designed for partial updates, only update provided fields
- **PUT**: Can be used for full or partial updates

**Behavior:**
- Only provided fields will be updated; other fields remain unchanged
- Supports both camelCase (field-by-field) and PascalCase (direct config) formats
- If the instance is currently running, it will be automatically restarted to apply the changes
- The instance must exist and not be read-only

**Use Cases:**
- Update a single field (e.g., just the name)
- Update a few related fields
- Partial configuration updates

**Example:**
Update only the instance name:
```json
{
"name": "Updated Instance Name"
}
```

Update name and frame rate limit:
```json
{
"name": "Updated Instance Name",
"frameRateLimit": 30
}
```

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateInstanceRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cập nhật instance thành công |
| 400 | Yêu cầu không hợp lệ (lỗi phân tích hoặc tham số không hợp lệ) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/core/instance/{instanceId}

**Summary:** Cập nhật thông tin instance

**Operation ID:** `updateInstance`

Cập nhật cấu hình instance. Chỉ các trường gửi lên được cập nhật. Hỗ trợ hai kiểu update formats:

**Update Format 1: Field-by-field (camelCase)**
- Update individual fields like `name`, `group`, `autoStart`, `frameRateLimit`, etc.
- Use `additionalParams` to update solution-specific parameters (RTSP_URL, MODEL_PATH, etc.)

**Update Format 2: Direct Config (PascalCase)**
- Update using PascalCase format matching instance config file
- Can update `Input`, `Output`, `Detector`, `Zone`, etc. directly

**Behavior:**
- If the instance is currently running, it will be automatically restarted to apply the changes
- If the instance is not running, changes will take effect when the instance is started
- The instance must exist and not be read-only
- Only provided fields will be updated; other fields remain unchanged

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateInstanceRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật instance thành công |
| 400 | Yêu cầu không hợp lệ hoặc validation thất bại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/classes

**Summary:** Lấy các class của instance

**Operation ID:** `getInstanceClasses`

Lấy danh sách class nhận diện mà instance hỗ trợ

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách class được hỗ trợ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/config

**Summary:** Lấy cấu hình instance

**Operation ID:** `getConfig`

Trả về cấu hình của instance. Endpoint cung cấp các thiết lập cấu hình instance, which are different from runtime state. Configuration includes settings like AutoRestart, AutoStart, Detector settings, Input configuration, etc.

**Note:** This returns the configuration format, not the runtime state. For runtime state information, use the instance details endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình instance |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/config

**Summary:** CORS preflight cho thiết lập config

**Operation ID:** `setConfigOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/config

**Summary:** Đặt giá trị config tại đường dẫn cụ thể

**Operation ID:** `setConfig`

Đặt giá trị cấu hình tại đường dẫn chỉ định. Đường dẫn là key trong cấu hình; với cấu hình lồng nhau structures, it will be nested keys separated by forward slashes "/".

**Note:** This operation will overwrite the value at the given path.

**Path Examples:**
- Simple path: `"DisplayName"` - Sets top-level DisplayName
- Nested path: `"Output/handlers/Mqtt"` - Sets Mqtt handler in Output.handlers

**jsonValue Format:**
- For string values: `"\"my string\""` (escaped JSON string)
- For object values: `"{\"key\":\"value\"}"` (escaped JSON object)

The instance must exist and not be read-only. If the instance is currently running, it will be automatically restarted to apply the changes.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SetConfigRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã thiết lập giá trị cấu hình thành công |
| 400 | Yêu cầu không hợp lệ (đầu vào sai hoặc thiếu trường bắt buộc) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/consume_events

**Summary:** Lấy events từ instance

**Operation ID:** `coreConsumeEventsV1`

Lấy events từ hàng đợi events của instance. Events được publish bởi instance processing pipeline và có thể được lấy qua endpoint này.
**Định dạng Event:** - Mỗi event có `dataType` (ví dụ: "detection", "tracking", "analytics") và `jsonObject` (chuỗi JSON đã serialize) - Events tuân theo schema tại https://bin.cvedia.com/schema/
**Hành vi:** - Trả về tất cả events có sẵn từ hàng đợi - Events sẽ bị xóa khỏi hàng đợi sau khi lấy - Trả về 204 No Content nếu không có events - Trả về 200 OK với mảng events nếu có events

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Lấy events thành công |
| 204 | Không có events |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/consume_events

**Summary:** CORS preflight cho endpoint consume events

**Operation ID:** `consumeEventsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/instance/{instanceId}/frame

**Summary:** Lấy frame cuối từ instance

**Operation ID:** `getLastFrame`

Trả về frame đã xử lý cuối cùng từ instance đang chạy. Frame mã hóa JPEG và trả về dạng base64-encoded string.

**Features:**
- Frame is captured from the pipeline after processing (includes OSD/detection overlays)
- Frame is automatically cached when pipeline processes new frames
- Frame cache is automatically cleaned up when instance stops or is deleted

**Important Notes:**
- Frame capture only works if the pipeline has an `app_des_node`
- If pipeline doesn't have `app_des_node`, frame will be empty string
- Instance must be running to have a frame
- Frame is cached automatically each time pipeline processes a new frame

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy frame cuối thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/frame

**Summary:** CORS preflight cho lấy frame cuối

**Operation ID:** `getLastFrameOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `OPTIONS` /v1/core/instance/{instanceId}/input

**Summary:** CORS preflight cho thiết lập input

**Operation ID:** `setInstanceInputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/input

**Summary:** Thiết lập nguồn đầu vào cho instance

**Operation ID:** `setInstanceInput`

Thiết lập cấu hình nguồn đầu vào cho instance. Thay thế trường Input trong Instance File theo to the request.

**Input Types:**
- **RTSP**: Uses `rtsp_src` node for RTSP streams
- **HLS**: Uses `hls_src` node for HLS streams
- **Manual**: Uses `v4l2_src` node for V4L2 devices

The instance must exist and not be read-only. If the instance is currently running, it will be automatically restarted to apply the changes.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SetInputRequest
  - example: `{'type': 'Manual', 'uri': 'http://localhost'}`

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cập nhật cài đặt đầu vào thành công |
| 400 | Yêu cầu không hợp lệ (đầu vào sai hoặc thiếu trường bắt buộc) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/jams/batch

**Summary:** CORS preflight cho cập nhật hàng loạt jam zones

**Operation ID:** `batchUpdateJamsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/jams/batch

**Summary:** Cập nhật hàng loạt vùng kẹt xe (jam zones)

**Operation ID:** `batchUpdateJams`

Cập nhật nhiều vùng kẹt xe (jam zones) cho instance ba_jam trong một yêu cầu.
Endpoint này cho phép cập nhật nhiều vùng cùng lúc, hiệu quả hơn so với cập nhật từng vùng. Instance sẽ được khởi động lại tự động để áp dụng thay đổi nếu đang chạy.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Request body:**

- **application/json**
  - type: `array`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật vùng kẹt xe thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/lines/batch

**Summary:** CORS preflight cho cập nhật hàng loạt lines

**Operation ID:** `batchUpdateLinesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/lines/batch

**Summary:** Cập nhật hàng loạt đường vượt (crossing lines)

**Operation ID:** `batchUpdateLines`

Cập nhật nhiều đường vượt (crossing lines) cho instance ba_crossline trong một yêu cầu.
This endpoint allows updating multiple crossing lines at once, which is more efficient than updating them individually. The instance will be restarted automatically to apply the changes if it is currently running.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Request body:**

- **application/json**
  - type: `array`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật đường thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/load

**Summary:** CORS preflight cho nạp instance

**Operation ID:** `loadInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/load

**Summary:** Nạp instance vào bộ nhớ

**Operation ID:** `coreLoadInstanceV1`

Nạp instance vào bộ nhớ và khởi tạo state storage. Khác với start instance - loading prepares the instance and enables state management, while starting begins the processing pipeline.

**Behavior:**
- Instance must exist
- If instance is already loaded, returns 406 Not Acceptable
- Initializes state storage for the instance
- Instance can be loaded without being started (running)

**Use Cases:**
- Prepare instance for state management
- Load instance into memory before setting runtime state
- Manage memory and resources

**Note:** State can only be set/get when instance is loaded. State is cleared when instance is unloaded.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã nạp instance thành công |
| 404 | Không tìm thấy instance |
| 406 | Không thể nạp instance (đã nạp hoặc trạng thái không hợp lệ) |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/output

**Summary:** Lấy đầu ra và kết quả xử lý của instance

**Operation ID:** `getInstanceOutput`

Trả về output và kết quả xử lý thời gian thực của instance tại thời điểm yêu cầu.
This endpoint provides comprehensive information about:
- Current processing metrics (FPS, frame rate limit)
- Input source (FILE or RTSP)
- Output type and details (FILE output files or RTMP stream)
- Detection settings and processing modes
- Current status and processing state

For instances with FILE output, it includes file count, total size, latest file information, and activity status.
For instances with RTMP output, it includes RTMP and RTSP URLs.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đầu ra instance và kết quả xử lý |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/output

**Summary:** CORS preflight cho lấy output instance

**Operation ID:** `getInstanceOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `OPTIONS` /v1/core/instance/{instanceId}/output/hls

**Summary:** CORS preflight cho endpoint HLS output

**Operation ID:** `hlsOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/output/hls

**Summary:** Cấu hình HLS output cho instance

**Operation ID:** `coreSetOutputHlsV1`

Bật hoặc tắt HLS (HTTP Live Streaming) output cho instance. Khi bật, trả về URI của HLS stream.
**HLS Output:** - HLS stream có thể truy cập qua HTTP - File playlist (.m3u8) được tạo tự động - Video segments (.ts files) được tạo tự động - Stream có thể phát trong VLC, trình duyệt web, hoặc các trình phát tương thích HLS khác

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enabled**: boolean — Bật hoặc tắt HLS output

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình HLS output thành công |
| 204 | Tắt HLS output thành công |
| 404 | Không tìm thấy instance |
| 406 | Không thể thiết lập HLS output |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/output/rtsp

**Summary:** CORS preflight cho endpoint RTSP output

**Operation ID:** `rtspOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/output/rtsp

**Summary:** Cấu hình RTSP output cho instance

**Operation ID:** `coreSetOutputRtspV1`

Bật hoặc tắt RTSP (Real-Time Streaming Protocol) output cho instance tại URI được chỉ định.
**RTSP Output:** - RTSP stream có thể truy cập qua RTSP clients (VLC, ffplay, v.v.) - URI có thể được cung cấp trong request body hoặc sẽ dùng mặc định - Stream hỗ trợ nhiều RTSP clients đồng thời

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enabled**: boolean — Bật hoặc tắt RTSP output
    - **uri**: string — RTSP URI (tùy chọn, sẽ dùng mặc định nếu không cung cấp)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Cấu hình RTSP output thành công |
| 400 | Yêu cầu không hợp lệ (lỗi parsing hoặc tham số không hợp lệ) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/output/stream

**Summary:** Lấy cấu hình stream output

**Operation ID:** `getStreamOutput`

Trả về cấu hình stream output hiện tại của instance. Endpoint lấy cấu hình stream output settings including whether it is enabled and the configured URI.

**Response Format:**
- `enabled`: Boolean indicating whether stream output is enabled
- `uri`: The configured stream URI (empty string if disabled)

The URI can be in one of the following formats:
- **RTMP**: `rtmp://host:port/path/stream_key`
- **RTSP**: `rtsp://host:port/path/stream_key`
- **HLS**: `hls://host:port/path/stream_key`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình đầu ra stream |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/output/stream

**Summary:** CORS preflight cho các endpoint stream output

**Operation ID:** `streamOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/output/stream

**Summary:** Cấu hình stream/record output

**Operation ID:** `configureStreamOutput`

Cấu hình stream hoặc record output cho instance. Cho phép bật hoặc tắt output and set either a stream URI or a local path for recording.

**Record Output Mode (using `path` parameter):**
- When `enabled` is `true` and `path` is provided, video will be saved as MP4 files to the specified local directory
- The path must exist and have write permissions (will be created if it doesn't exist)
- Uses `rtmp_des_node` to push data to local stream
- Files are saved in MP4 format to the specified path

**Stream Output Mode (using `uri` parameter):**
- **RTMP**: `rtmp://host:port/path/stream_key` - For RTMP streaming (e.g., to MediaMTX, YouTube Live, etc.)
- **RTSP**: `rtsp://host:port/path/stream_key` - For RTSP streaming
- **HLS**: `hls://host:port/path/stream_key` - For HLS streaming

**Behavior:**
- When `enabled` is `true`, either `path` (for record output) or `uri` (for stream output) field is required
- When `enabled` is `false`, output is disabled
- If the instance is currently running, it will be automatically restarted to apply the changes
- The stream output uses the `rtmp_des_node` in the pipeline

**MediaMTX Setup:**
- For local streaming, ensure MediaMTX is installed and running: https://github.com/bluenviron/mediamtx
- Default MediaMTX RTMP endpoint: `rtmp://localhost:1935/live/stream`
- Default MediaMTX RTSP endpoint: `rtsp://localhost:8554/live/stream"

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ConfigureStreamOutputRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình stream output thành công |
| 400 | Yêu cầu không hợp lệ (đầu vào sai hoặc thiếu trường bắt buộc) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/preview

**Summary:** Lấy preview instance

**Operation ID:** `getInstancePreview`

Lấy ảnh/frame preview từ instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Ảnh xem trước |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/push/compressed

**Summary:** CORS preflight cho endpoint push compressed frame

**Operation ID:** `pushCompressedFrameOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/push/compressed

**Summary:** Đẩy frame đã nén vào instance

**Operation ID:** `corePostCompressedFrameV1`

Đẩy một frame đã được nén (JPEG, PNG, BMP, v.v.) vào instance để xử lý. Frame sẽ được decode bằng OpenCV và xử lý bởi instance pipeline.
**Định dạng được hỗ trợ:** - JPEG/JPEG - PNG - BMP - TIFF - WebP - Các định dạng khác được hỗ trợ bởi OpenCV `imdecode()`
**Định dạng Request:** - Content-Type: `multipart/form-data` - Field `frame`: Dữ liệu frame đã nén (binary) - Field `timestamp`: Timestamp int64 (tùy chọn, mặc định là thời gian hiện tại)
Instance phải đang chạy để nhận frames. Frames được đưa vào hàng đợi và xử lý bất đồng bộ.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **frame**: string — Dữ liệu frame đã nén (JPEG, PNG, BMP, v.v.)
    - **timestamp**: integer — Timestamp frame tính bằng milliseconds (tùy chọn)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Frame đã được đẩy thành công |
| 400 | Yêu cầu không hợp lệ (dữ liệu frame không hợp lệ hoặc thiếu trường bắt buộc) |
| 404 | Không tìm thấy instance |
| 409 | Instance hiện không đang chạy |
| 500 | Lỗi máy chủ (lỗi decode, hàng đợi đầy, v.v.) |


### `OPTIONS` /v1/core/instance/{instanceId}/push/encoded/{codecId}

**Summary:** CORS preflight cho endpoint push encoded frame

**Operation ID:** `pushEncodedFrameOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/push/encoded/{codecId}

**Summary:** Đẩy frame đã encode vào instance

**Operation ID:** `corePostEncodedFrameV1`

Đẩy một frame đã được encode (H.264/H.265) vào instance để xử lý. Frame sẽ được decode và xử lý bởi instance pipeline.
**Codec được hỗ trợ:** - `h264`: H.264/AVC - `h265`: H.265/HEVC - `hevc`: Bí danh cho h265
**Định dạng Request:** - Content-Type: `multipart/form-data` - Field `frame`: Dữ liệu frame đã encode (binary) - Field `timestamp`: Timestamp int64 (tùy chọn, mặc định là thời gian hiện tại)
Instance phải đang chạy để nhận frames. Frames được đưa vào hàng đợi và xử lý bất đồng bộ.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

| codecId | path | Yes | Định danh codec (h264, h265, hoặc hevc) |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **frame**: string — Dữ liệu frame đã encode (H.264/H.265)
    - **timestamp**: integer — Timestamp frame tính bằng milliseconds (tùy chọn)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Frame đã được đẩy thành công |
| 400 | Yêu cầu không hợp lệ (codec không được hỗ trợ, dữ liệu frame không hợp lệ, hoặc thiếu trường bắt buộc) |
| 404 | Không tìm thấy instance |
| 409 | Instance hiện không đang chạy |
| 500 | Lỗi máy chủ (lỗi decode, hàng đợi đầy, v.v.) |


### `OPTIONS` /v1/core/instance/{instanceId}/restart

**Summary:** CORS preflight cho khởi động lại instance

**Operation ID:** `restartInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/restart

**Summary:** Khởi động lại instance

**Operation ID:** `restartInstance`

Khởi động lại instance bằng cách dừng (nếu đang chạy) rồi start lại. Hữu ích để áp dụng configuration changes or recovering from errors.

**Behavior:**
- If the instance is running, it will be stopped first
- Then the instance will be started again
- This ensures a clean restart with the current configuration
- Returns the updated instance information after restart

**Use Cases:**
- Apply configuration changes that require a restart
- Recover from errors or unexpected states
- Refresh the pipeline with updated settings

**Note:** Restarting an instance may take several seconds as the pipeline is stopped and then restarted.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã khởi động lại instance thành công |
| 400 | Khởi động lại instance thất bại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `POST` /v1/core/instance/{instanceId}/start

**Summary:** Khởi động instance

**Operation ID:** `startInstance`

Khởi động pipeline của instance. Instance phải có pipeline đã cấu hình và solution hợp lệ.

**Prerequisites:**
- Instance must exist
- Instance must have a valid solution configuration
- Instance must not be read-only
- Input source must be properly configured (if required by solution)

**Behavior:**
- If the instance is already running, the request will succeed without error
- The pipeline will be built and started based on the instance's solution configuration
- Processing will begin according to the configured input source and detector settings
- Returns the updated instance information with `running: true`

**Note:** Starting an instance may take a few seconds as the pipeline is initialized.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã khởi động instance thành công |
| 400 | Khởi động instance thất bại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/state

**Summary:** Lấy trạng thái runtime của instance

**Operation ID:** `coreGetInstanceStateV1`

Trả về trạng thái runtime của instance. State khác config — state chứa thiết lập runtime that only exist when the instance is loaded, while config contains persistent settings that are stored in files.

**State vs Config:**
- **State**: Runtime settings, in-memory only, cleared when instance is unloaded
- **Config**: Persistent settings, stored in files, persists after restart

**Behavior:**
- Instance must exist and be loaded (or running)
- Returns empty object `{}` if no state has been set
- State is cleared when instance is unloaded

**Use Cases:**
- Get runtime settings and flags
- Check runtime state values
- Monitor runtime configuration

**Note:** State does not persist after unload or restart. For persistent configuration, use the config endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy trạng thái instance thành công |
| 404 | Không tìm thấy instance |
| 406 | Instance chưa được nạp |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/state

**Summary:** CORS preflight cho trạng thái instance

**Operation ID:** `instanceStateOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/state

**Summary:** Đặt giá trị trạng thái runtime tại đường dẫn cụ thể

**Operation ID:** `corePostInstanceStateV1`

Đặt giá trị runtime state tại đường dẫn chỉ định. State khác config — state chứa thiết lập runtime settings that only exist when the instance is loaded, while config contains persistent settings.

**State vs Config:**
- **State**: Runtime settings, in-memory only, cleared when instance is unloaded
- **Config**: Persistent settings, stored in files, persists after restart

**Behavior:**
- Instance must exist and be loaded (or running)
- If instance is running but not loaded, state storage will be initialized
- Path uses forward slashes "/" for nested structures
- State is cleared when instance is unloaded

**Path Examples:**
- Simple path: `"testValue"` - Sets top-level testValue
- Nested path: `"Output/handlers/Mqtt"` - Sets Mqtt handler in Output.handlers
- Deep nested: `"runtime/debug/enabled"` - Sets enabled flag in runtime.debug

**jsonValue Format:**
- For string values: `"\"my string\""` (escaped JSON string)
- For object values: `"{\"key\":\"value\"}"` (escaped JSON object)
- For number values: `"123"` or `"45.67"`
- For boolean values: `"true"` or `"false"`

**Use Cases:**
- Set runtime flags and settings
- Configure runtime behavior
- Update temporary runtime values

**Note:** State does not persist after unload or restart. For persistent configuration, use the config endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SetStateRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã thiết lập giá trị state thành công |
| 400 | Yêu cầu không hợp lệ (đầu vào sai, thiếu trường bắt buộc hoặc JSON không hợp lệ) |
| 404 | Không tìm thấy instance |
| 406 | Instance chưa được nạp |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/statistics

**Summary:** Lấy thống kê instance

**Operation ID:** `getStatistics`

Trả về thống kê thời gian thực của instance: frame đã xử lý, framerate, độ trễ, resolution, and queue information.

**Statistics Include:**
- Frames processed count
- Source framerate (FPS from source)
- Current framerate (processing FPS)
- Average latency (milliseconds)
- Input queue size
- Dropped frames count
- Resolution and format information
- Source resolution

**Important Notes:**
- Statistics are calculated in real-time
- Statistics reset to 0 when instance is restarted
- Statistics are not persisted (only calculated when requested)
- Instance must be running to get statistics

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy thống kê thành công |
| 404 | Không tìm thấy instance hoặc instance không đang chạy |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/statistics

**Summary:** CORS preflight cho lấy thống kê

**Operation ID:** `getStatisticsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/stop

**Summary:** Dừng instance

**Operation ID:** `stopInstance`

Dừng pipeline của instance, dừng mọi xử lý và giải phóng tài nguyên.

**Behavior:**
- If the instance is already stopped, the request will succeed without error
- The pipeline will be gracefully stopped and all resources will be released
- Processing will stop immediately
- Returns the updated instance information with `running: false`

**Note:** Stopping an instance may take a few seconds as the pipeline is cleaned up.

The instance configuration is preserved and can be started again using the start endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã dừng instance thành công |
| 400 | Dừng instance thất bại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/stops/batch

**Summary:** CORS preflight cho cập nhật hàng loạt stops

**Operation ID:** `batchUpdateStopsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/stops/batch

**Summary:** Cập nhật hàng loạt vùng dừng (stop zones)

**Operation ID:** `batchUpdateStops`

Cập nhật nhiều vùng dừng (stop zones) cho instance ba_stop trong một yêu cầu.
This endpoint allows updating multiple stop zones at once, which is more efficient than updating them individually. The instance will be restarted automatically to apply the changes if it is currently running.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance (UUID) |

**Request body:**

- **application/json**
  - type: `array`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật vùng dừng thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/unload

**Summary:** CORS preflight cho gỡ instance

**Operation ID:** `unloadInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/unload

**Summary:** Gỡ instance khỏi bộ nhớ

**Operation ID:** `coreUnloadInstanceV1`

Gỡ instance khỏi bộ nhớ, xóa state storage và giải phóng tài nguyên. Khác với stop an instance - unloading removes the instance from memory and clears runtime state, while stopping only halts the processing pipeline.

**Behavior:**
- Instance must exist and be loaded
- If instance is running, it will be stopped first
- State storage is cleared (all runtime state is lost)
- Resources are released

**Use Cases:**
- Free memory when instance is not needed
- Clear runtime state
- Release resources

**Note:** State is cleared when instance is unloaded. State does not persist after unload. Config (persistent configuration) is not affected.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã gỡ instance khỏi bộ nhớ thành công |
| 404 | Không tìm thấy instance |
| 406 | Instance chưa được nạp |
| 500 | Lỗi máy chủ |



## Lines Core

### `DELETE` /v1/core/instance/{instanceId}/lines

**Summary:** Xóa tất cả đường vượt (crossing lines)

**Operation ID:** `deleteAllLines`

Xóa tất cả đường vượt (crossing lines) của instance ba_crossline. Đường được gỡ ngay khỏi luồng video đang chạy, không cần khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa tất cả lines thành công |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/lines

**Summary:** Lấy tất cả đường vượt (crossing lines)

**Operation ID:** `getAllLines`

Trả về tất cả đường vượt (crossing lines) đã cấu hình cho instance ba_crossline. Đường dùng để đếm đối tượng vượt qua đường xác định trên luồng video.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách đường vượt (crossing lines) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/lines

**Summary:** CORS preflight cho các endpoint lines

**Operation ID:** `linesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/instance/{instanceId}/lines

**Summary:** Tạo một hoặc nhiều đường vượt (crossing lines)

**Operation ID:** `createLine`

Tạo một hoặc nhiều đường vượt (crossing lines) cho instance ba_crossline. Đường được vẽ trên luồng video và dùng để đếm đối tượng. **Định dạng:** Một đường — gửi một object (CreateLineRequest); Nhiều đường — gửi mảng [CreateLineRequest, ...]. **Cập nhật thời gian thực:** Đường được áp dụng ngay không cần khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

**Request body:**

- **application/json**
  - type: `object`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo đường thành công. Trả về một object đường với yêu cầu đơn, hoặc object kèm metadata với yêu cầu mảng for array request. |
| 400 | Yêu cầu không hợp lệ (tọa độ hoặc tham số sai) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** Xóa một đường vượt (crossing line) theo ID

**Operation ID:** `deleteLine`

Xóa một đường vượt (crossing line) theo ID. Đường được gỡ ngay khỏi luồng video, không cần khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| lineId | path | Yes | Định danh duy nhất của đường cần xóa |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa đường thành công |
| 404 | Không tìm thấy instance hoặc đường |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** Lấy một đường vượt (crossing line) theo ID

**Operation ID:** `getLine`

Trả về một đường vượt (crossing line) theo ID cho instance ba_crossline.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| lineId | path | Yes | Định danh duy nhất của đường cần lấy |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy đường thành công |
| 404 | Không tìm thấy instance hoặc đường |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** CORS preflight cho line endpoints

**Operation ID:** `lineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** Cập nhật một đường vượt (crossing line) theo ID

**Operation ID:** `updateLine`

Cập nhật một đường vượt (crossing line) theo ID cho instance ba_crossline. Đường được áp dụng ngay lên luồng video, không cần khởi động lại instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh duy nhất của instance |

| lineId | path | Yes | Định danh duy nhất của đường cần cập nhật |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateLineRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật đường thành công |
| 400 | Yêu cầu không hợp lệ (tọa độ hoặc tham số sai) |
| 404 | Không tìm thấy instance hoặc đường |
| 500 | Lỗi máy chủ |



## Lines SecuRT

### `OPTIONS` /v1/securt/instance/{instanceId}/line/counting

**Summary:** CORS preflight cho counting line

**Operation ID:** `createCountingLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/line/counting

**Summary:** Tạo đường đếm (counting line)

**Operation ID:** `createCountingLine`

Tạo đường đếm (counting line) mới cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CountingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo đường đếm thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/counting/{lineId}

**Summary:** CORS preflight cho counting line với ID

**Operation ID:** `createCountingLineWithIdOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/securt/instance/{instanceId}/line/counting/{lineId}

**Summary:** Tạo counting line với ID

**Operation ID:** `createCountingLineWithId`

Tạo đường đếm (counting line) với ID chỉ định cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| lineId | path | Yes | Định danh đường (line ID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CountingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo đường đếm thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Đường đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/crossing

**Summary:** CORS preflight cho crossing line

**Operation ID:** `createCrossingLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/line/crossing

**Summary:** Tạo đường vượt (crossing line)

**Operation ID:** `createCrossingLine`

Tạo đường vượt (crossing line) mới cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo đường vượt (crossing line) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/crossing/{lineId}

**Summary:** CORS preflight cho crossing line với ID

**Operation ID:** `createCrossingLineWithIdOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/securt/instance/{instanceId}/line/crossing/{lineId}

**Summary:** Tạo crossing line với ID

**Operation ID:** `createCrossingLineWithId`

Tạo đường vượt (crossing line) với ID chỉ định cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| lineId | path | Yes | Định danh đường (line ID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo đường vượt thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Đường đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/tailgating

**Summary:** CORS preflight cho tailgating line

**Operation ID:** `createTailgatingLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/line/tailgating

**Summary:** Tạo đường bám đuôi (tailgating line)

**Operation ID:** `createTailgatingLine`

Tạo đường bám đuôi (tailgating line) mới cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/TailgatingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo đường bám đuôi thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/tailgating/{lineId}

**Summary:** CORS preflight cho tailgating line với ID

**Operation ID:** `createTailgatingLineWithIdOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/securt/instance/{instanceId}/line/tailgating/{lineId}

**Summary:** Tạo tailgating line với ID

**Operation ID:** `createTailgatingLineWithId`

Tạo đường bám đuôi (tailgating line) với ID chỉ định cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| lineId | path | Yes | Định danh đường (line ID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/TailgatingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo đường bám đuôi thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Đường đã tồn tại |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/securt/instance/{instanceId}/line/{lineId}

**Summary:** Xóa line theo ID

**Operation ID:** `deleteLine`

Xóa một đường (line) theo ID

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

| lineId | path | Yes | Định danh đường (line ID) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã xóa đường thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance hoặc đường |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/{lineId}

**Summary:** CORS preflight cho line theo ID

**Operation ID:** `deleteLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `DELETE` /v1/securt/instance/{instanceId}/lines

**Summary:** Xóa tất cả lines của instance SecuRT

**Operation ID:** `deleteAllSecuRTLines`

Xóa tất cả đường phân tích (analytics lines) của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã xóa tất cả lines thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/lines

**Summary:** Lấy các đường phân tích (analytics lines) của instance SecuRT

**Operation ID:** `getSecuRTLines`

Trả về tất cả đường phân tích (analytics lines) đã cấu hình cho instance SecuRT, gồm crossing lines, counting lines, and tailgating lines.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy đường analytics thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/lines

**Summary:** CORS preflight cho lines

**Operation ID:** `getSecuRTLinesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |



## Logs

### `GET` /v1/core/log

**Summary:** Liệt kê tất cả các file log theo danh mục

**Operation ID:** `listLogFiles`

Trả về danh sách tất cả các file log được tổ chức theo danh mục (api, instance, sdk_output, general).
Mỗi danh mục chứa một mảng các file log với ngày, kích thước và đường dẫn của chúng.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách các file log theo danh mục |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/log

**Summary:** CORS preflight cho danh sách log

**Operation ID:** `listLogFilesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/log/{category}

**Summary:** Lấy log theo danh mục

**Operation ID:** `getLogsByCategory`

Trả về log từ một danh mục cụ thể với tùy chọn lọc.

**Danh mục:** api, instance, sdk_output, general

**Tham số Truy vấn:**
- `level`: Lọc theo mức log (không phân biệt chữ hoa/thường, ví dụ: INFO, ERROR, WARNING)
- `from`: Lọc log từ timestamp (định dạng ISO 8601, ví dụ: 2024-01-01T00:00:00.000Z)
- `to`: Lọc log đến timestamp (định dạng ISO 8601, ví dụ: 2024-01-01T23:59:59.999Z)
- `tail`: Chỉ lấy N dòng cuối cùng từ file log mới nhất (số nguyên)

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| category | path | Yes | Danh mục log |

| level | query | No | Lọc theo mức log (không phân biệt chữ hoa/thường) |

| from | query | No | Lọc log từ timestamp (ISO 8601) |

| to | query | No | Lọc log đến timestamp (ISO 8601) |

| tail | query | No | Chỉ lấy N dòng cuối cùng từ file log mới nhất |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Log từ danh mục đã chỉ định |
| 400 | Yêu cầu không hợp lệ (danh mục hoặc tham số không hợp lệ) |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/log/{category}

**Summary:** CORS preflight cho lấy log theo danh mục

**Operation ID:** `getLogsByCategoryOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/log/{category}/{date}

**Summary:** Lấy log theo danh mục và ngày

**Operation ID:** `getLogsByCategoryAndDate`

Trả về log từ một danh mục và ngày cụ thể với tùy chọn lọc.

**Danh mục:** api, instance, sdk_output, general
**Định dạng Ngày:** YYYY-MM-DD (ví dụ: 2024-01-01)

**Tham số Truy vấn:**
- `level`: Lọc theo mức log (không phân biệt chữ hoa/thường, ví dụ: INFO, ERROR, WARNING)
- `from`: Lọc log từ timestamp (định dạng ISO 8601, ví dụ: 2024-01-01T00:00:00.000Z)
- `to`: Lọc log đến timestamp (định dạng ISO 8601, ví dụ: 2024-01-01T23:59:59.999Z)
- `tail`: Chỉ lấy N dòng cuối cùng từ file log (số nguyên)

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| category | path | Yes | Danh mục log |

| date | path | Yes | Date in YYYY-MM-DD format |

| level | query | No | Lọc theo mức log (không phân biệt chữ hoa/thường) |

| from | query | No | Lọc log từ timestamp (ISO 8601) |

| to | query | No | Lọc log đến timestamp (ISO 8601) |

| tail | query | No | Chỉ lấy N dòng cuối cùng từ file log |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Log từ danh mục và ngày đã chỉ định |
| 400 | Bad request (invalid category, date format, or parameters) |
| 404 | Không tìm thấy file log cho ngày đã chỉ định |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/log/{category}/{date}

**Summary:** CORS preflight cho lấy log theo danh mục và ngày

**Operation ID:** `getLogsByCategoryAndDateOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |



## Models

### `GET` /v1/core/model/list

**Summary:** Liệt kê các file mô hình đã tải lên

**Operation ID:** `listModels`

Trả về danh sách các file mô hình đã được tải lên máy chủ.
- Nếu tham số `directory` không được chỉ định: Tìm kiếm đệ quy và liệt kê tất cả các file mô hình
  trong toàn bộ cây thư mục models (bao gồm tất cả các thư mục con).
- Nếu tham số `directory` được chỉ định: Chỉ liệt kê các file trong thư mục cụ thể đó
  (không đệ quy, không tìm kiếm thư mục con).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Đường dẫn thư mục con để liệt kê file từ đó (ví dụ: "projects/myproject/models", "detection/yolov8"). Nếu được chỉ định, chỉ liệt kê các file trong thư mục đó (không đệ quy). Nếu không được chỉ định, tìm kiếm đệ quy tất cả các thư mục con.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách các file mô hình |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/model/upload

**Summary:** CORS preflight cho tải lên mô hình

**Operation ID:** `uploadModelOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/model/upload

**Summary:** Tải lên file mô hình

**Operation ID:** `uploadModel`

Tải lên file mô hình (ONNX, weights, v.v.) lên máy chủ.
File sẽ được lưu trong thư mục models. Bạn có thể chỉ định thư mục con
bằng tham số truy vấn `directory` (ví dụ: `projects/myproject/models`).
Nếu thư mục không tồn tại, nó sẽ được tạo tự động.
Nếu không chỉ định thư mục, file sẽ được lưu trong thư mục models gốc.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Đường dẫn thư mục con nơi file mô hình sẽ được lưu. Use forward slashes to separate directory levels (e.g., "projects/myproject/models", "detection/yolov8", "classification/resnet50"). The directory will be created automatically if it doesn't exist. If not specified, files will be saved in the base models directory.  |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **files**: array — File mô hình để tải lên. Chọn nhiều file để tải lên cùng lúc (.onnx, .rknn, .weights, .cfg, .pt, .pth, .pb, .tflite)
    - **file**: string — Một file mô hình để tải lên (thay thế cho mảng files)
- **application/octet-stream**
  - type: `string` — File mô hình dưới dạng dữ liệu nhị phân

**Responses:**

| Code | Description |
|------|-------------|
| 201 | File mô hình đã được tải lên thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | File đã tồn tại |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/model/{modelName}

**Summary:** Xóa một file mô hình

**Operation ID:** `deleteModel`

Xóa một file mô hình khỏi máy chủ.
Bạn có thể chỉ định thư mục con bằng tham số truy vấn `directory` nếu file nằm trong thư mục con.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| modelName | path | Yes | Tên file mô hình cần xóa |

| directory | query | No | Đường dẫn thư mục con nơi file mô hình được đặt (ví dụ: "projects/myproject/models", "detection/yolov8"). Nếu không được chỉ định, file được giả định nằm trong thư mục models cơ sở.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | File mô hình đã được xóa thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy file mô hình |
| 500 | Lỗi máy chủ |


### `PUT` /v1/core/model/{modelName}

**Summary:** Đổi tên một file mô hình

**Operation ID:** `renameModel`

Đổi tên một file mô hình trên máy chủ.
Bạn có thể chỉ định thư mục con bằng tham số truy vấn `directory` nếu file nằm trong thư mục con.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| modelName | path | Yes | Tên hiện tại của file mô hình cần đổi tên |

| directory | query | No | Đường dẫn thư mục con nơi file mô hình được đặt (ví dụ: "projects/myproject/models", "detection/yolov8"). Nếu không được chỉ định, file được giả định nằm trong thư mục models cơ sở.  |

**Request body:**

- **application/json**
  - type: `object`
    - **newName**: string — Tên mới cho file mô hình (phải có phần mở rộng file mô hình hợp lệ)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | File mô hình đã được đổi tên thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy file mô hình |
| 409 | Tên mới đã tồn tại |
| 500 | Lỗi máy chủ |



## Node

### `GET` /v1/core/node

**Summary:** Liệt kê tất cả node cấu hình sẵn

**Operation ID:** `listNodes`

Trả về danh sách tất cả node cấu hình sẵn trong node pool. Có thể lọc theo trạng thái available và category.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| available | query | No | Lọc chỉ hiển thị node khả dụng (không đang dùng) |

| category | query | No | Lọc node theo danh mục |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy danh sách node thành công |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/node

**Summary:** CORS preflight cho nodes

**Operation ID:** `listNodesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/node

**Summary:** Tạo node cấu hình sẵn mới

**Operation ID:** `createNode`

Tạo node cấu hình sẵn mới từ template với tham số chỉ định

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateNodeRequest

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo node thành công |
| 400 | Yêu cầu không hợp lệ (thiếu trường bắt buộc hoặc tham số không hợp lệ) |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/node/build-solution

**Summary:** CORS preflight cho build solution

**Operation ID:** `buildSolutionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/node/build-solution

**Summary:** Build solution từ các node

**Operation ID:** `buildSolutionFromNodes`

Build cấu hình solution từ các node cấu hình sẵn đã chọn.
This endpoint allows creating a solution by combining multiple pre-configured nodes. The solution will be created with the specified nodes and their configurations.

**Request body:**

- **application/json**
  - type: `object`
    - **nodes**: array — Mảng định danh node đưa vào solution
    - **name**: string — Tên solution
    - **description**: string — Mô tả solution

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã build solution thành công |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/node/preconfigured

**Summary:** Liệt kê node cấu hình sẵn

**Operation ID:** `getPreConfiguredNodes`

Lấy danh sách tất cả node cấu hình sẵn trong node pool

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách node cấu hình sẵn |
| 500 | Lỗi máy chủ |


### `POST` /v1/core/node/preconfigured

**Summary:** Tạo node cấu hình sẵn

**Operation ID:** `createPreConfiguredNode`

Tạo node cấu hình sẵn mới từ template

**Request body:**

- **application/json**
  - type: `object`
    - **templateId**: string — Định danh template dùng để tạo node
    - **parameters**: object — Tham số node

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo node thành công |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/node/preconfigured/available

**Summary:** Liệt kê node cấu hình sẵn có sẵn

**Operation ID:** `getAvailableNodes`

Lấy danh sách node cấu hình sẵn có sẵn (không đang dùng)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách node cấu hình sẵn có sẵn |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/node/stats

**Summary:** Lấy thống kê node pool

**Operation ID:** `getNodeStats`

Trả về thống kê node pool (tổng template, tổng node, node available, theo category).

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy thống kê thành công |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/node/stats

**Summary:** CORS preflight cho stats

**Operation ID:** `getNodeStatsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/node/template

**Summary:** Liệt kê tất cả node template

**Operation ID:** `listTemplates`

Trả về danh sách tất cả node template có sẵn dùng để tạo node cấu hình sẵn.

**Available Node Types:**

**Source Nodes:**
- `rtsp_src` - RTSP Source
- `file_src` - File Source
- `app_src` - App Source
- `image_src` - Image Source
- `rtmp_src` - RTMP Source
- `udp_src` - UDP Source

**Detector Nodes:**
- `yunet_face_detector` - YuNet Face Detector
- `yolo_detector` - YOLO Detector
- `yolov11_detector` - YOLOv11 Detector
- `mask_rcnn_detector` - Mask R-CNN Detector
- `openpose_detector` - OpenPose Detector
- `enet_seg` - ENet Segmentation
- `trt_yolov8_detector` - TensorRT YOLOv8 Detector (requires CVEDIX_WITH_TRT)
- `trt_yolov8_seg_detector` - TensorRT YOLOv8 Segmentation (requires CVEDIX_WITH_TRT)
- `trt_yolov8_pose_detector` - TensorRT YOLOv8 Pose (requires CVEDIX_WITH_TRT)
- `trt_yolov8_classifier` - TensorRT YOLOv8 Classifier (requires CVEDIX_WITH_TRT)
- `trt_vehicle_detector` - TensorRT Vehicle Detector (requires CVEDIX_WITH_TRT)
- `trt_vehicle_plate_detector` - TensorRT Vehicle Plate Detector (requires CVEDIX_WITH_TRT)
- `trt_vehicle_plate_detector_v2` - TensorRT Vehicle Plate Detector v2 (requires CVEDIX_WITH_TRT)
- `trt_insight_face_recognition` - TensorRT InsightFace Recognition (requires CVEDIX_WITH_TRT)
- `rknn_yolov8_detector` - RKNN YOLOv8 Detector (requires CVEDIX_WITH_RKNN)
- `rknn_yolov11_detector` - RKNN YOLOv11 Detector (requires CVEDIX_WITH_RKNN)
- `rknn_face_detector` - RKNN Face Detector (requires CVEDIX_WITH_RKNN)
- `ppocr_text_detector` - PaddleOCR Text Detector (requires CVEDIX_WITH_PADDLE)
- `face_swap` - Face Swap
- `insight_face_recognition` - InsightFace Recognition
- `mllm_analyser` - MLLM Analyser

**Processor Nodes:**
- `sface_feature_encoder` - SFace Feature Encoder
- `sort_track` - SORT Tracker
- `face_osd_v2` - Face OSD v2
- `ba_crossline` - BA Crossline
- `ba_crossline_osd` - BA Crossline OSD
- `ba_jam` - BA Jam
- `ba_jam_osd` - BA Jam OSD
- `classifier` - Classifier
- `lane_detector` - Lane Detector
- `restoration` - Restoration
- `trt_vehicle_feature_encoder` - TensorRT Vehicle Feature Encoder (requires CVEDIX_WITH_TRT)
- `trt_vehicle_color_classifier` - TensorRT Vehicle Color Classifier (requires CVEDIX_WITH_TRT)
- `trt_vehicle_type_classifier` - TensorRT Vehicle Type Classifier (requires CVEDIX_WITH_TRT)
- `trt_vehicle_scanner` - TensorRT Vehicle Scanner (requires CVEDIX_WITH_TRT)

**Destination Nodes:**
- `file_des` - File Destination
- `rtmp_des` - RTMP Destination
- `screen_des` - Screen Destination

**Broker Nodes:**
- `json_console_broker` - JSON Console Broker
- `json_enhanced_console_broker` - JSON Enhanced Console Broker
- `json_mqtt_broker` - JSON MQTT Broker (requires CVEDIX_WITH_MQTT)
- `json_kafka_broker` - JSON Kafka Broker (requires CVEDIX_WITH_KAFKA)
- `xml_file_broker` - XML File Broker
- `xml_socket_broker` - XML Socket Broker
- `msg_broker` - Message Broker
- `ba_socket_broker` - BA Socket Broker
- `embeddings_socket_broker` - Embeddings Socket Broker
- `embeddings_properties_socket_broker` - Embeddings Properties Socket Broker
- `plate_socket_broker` - Plate Socket Broker
- `expr_socket_broker` - Expression Socket Broker

All node templates are automatically imported from CVEDIX SDK nodes available in `/opt/cvedix/include/cvedix/nodes/infers`.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy danh sách template thành công |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/node/template

**Summary:** CORS preflight cho templates

**Operation ID:** `listTemplatesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/node/template/{category}

**Summary:** Lấy template theo category

**Operation ID:** `getTemplatesByCategory`

Lấy danh sách node template lọc theo category

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| category | path | Yes | Danh mục template |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách template trong danh mục |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/node/template/{templateId}

**Summary:** Lấy chi tiết template

**Operation ID:** `getTemplate`

Trả về thông tin chi tiết của một node template

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| templateId | path | Yes | Định danh template duy nhất |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy chi tiết template thành công |
| 404 | Không tìm thấy template |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/node/template/{templateId}

**Summary:** CORS preflight cho template

**Operation ID:** `getTemplateOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `DELETE` /v1/core/node/{nodeId}

**Summary:** Xóa node

**Operation ID:** `deleteNode`

Xóa node cấu hình sẵn. Chỉ node không đang được sử dụng mới có thể xóa.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| nodeId | path | Yes | Định danh node duy nhất |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa node thành công |
| 404 | Không tìm thấy node |
| 409 | Xung đột (node đang được sử dụng) |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/node/{nodeId}

**Summary:** Lấy chi tiết node

**Operation ID:** `getNode`

Trả về thông tin chi tiết của một node cấu hình sẵn

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| nodeId | path | Yes | Định danh node duy nhất |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy chi tiết node thành công |
| 404 | Không tìm thấy node |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/node/{nodeId}

**Summary:** CORS preflight cho node

**Operation ID:** `getNodeOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/core/node/{nodeId}

**Summary:** Cập nhật node

**Operation ID:** `updateNode`

Cập nhật tham số node. Chỉ node không đang được sử dụng mới có thể cập nhật.
Lưu ý: Thao tác cập nhật xóa node cũ và tạo node mới với tham số đã cập nhật.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| nodeId | path | Yes | Định danh node duy nhất |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateNodeRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật node thành công |
| 400 | Yêu cầu không hợp lệ (thiếu trường parameters) |
| 404 | Không tìm thấy node |
| 409 | Xung đột (node đang được sử dụng) |
| 500 | Lỗi máy chủ |



## ONVIF

### `OPTIONS` /v1/onvif/camera/{cameraid}/credentials

**Summary:** CORS preflight cho credentials ONVIF

**Operation ID:** `setONVIFCredentialsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/onvif/camera/{cameraid}/credentials

**Summary:** Thiết lập credentials cho một camera ONVIF

**Operation ID:** `setONVIFCredentials`

Thiết lập thông tin xác thực cho một camera ONVIF cụ thể.
**Camera ID:** - Có thể là địa chỉ IP của camera (ví dụ: "192.168.1.100") hoặc UUID - Định dạng địa chỉ IP: 0-255.0-255.0-255.0-255
**Credentials:** - Username và password là bắt buộc - Credentials được lưu trữ an toàn và được sử dụng cho các thao tác ONVIF tiếp theo - Nếu camera không được tìm thấy, có thể được đề xuất nếu phát hiện IP tương tự
**Cách sử dụng:** 1. Khám phá camera bằng POST /v1/onvif/discover 2. Lấy danh sách camera bằng GET /v1/onvif/cameras 3. Thiết lập credentials bằng endpoint này 4. Lấy streams bằng GET /v1/onvif/streams/{cameraid}

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| cameraid | path | Yes | Định danh camera (địa chỉ IP hoặc UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ONVIFCredentialsRequest
  - example: `{'username': 'admin', 'password': 'password123'}`

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Credentials đã được thiết lập thành công |
| 400 | Yêu cầu không hợp lệ (thiếu username hoặc password, JSON không hợp lệ) |
| 404 | Không tìm thấy camera |
| 500 | Lỗi máy chủ |


### `GET` /v1/onvif/cameras

**Summary:** Lấy danh sách tất cả camera ONVIF đã khám phá

**Operation ID:** `getONVIFCameras`

Lấy danh sách tất cả các camera ONVIF đã được khám phá trên mạng.
**Thông tin Camera:** - Mỗi camera bao gồm địa chỉ IP, UUID, nhà sản xuất, model và số serial - Camera được lọc dựa trên whitelist hỗ trợ (nếu được cấu hình) - Chỉ các camera vượt qua kiểm tra whitelist mới được trả về
**Cách sử dụng:** 1. Đầu tiên gọi POST /v1/onvif/discover để khám phá camera 2. Sau đó gọi endpoint này để lấy danh sách camera đã khám phá 3. Sử dụng camera ID (IP hoặc UUID) để lấy streams hoặc thiết lập credentials

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách camera ONVIF đã khám phá |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/onvif/cameras

**Summary:** CORS preflight cho camera ONVIF

**Operation ID:** `getONVIFCamerasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `OPTIONS` /v1/onvif/discover

**Summary:** CORS preflight cho khám phá ONVIF

**Operation ID:** `discoverONVIFCamerasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/onvif/discover

**Summary:** Khám phá camera ONVIF

**Operation ID:** `discoverONVIFCameras`

Khám phá các camera ONVIF trên mạng bằng giao thức WS-Discovery.
Endpoint này khởi động quá trình khám phá bất đồng bộ để tìm kiếm các camera tuân thủ ONVIF trên mạng cục bộ.
**Tham số Timeout:** - Tham số truy vấn tùy chọn `timeout` (1-30 giây, mặc định: 5) - Điều khiển thời gian chờ phản hồi từ camera trong quá trình khám phá
**Phản hồi:** - Trả về 204 No Content khi quá trình khám phá được khởi động - Sử dụng GET /v1/onvif/cameras để lấy danh sách camera đã khám phá sau khi hoàn tất

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| timeout | query | No | Thời gian chờ khám phá tính bằng giây (1-30, mặc định: 5) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Quá trình khám phá đã được khởi động thành công |
| 500 | Lỗi máy chủ |


### `GET` /v1/onvif/streams/{cameraid}

**Summary:** Lấy streams cho một camera ONVIF

**Operation ID:** `getONVIFStreams`

Lấy các video streams có sẵn cho một camera ONVIF cụ thể.
**Camera ID:** - Có thể là địa chỉ IP của camera (ví dụ: "192.168.1.100") hoặc UUID - Định dạng địa chỉ IP: 0-255.0-255.0-255.0-255
**Thông tin Stream:** - Mỗi stream bao gồm profile token, độ phân giải (width/height), frame rate (fps) và RTSP URI - Streams được lấy bằng dịch vụ ONVIF Media - Credentials của camera phải được thiết lập trước khi lấy streams (nếu yêu cầu xác thực)
**Cách sử dụng:** 1. Khám phá camera bằng POST /v1/onvif/discover 2. Lấy danh sách camera bằng GET /v1/onvif/cameras 3. Thiết lập credentials bằng POST /v1/onvif/camera/{cameraid}/credentials (nếu cần) 4. Lấy streams bằng endpoint này

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| cameraid | path | Yes | Định danh camera (địa chỉ IP hoặc UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách streams có sẵn cho camera |
| 400 | Yêu cầu không hợp lệ (thiếu hoặc không hợp lệ camera ID) |
| 404 | Không tìm thấy camera |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/onvif/streams/{cameraid}

**Summary:** CORS preflight cho streams ONVIF

**Operation ID:** `getONVIFStreamsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |



## Recognition

### `DELETE` /v1/recognition/face-database/connection

**Summary:** Xóa cấu hình kết nối face database

**Operation ID:** `deleteFaceDatabaseConnection`

Xóa cấu hình kết nối face database. Sau khi xóa, hệ thống dùng lại file mặc định face_database.txt. Hành vi: gỡ cấu hình khỏi config.json; đặt enabled=false; hệ thống chuyển sang dùng face_database.txt; nếu chưa có cấu hình thì trả về success báo không có cấu hình. Lưu ý: Thao tác này không xóa dữ liệu trong database, chỉ gỡ cấu hình kết nối. Để xóa dữ liệu trong database, dùng các endpoint xóa face.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa cấu hình kết nối database thành công |
| 500 | Lỗi máy chủ (không cập nhật được cấu hình, v.v.) |


### `GET` /v1/recognition/face-database/connection

**Summary:** Lấy cấu hình kết nối face database

**Operation ID:** `getFaceDatabaseConnection`

Lấy cấu hình kết nối face database hiện tại. Nếu chưa cấu hình hoặc đã tắt, trả về thông tin file mặc định face_database.txt. Khi đã cấu hình trả về chi tiết kết nối; khi chưa cấu hình trả về enabled=false và đường dẫn file mặc định.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy cấu hình thành công |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/recognition/face-database/connection

**Summary:** CORS preflight cho face database connection

**Operation ID:** `faceDatabaseConnectionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/recognition/face-database/connection

**Summary:** Cấu hình kết nối face database

**Operation ID:** `configureFaceDatabaseConnection`

Configures the face database connection for MySQL or PostgreSQL. This endpoint allows you to configure
a database connection to store face recognition data instead of using the default `face_database.txt` file.

**Database Support:**
- MySQL (default port: 3306)
- PostgreSQL (default port: 5432)

**Database Schema:**
The system expects two tables in the database:

1. **face_libraries** table:
   - `id` (primary key, auto-increment)
   - `image_id` (varchar 36) - Unique identifier for each face image
   - `subject` (varchar 255) - Subject/name of the person
   - `base64_image` (longtext) - Base64-encoded face image
   - `embedding` (text) - Face embedding vector (comma-separated floats)
   - `created_at` (timestamp) - Creation timestamp
   - `machine_id` (varchar 255) - Machine identifier
   - `mac_address` (varchar 255) - MAC address

2. **face_log** table:
   - `id` (primary key, auto-increment)
   - `request_type` (varchar 50) - Type of request (recognize, register, etc.)
   - `timestamp` (datetime) - Request timestamp
   - `client_ip` (varchar 45) - Client IP address
   - `request_body` (longtext) - Request body JSON
   - `response_body` (longtext) - Response body JSON
   - `response_code` (int) - HTTP response code
   - `notes` (text) - Additional notes
   - `mac_address` (varchar 255) - MAC address
   - `machine_id` (varchar 255) - Machine identifier

**Behavior:**
- If database connection is configured and enabled, the system will use the database instead of `face_database.txt`
- If `enabled: false` is sent, the database connection is disabled and the system falls back to `face_database.txt`
- Configuration is persisted in `config.json` under the `face_database` section
- The configuration takes effect immediately after being saved

**Note:** The database tables must be created manually before using this endpoint. The endpoint only configures
the connection parameters, it does not create the database schema.

**Request body:**

- **application/json**
  - type: `object`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cấu hình kết nối database thành công |
| 400 | Yêu cầu không hợp lệ (thiếu trường bắt buộc, loại database không hợp lệ, v.v.) |
| 500 | Lỗi máy chủ (không lưu được cấu hình, v.v.) |


### `GET` /v1/recognition/faces

**Summary:** Liệt kê face subject

**Operation ID:** `listFaceSubjects`

Lấy danh sách tất cả face subject đã lưu, có hỗ trợ phân trang.
You can filter by subject name or retrieve all subjects.

This endpoint returns paginated results with face information including image_id and subject name.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| page | query | No | Số trang mẫu cần trả về. Dùng cho phân trang. Giá trị mặc định 0. |

| size | query | No | Số khuôn mặt mỗi trang (kích thước trang). Dùng cho phân trang. Giá trị mặc định 20. |

| subject | query | No | Chỉ định subject mẫu cần trả về. Để trống thì trả về mẫu của mọi subject. |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy danh sách face subject thành công |
| 401 | Chưa xác thực (thiếu hoặc API key không hợp lệ) |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/recognition/faces

**Summary:** CORS preflight cho đăng ký face subject

**Operation ID:** `registerFaceSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/recognition/faces

**Summary:** Đăng ký face subject

**Operation ID:** `registerFaceSubject`

Đăng ký face subject bằng cách lưu ảnh. Có thể thêm nhiều ảnh để huấn luyện hệ thống.
Images should contain only a single face.

**Face Detection Validation**: Before registration, the system performs mandatory face detection.
If no face is detected in the uploaded image, the registration will be rejected with a 400 error.
The user must upload a different image that contains a clear, detectable face.

This endpoint accepts image data in two formats:
1. **Multipart/form-data**: Upload file directly (recommended for file uploads)
2. **Application/json**: Send base64-encoded image string

Supported image formats: JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP
Maximum file size: 5MB

The system will detect the face in the image and store the face features for recognition.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| subject | query | Yes | Tên subject cần đăng ký (vd. \"subject1\") |

| det_prob_threshold | query | No | Ngưỡng xác suất nhận diện (0.0 đến 1.0) |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **file**: string — Image file to upload. Supported formats: JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP. Max size: 5MB
- **application/json**
  - type: `object`
    - **file**: string — Base64-encoded image data. Supported formats: JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP. Max size: 5MB
  - example: `{'file': 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=='}`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã đăng ký face subject thành công |
| 400 | Invalid request. Possible reasons: - Missing required query parameter: subject - Invalid image format or corrupted image data - No face detected in the uploaded image (registration will be rejected) - Face detected but confidence below threshold  |
| 401 | Chưa xác thực (thiếu hoặc API key không hợp lệ) |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/recognition/faces/all

**Summary:** Xóa tất cả face subject

**Operation ID:** `deleteAllFaceSubjects`

Xóa tất cả face subject khỏi database. Thao tác không thể hoàn tác.
All image IDs and subject mappings will be removed.

**Warning:** This endpoint will delete all registered faces. Use with caution.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa tất cả face subject thành công |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/recognition/faces/all

**Summary:** CORS preflight cho xóa tất cả face subject

**Operation ID:** `deleteAllFaceSubjectsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `OPTIONS` /v1/recognition/faces/delete

**Summary:** CORS preflight cho xóa nhiều face subject

**Operation ID:** `deleteMultipleFaceSubjectsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/recognition/faces/delete

**Summary:** Xóa nhiều face subject

**Operation ID:** `deleteMultipleFaceSubjects`

Xóa nhiều face subject theo image ID. Nếu một số ID không tồn tại
they will be ignored. This endpoint is available since version 1.0.

**Request body:**

- **application/json**
  - type: `array` — Mảng image ID cần xóa
  - example: `['6b135f5b-a365-4522-b1f1-4c9ac2dd0728', '7c246g6c-b476-5633-c2g2-5d0bd3ee1839']`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa face subject thành công |
| 400 | Yêu cầu không hợp lệ (JSON không hợp lệ, không phải mảng, v.v.) |
| 401 | Chưa xác thực (thiếu hoặc API key không hợp lệ) |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/recognition/faces/{image_id}

**Summary:** Xóa face subject theo ID hoặc tên subject

**Operation ID:** `deleteFaceSubject`

Xóa face subject theo image ID hoặc tên subject.

- If the identifier is an **image_id**, only that specific face will be deleted.
- If the identifier is a **subject name**, all faces associated with that subject will be deleted.

The endpoint automatically detects whether the identifier is an image_id or subject name.
If no face is found with the given identifier, a 404 error is returned.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| image_id | path | Yes | Either the UUID of the face subject to be deleted, or the subject name. The endpoint will automatically detect the type of identifier.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa face subject thành công |
| 400 | Yêu cầu không hợp lệ (thiếu image_id, v.v.) |
| 401 | Chưa xác thực (thiếu hoặc API key không hợp lệ) |
| 404 | Không tìm thấy face subject |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/recognition/faces/{image_id}

**Summary:** CORS preflight cho xóa face subject

**Operation ID:** `deleteFaceSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `OPTIONS` /v1/recognition/recognize

**Summary:** CORS preflight cho face recognition

**Operation ID:** `recognizeFacesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/recognition/recognize

**Summary:** Nhận diện khuôn mặt từ ảnh tải lên

**Operation ID:** `recognizeFaces`

Nhận diện khuôn mặt từ ảnh tải lên. Ảnh có thể gửi dạng base64
in a multipart/form-data request. Returns face detection results including bounding boxes,
landmarks, recognized subjects, and execution times.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| limit | query | No | Số khuôn mặt tối đa cần nhận diện (0 = không giới hạn). Ưu tiên nhận diện khuôn mặt lớn nhất trước. |

| prediction_count | query | No | Số dự đoán trả về cho mỗi khuôn mặt |

| det_prob_threshold | query | No | Ngưỡng xác suất nhận diện (0.0 đến 1.0) |

| threshold | query | No | Ngưỡng độ tương đồng tùy chọn (0.0 đến 1.0). Nếu có, subject có độ tương đồng dưới ngưỡng sẽ bị lọc bỏ. Nếu bỏ qua, trả về top-N độ tương đồng không lọc. |

| face_plugins | query | No | Các face plugin cần bật (phân tách bằng dấu phẩy) |

| status | query | No | Bộ lọc trạng thái |

| detect_faces | query | No | Có phát hiện khuôn mặt (true) hay chỉ nhận diện khuôn mặt đã có (false) |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **file**: string — File ảnh dạng base64 hoặc dữ liệu nhị phân

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Kết quả nhận diện khuôn mặt |
| 400 | Yêu cầu không hợp lệ (thiếu file, định dạng ảnh không hợp lệ, v.v.) |
| 401 | Chưa xác thực (thiếu hoặc API key không hợp lệ) |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/recognition/search

**Summary:** CORS preflight cho tìm kiếm appearance subject

**Operation ID:** `searchAppearanceSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/recognition/search

**Summary:** Tìm kiếm appearance subject

**Operation ID:** `searchAppearanceSubject`

Tìm khuôn mặt tương tự trong database từ ảnh khuôn mặt đầu vào.
Returns a list of matching faces sorted by similarity (highest first) above the specified threshold.

**How it works:**
1. Detects faces in the uploaded image
2. Extracts face embeddings from the first detected face
3. Compares with all faces in the database
4. Returns matches with similarity >= threshold, sorted from highest to lowest

**Response fields:**
- `image_id`: Unique identifier of the matched face
- `subject`: Name of the matched subject
- `similarity`: Cosine similarity score (0.0 - 1.0)
- `face_image`: Base64 encoded face image (reserved for future use)

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| threshold | query | No | Ngưỡng độ tương đồng tối thiểu cho kết quả khớp (0.0 - 1.0) |

| limit | query | No | Số kết quả tối đa trả về (0 = không giới hạn) |

| det_prob_threshold | query | No | Ngưỡng xác suất phát hiện khuôn mặt |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **file**: string — File ảnh chứa khuôn mặt cần tìm
- **application/json**
  - type: `object`
    - **file**: string — Dữ liệu ảnh mã hóa Base64

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Kết quả tìm kiếm |
| 400 | Yêu cầu không hợp lệ |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/recognition/subjects/{subject}

**Summary:** CORS preflight cho đổi tên subject

**Operation ID:** `renameSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/recognition/subjects/{subject}

**Summary:** Đổi tên face subject

**Operation ID:** `renameSubject`

Renames an existing face subject. This endpoint searches for the face subject name in the face database
and allows you to select and rename that subject.

**How it works:**
1. The endpoint searches for the subject name specified in the URL path (`{subject}`) in the face database.
2. If found, it renames the subject to the new name provided in the request body.
3. All face registrations associated with the old subject name are updated to use the new name.

**Behavior:**
- If the new subject name does not exist, all faces from the old subject are moved to the new name.
- If the new subject name already exists, the subjects are merged. The embeddings are averaged and
  all faces from the old subject are reassigned to the subject with the new name, and the old subject is removed.
- All image_id mappings are automatically updated to reflect the new subject name.
- The face database file is updated to persist the changes.

**Note:** The subject name in the URL path must match exactly with a subject name in the database.
Use GET /v1/recognition/faces to list all available subjects.

This endpoint is available since version 0.6.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| subject | path | Yes | The current subject name to rename. This must match exactly with a subject name in the face database. Use GET /v1/recognition/faces to see all available subjects.  |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/RenameSubjectRequest
  - example: `{'subject': 'testing_face'}`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã đổi tên subject thành công |
| 400 | Yêu cầu không hợp lệ (không tìm thấy subject, thiếu subject, JSON không hợp lệ, v.v.) |
| 500 | Lỗi máy chủ |



## SecuRT Instance

### `GET` /v1/securt/experimental/instance/{instanceId}/surrender_detection

**Summary:** Lấy cấu hình surrender detection (Thử nghiệm)

**Operation ID:** `getSurrenderDetection`

Lấy cấu hình surrender detection của instance SecuRT. (Tính năng thử nghiệm.)

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình surrender detection |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/experimental/instance/{instanceId}/surrender_detection

**Summary:** CORS preflight cho surrender detection

**Operation ID:** `surrenderDetectionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/experimental/instance/{instanceId}/surrender_detection

**Summary:** Thiết lập surrender detection (Thử nghiệm)

**Operation ID:** `setSurrenderDetection`

Bật hoặc tắt surrender detection cho instance SecuRT. (Tính năng thử nghiệm.)

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Bật surrender detection

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình surrender detection thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance

**Summary:** CORS preflight cho tạo instance

**Operation ID:** `createSecuRTInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance

**Summary:** Tạo instance SecuRT mới

**Operation ID:** `createSecuRTInstance`

Tạo instance SecuRT mới với cấu hình cung cấp. ID instance sẽ được tự sinh nếu không cung cấp.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInstanceWrite
  - example: `{'name': 'My SecuRT Instance', 'detectorMode': 'SmartDetection', 'detectionSensitivity': 'Low', 'movementSensitivity': 'Low', 'sensorModality': 'RGB', 'frameRateLimit': 0, 'metadataMode': False, 'statisticsMode': False, 'diagnosticsMode': False, 'debugMode': False}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo instance thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Instance đã tồn tại hoặc tạo thất bại |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/securt/instance/{instanceId}

**Summary:** Xóa instance SecuRT

**Operation ID:** `deleteSecuRTInstance`

Xóa instance SecuRT. Instance phải tồn tại.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã xóa instance thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}

**Summary:** CORS preflight cho thao tác instance

**Operation ID:** `securtInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PATCH` /v1/securt/instance/{instanceId}

**Summary:** Cập nhật instance SecuRT

**Operation ID:** `updateSecuRTInstance`

Cập nhật instance SecuRT với cấu hình cung cấp. Chỉ các trường được gửi lên mới được cập nhật.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInstanceWrite
  - example: `{'name': 'Updated Instance Name', 'detectorMode': 'Detection', 'detectionSensitivity': 'Medium'}`

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cập nhật instance thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `PUT` /v1/securt/instance/{instanceId}

**Summary:** Tạo instance SecuRT với ID chỉ định

**Operation ID:** `createSecuRTInstanceWithId`

Tạo instance SecuRT với ID chỉ định. Nếu instance đã tồn tại sẽ trả về lỗi conflict.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInstanceWrite
  - example: `{'name': 'My SecuRT Instance', 'detectorMode': 'SmartDetection', 'detectionSensitivity': 'Low', 'movementSensitivity': 'Low', 'sensorModality': 'RGB', 'frameRateLimit': 0, 'metadataMode': False, 'statisticsMode': False, 'diagnosticsMode': False, 'debugMode': False}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo instance thành công |
| 400 | Yêu cầu không hợp lệ |
| 409 | Instance đã tồn tại |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/analytics_entities

**Summary:** Lấy analytics entities của instance SecuRT

**Operation ID:** `securtGetAnalyticsEntitiesV1`

Trả về tất cả analytics entities (vùng và đường) đã cấu hình cho instance SecuRT, gồm mọi loại of areas (crossing, intrusion, loitering, crowding, occupancy, crowd estimation, dwelling, armed person, object left, object removed, fallen person) and all types of lines (crossing, counting, tailgating).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy thực thể analytics thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/analytics_entities

**Summary:** CORS preflight cho analytics entities

**Operation ID:** `securtGetAnalyticsEntitiesV1Options`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/securt/instance/{instanceId}/attributes_extraction

**Summary:** Lấy cấu hình attributes extraction

**Operation ID:** `getAttributesExtraction`

Lấy cấu hình attributes extraction đã thiết lập cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình attributes extraction |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/attributes_extraction

**Summary:** CORS preflight cho attributes extraction

**Operation ID:** `attributesExtractionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/attributes_extraction

**Summary:** Thiết lập attributes extraction

**Operation ID:** `setAttributesExtraction`

Cấu hình trích xuất thuộc tính (attributes extraction) cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enabled**: boolean — Bật trích xuất thuộc tính (attributes extraction)
    - **attributes**: array — Mảng thuộc tính cần trích xuất (attributes to extract)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình attributes extraction thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** Xóa tất cả vùng loại trừ (exclusion areas)

**Operation ID:** `deleteExclusionAreas`

Xóa tất cả vùng loại trừ của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã xóa tất cả vùng loại trừ thành công |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** Lấy danh sách vùng loại trừ (exclusion areas)

**Operation ID:** `getExclusionAreas`

Lấy tất cả vùng loại trừ (exclusion areas) của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách vùng loại trừ (exclusion areas) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** CORS preflight cho exclusion areas

**Operation ID:** `exclusionAreasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** Thêm vùng loại trừ (exclusion area)

**Operation ID:** `addExclusionArea`

Thêm vùng loại trừ (exclusion area) vào instance SecuRT để loại một số vùng khỏi phân tích

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **coordinates**: array — Mảng tọa độ định nghĩa đa giác vùng loại trừ (exclusion area)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã thêm vùng loại trừ thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/face_detection

**Summary:** CORS preflight cho face detection

**Operation ID:** `faceDetectionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/face_detection

**Summary:** Thiết lập face detection

**Operation ID:** `setFaceDetection`

Bật hoặc tắt nhận diện khuôn mặt (face detection) cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Bật nhận diện khuôn mặt

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình nhận diện khuôn mặt thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/feature_extraction

**Summary:** Lấy cấu hình feature extraction

**Operation ID:** `getFeatureExtraction`

Lấy các loại feature extraction đã cấu hình của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình trích xuất đặc trưng |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/feature_extraction

**Summary:** CORS preflight cho feature extraction

**Operation ID:** `featureExtractionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/feature_extraction

**Summary:** Thiết lập feature extraction

**Operation ID:** `setFeatureExtraction`

Cấu hình các loại feature extraction cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **types**: array — Mảng loại trích xuất đặc trưng

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình trích xuất đặc trưng thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/input

**Summary:** CORS preflight cho input

**Operation ID:** `setSecuRTInstanceInputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/input

**Summary:** Thiết lập nguồn đầu vào cho instance SecuRT

**Operation ID:** `setSecuRTInstanceInput`

Thiết lập nguồn đầu vào cho instance SecuRT. Hỗ trợ File, RTSP, RTMP, HLS. Instance phải tồn tại; nếu đang chạy sẽ được khởi động lại để áp dụng thay đổi.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInputRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cập nhật cấu hình input thành công |
| 400 | Yêu cầu không hợp lệ (loại input sai hoặc thiếu trường bắt buộc) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/lpr

**Summary:** Lấy cấu hình nhận diện biển số (LPR)

**Operation ID:** `getLPR`

Lấy cấu hình LPR của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình LPR |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/lpr

**Summary:** CORS preflight cho LPR

**Operation ID:** `lprOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/lpr

**Summary:** Thiết lập nhận diện biển số (LPR)

**Operation ID:** `setLPR`

Bật hoặc tắt nhận diện biển số (LPR) cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Bật nhận diện biển số

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình LPR thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/masking_areas

**Summary:** CORS preflight cho masking areas

**Operation ID:** `maskingAreasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/masking_areas

**Summary:** Thiết lập vùng che (masking areas)

**Operation ID:** `setMaskingAreas`

Cấu hình vùng che (masking areas) cho instance SecuRT để ẩn một số vùng khỏi xử lý

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **areas**: array — Mảng đa giác vùng che (masking area), mỗi đa giác là mảng tọa độ

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình vùng che (masking areas) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/motion_area

**Summary:** CORS preflight cho motion area

**Operation ID:** `motionAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/motion_area

**Summary:** Thiết lập vùng chuyển động (motion area)

**Operation ID:** `setMotionArea`

Cấu hình vùng phát hiện chuyển động (motion area) cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **coordinates**: array — Mảng tọa độ định nghĩa đa giác vùng chuyển động (motion area)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình vùng chuyển động (motion area) thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/output

**Summary:** CORS preflight cho output

**Operation ID:** `setSecuRTInstanceOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/output

**Summary:** Thiết lập đích đầu ra cho instance SecuRT

**Operation ID:** `setSecuRTInstanceOutput`

Cấu hình đích đầu ra cho instance SecuRT. Hỗ trợ MQTT, RTMP, RTSP, HLS. Instance đang chạy sẽ được khởi động lại để áp dụng. Có thể gọi nhiều lần để cấu hình nhiều output; cấu hình lưu trong AdditionalParams của instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTOutputRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cập nhật cấu hình output thành công |
| 400 | Yêu cầu không hợp lệ (loại output sai hoặc thiếu trường bắt buộc) |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/performance_profile

**Summary:** Lấy performance profile

**Operation ID:** `getPerformanceProfile`

Lấy performance profile đã cấu hình của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình hồ sơ hiệu năng |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/performance_profile

**Summary:** CORS preflight cho performance profile

**Operation ID:** `performanceProfileOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/performance_profile

**Summary:** Thiết lập performance profile

**Operation ID:** `setPerformanceProfile`

Cấu hình performance profile cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **profile**: string — Tên hồ sơ hiệu năng

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình hồ sơ hiệu năng thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/pip

**Summary:** Lấy cấu hình picture-in-picture (PIP)

**Operation ID:** `getPIP`

Lấy cấu hình PIP của instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Cấu hình PIP |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/pip

**Summary:** CORS preflight cho PIP

**Operation ID:** `pipOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/securt/instance/{instanceId}/pip

**Summary:** Thiết lập picture-in-picture (PIP)

**Operation ID:** `setPIP`

Bật hoặc tắt tính năng picture-in-picture (PIP) cho instance SecuRT

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Bật picture-in-picture

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Đã cấu hình PIP thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `GET` /v1/securt/instance/{instanceId}/stats

**Summary:** Lấy thống kê instance SecuRT

**Operation ID:** `getSecuRTInstanceStats`

Trả về thống kê của instance SecuRT gồm frame rate, độ trễ, số frame đã xử lý, số track, và running status.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Định danh instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã lấy thống kê thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy instance |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/securt/instance/{instanceId}/stats

**Summary:** CORS preflight cho stats

**Operation ID:** `getSecuRTInstanceStatsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |



## Solutions

### `GET` /v1/core/solution

**Summary:** Liệt kê tất cả solution

**Operation ID:** `listSolutions`

Trả về danh sách tất cả solution kèm thông tin tóm tắt (loại solution, số node pipeline).

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách solution |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/solution

**Summary:** CORS preflight cho solutions

**Operation ID:** `listSolutionsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/solution

**Summary:** Tạo solution mới

**Operation ID:** `createSolution`

Tạo cấu hình solution tùy chỉnh mới. Solution tùy chỉnh có thể sửa và xóa, khác với solution mặc định.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateSolutionRequest
  - example: `{'solutionId': 'custom_face_detection', 'solutionName': 'Custom Face Detection', 'solutionType': 'face_detection', 'pipeline': [{'nodeType': 'rtsp_src', 'nodeName': 'source_{instanceId}', 'parameters': {'url': 'rtsp://localhost/stream'}}, {'nodeType': 'yunet_face_detector', 'nodeName': 'detector_{instanceId}', 'parameters': {'model_path': 'models/face/yunet.onnx'}}, {'nodeType': 'file_des', 'nodeName': 'output_{instanceId}', 'parameters': {'output_path': '/tmp/output'}}], 'defaults': {'RTSP_URL': 'rtsp://localhost/stream'}}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Đã tạo solution thành công |
| 400 | Yêu cầu không hợp lệ (validation thất bại) |
| 409 | Solution đã tồn tại |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/solution/defaults

**Summary:** Liệt kê solution mặc định

**Operation ID:** `listDefaultSolutions`

Lấy danh sách tất cả solution mặc định có sẵn từ examples/default_solutions/

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách solution mặc định |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/solution/defaults

**Summary:** CORS preflight cho default solutions

**Operation ID:** `listDefaultSolutionsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `OPTIONS` /v1/core/solution/defaults/{solutionId}

**Summary:** CORS preflight cho nạp default solution

**Operation ID:** `loadDefaultSolutionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `POST` /v1/core/solution/defaults/{solutionId}

**Summary:** Nạp solution mặc định

**Operation ID:** `loadDefaultSolution`

Nạp solution mặc định từ examples/default_solutions/ vào solution registry.
This endpoint allows loading a default solution template into the system for use in creating instances.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Định danh solution mặc định cần nạp |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã nạp solution mặc định thành công |
| 404 | Không tìm thấy solution mặc định |
| 500 | Lỗi máy chủ |


### `DELETE` /v1/core/solution/{solutionId}

**Summary:** Xóa solution

**Operation ID:** `deleteSolution`

Xóa solution tùy chỉnh. Solution mặc định không thể xóa.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Định danh solution |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa solution thành công |
| 403 | Không thể xóa solution mặc định |
| 404 | Không tìm thấy solution |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/solution/{solutionId}

**Summary:** Lấy chi tiết solution

**Operation ID:** `getSolution`

Trả về thông tin chi tiết của một solution, gồm cấu hình pipeline đầy đủ.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Định danh solution |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Chi tiết solution |
| 404 | Không tìm thấy solution |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/solution/{solutionId}

**Summary:** CORS preflight cho solution

**Operation ID:** `getSolutionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `PUT` /v1/core/solution/{solutionId}

**Summary:** Cập nhật solution

**Operation ID:** `updateSolution`

Cập nhật cấu hình solution tùy chỉnh. Solution mặc định không thể cập nhật.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Định danh solution |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateSolutionRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã cập nhật solution thành công |
| 400 | Yêu cầu không hợp lệ (validation thất bại) |
| 403 | Không thể cập nhật solution mặc định |
| 404 | Không tìm thấy solution |
| 500 | Lỗi máy chủ |


### `GET` /v1/core/solution/{solutionId}/instance-body

**Summary:** Lấy body mẫu tạo instance

**Operation ID:** `getSolutionInstanceBody`

Trả về body yêu cầu mẫu để tạo instance với solution này, kèm metadata schema chi tiết cho UI linh hoạt.
Bao gồm:
- **Trường chuẩn**: Các trường tạo instance chung với giá trị mặc định
- **Tham số bổ sung**: Tham số theo solution với giá trị mẫu theo tên tham số
- **Schema**: Metadata chi tiết cho mọi trường (kiểu, quy tắc validation, gợi ý UI, mô tả, ví dụ, danh mục)

Body phản hồi có thể dùng trực tiếp để tạo instance qua POST /v1/core/instance.
Trường schema cung cấp metadata phong phú để UI render form động, kiểm tra đầu vào và trải nghiệm người dùng tốt hơn.

**Chế độ tối thiểu** (`?minimal=true`):
- Chỉ trả về các trường cần thiết: `name`, `solution`, `group`, `autoStart`
- Chỉ gồm additionalParams bắt buộc (tham số không có giá trị mặc định)
- Loại trừ tham số đầu ra tùy chọn (MQTT, RTMP, Screen nếu chưa đặt)
- Schema đơn giản, không có flexibleInputOutput
- Dùng cho form đơn giản, thân thiện với ít trường

**Chế độ đầy đủ** (mặc định):
- Trả về tất cả trường có sẵn kèm tùy chọn nâng cao
- Gồm mọi additionalParams (input và output)
- Schema đầy đủ với tùy chọn flexibleInputOutput
- Dùng khi cấu hình nâng cao hoặc người dùng chọn "Tùy chọn nâng cao"

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Định danh solution |

| minimal | query | No | Nếu true, chỉ trả về các trường cần thiết cho form UI tối thiểu. Chỉ gồm tham số bắt buộc và các trường tùy chọn chung (name, solution, group, autoStart). Nên dùng khi render form lần đầu để tránh quá nhiều trường.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Body mẫu tạo instance |
| 404 | Không tìm thấy solution |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/solution/{solutionId}/instance-body

**Summary:** CORS preflight cho solution instance body

**Operation ID:** `getSolutionInstanceBodyOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |


### `GET` /v1/core/solution/{solutionId}/parameters

**Summary:** Lấy schema tham số solution

**Operation ID:** `getSolutionParameters`

Trả về schema tham số cần thiết để tạo instance với solution này.
Bao gồm:
- **Tham số bổ sung**: Tham số theo solution trích từ pipeline nodes (vd. RTSP_URL, MODEL_PATH, FILE_PATH)
- **Trường chuẩn**: Các trường tạo instance chung (name, group, persistent, autoStart, ...)

Tham số đánh dấu bắt buộc phải có trong object `additionalParams` khi tạo instance.
Tham số có giá trị mặc định là tùy chọn.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Định danh solution |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Schema tham số solution |
| 404 | Không tìm thấy solution |
| 500 | Lỗi máy chủ |


### `OPTIONS` /v1/core/solution/{solutionId}/parameters

**Summary:** CORS preflight cho tham số solution

**Operation ID:** `getSolutionParametersOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Phản hồi CORS preflight |



## Video

### `GET` /v1/core/video/list

**Summary:** Liệt kê file video đã tải lên

**Operation ID:** `listVideos`

Returns a list of video files that have been uploaded to the server.
- If `directory` parameter is not specified: Recursively searches and lists all video files
  in the entire videos directory tree (including all subdirectories).
- If `directory` parameter is specified: Lists only files in that specific directory
  (non-recursive, does not search subdirectories).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Subdirectory path to list files from (e.g., "projects/myproject/videos", "users/user123"). If specified, only lists files in that directory (non-recursive). If not specified, recursively searches all subdirectories.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Danh sách file video |
| 500 | Lỗi máy chủ |


### `POST` /v1/core/video/upload

**Summary:** Tải lên file video

**Operation ID:** `uploadVideo`

Tải file video (MP4, AVI, MKV, v.v.) lên máy chủ. File được lưu trong thư mục videos. Có thể chỉ định thư mục con qua tham số query `directory` (ví dụ: `projects/myproject/videos`). Thư mục sẽ được tạo tự động nếu chưa tồn tại. Nếu không chỉ định, file lưu tại thư mục videos gốc.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Đường dẫn thư mục con nơi file video sẽ được lưu. Sử dụng dấu gạch chéo để phân tách các cấp thư mục (ví dụ: "projects/myproject/videos", "users/user123", "category1/subcategory"). Thư mục sẽ được tạo tự động nếu không tồn tại. Nếu không được chỉ định, file sẽ được lưu trong thư mục videos gốc.  |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **files**: array — File video để tải lên. Chọn nhiều file để tải lên cùng lúc (.mp4, .avi, .mkv, .mov, .flv, .wmv, .webm, v.v.)
    - **file**: string — Một file video để tải lên (thay thế cho mảng files)
- **application/octet-stream**
  - type: `string` — File video dưới dạng dữ liệu nhị phân

**Responses:**

| Code | Description |
|------|-------------|
| 201 | File video đã được tải lên thành công |


### `DELETE` /v1/core/video/{videoName}

**Summary:** Xóa file video

**Operation ID:** `deleteVideo`

Xóa file video khỏi máy chủ. Có thể chỉ định thư mục con qua tham số query `directory` nếu file nằm trong thư mục con.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| videoName | path | Yes | Tên file video cần xóa |

| directory | query | No | Đường dẫn thư mục con chứa file video (ví dụ: "projects/myproject/videos"). Nếu không chỉ định, file được coi là ở thư mục videos gốc.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã xóa file video thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy file video |
| 500 | Lỗi máy chủ |


### `PUT` /v1/core/video/{videoName}

**Summary:** Đổi tên file video

**Operation ID:** `renameVideo`

Đổi tên file video trên máy chủ. Có thể chỉ định thư mục con qua tham số query `directory` nếu file nằm trong thư mục con.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| videoName | path | Yes | Tên hiện tại của file video cần đổi tên |

| directory | query | No | Đường dẫn thư mục con chứa file video (ví dụ: "projects/myproject/videos"). Nếu không chỉ định, file được coi là ở thư mục videos gốc.  |

**Request body:**

- **application/json**
  - type: `object`
    - **newName**: string — Tên mới cho file video (phải có phần mở rộng hợp lệ)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Đã đổi tên file video thành công |
| 400 | Yêu cầu không hợp lệ |
| 404 | Không tìm thấy file video |
| 409 | Tên mới đã tồn tại |
| 500 | Lỗi máy chủ |


