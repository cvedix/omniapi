# Edge AI API

**Version:** 1.0.0

REST API for Edge AI operations and management.

## Logging Features

The server supports detailed logging features that can be enabled via command-line arguments:

- **API Logging** (`--log-api` or `--debug-api`): Logs all API requests and responses with response times
- **Instance Execution Logging** (`--log-instance` or `--debug-instance`): Logs instance lifecycle events (start/stop/status)
- **SDK Output Logging** (`--log-sdk-output` or `--debug-sdk-output`): Logs SDK output when instances process data

Example usage:
```bash
./edge_ai_api --log-api --log-instance --log-sdk-output
```

For more details, see the [Logging Documentation](../docs/LOGGING.md).

## Swagger UI

This API provides Swagger UI for interactive testing and exploration:
- Access Swagger UI at `/swagger` or `/v1/swagger`
- OpenAPI specification available at `/openapi.yaml` or `/v1/openapi.yaml`
- Server URLs are automatically updated based on environment variables (`API_HOST` and `API_PORT`)

---

## AI

### `POST` /v1/core/ai/batch

**Summary:** Process batch of images/frames

**Operation ID:** `processBatch`

Processes multiple images/frames in a batch through the AI processing pipeline.
Currently returns 501 Not Implemented.

**Request body:**

- **application/json**
  - type: `object`
    - **images**: array — Array of base64-encoded images
    - **config**: string — Processing configuration (JSON string)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Batch processing job submitted successfully |
| 400 | Bad request |
| 501 | Not implemented |


### `GET` /v1/core/ai/metrics

**Summary:** Get AI processing metrics

**Operation ID:** `getAIMetrics`

Returns detailed metrics about AI processing including performance statistics,
cache statistics, and rate limiter information.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | AI processing metrics |


### `POST` /v1/core/ai/process

**Summary:** Process single image/frame

**Operation ID:** `processImage`

Processes a single image/frame through the AI processing pipeline.
Supports priority-based queuing and rate limiting.

**Request body:**

- **application/json**
  - type: `object`
    - **image**: string — Base64-encoded image data
    - **config**: string — Processing configuration (JSON string)
    - **priority**: string — Request priority

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Processing job submitted successfully |
| 400 | Bad request (invalid JSON or missing required fields) |
| 429 | Rate limit exceeded |
| 500 | Server error |
| 501 | Not implemented |


### `GET` /v1/core/ai/status

**Summary:** Get AI processing status

**Operation ID:** `getAIStatus`

Returns the current status of the AI processing system including queue size,
queue max capacity, GPU availability, and other resource information.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | AI processing status |



## Area Core

### `DELETE` /v1/core/instance/{instanceId}/jams

**Summary:** Delete all jam zones

**Operation ID:** `deleteAllJams`

Deletes all jam zones for a ba_jam instance.

**Real-time Update:** Zones are removed from the running instance when possible; otherwise, a restart will be triggered.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | All zones deleted successfully |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/jams

**Summary:** Get all jam zones

**Operation ID:** `getAllJams`

Returns all jam zones configured for a ba_jam instance. Jam zones define ROIs where traffic congestion detection is performed.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of jam zones |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/jams

**Summary:** CORS preflight for jams endpoints

**Operation ID:** `jamsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/jams

**Summary:** Create one or more jam zones

**Operation ID:** `createJam`

Creates one or more jam zones for a ba_jam instance. Jam zones are used to detect traffic congestion within a specified ROI.

**Request Format:**
- Single zone: Send a single object (CreateJamRequest)
- Multiple zones: Send an array of objects [CreateJamRequest, ...]

**Real-time Update:** Zones are applied to the running instance when possible; otherwise, a restart will be triggered.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Request body:**

- **application/json**
  - type: `object`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Zone(s) created successfully. Returns single zone object for single request, or object with metadata for array request. |
| 400 | Bad request (invalid ROI or parameters) |
| 404 | Instance not found |
| 500 | Internal server error |


### `DELETE` /v1/core/instance/{instanceId}/jams/{jamId}

**Summary:** Delete a specific jam zone

**Operation ID:** `deleteJam`

Deletes a specific jam zone by ID.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| jamId | path | Yes | The unique identifier of the jam zone to delete |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Jam zone deleted successfully |
| 404 | Instance or zone not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/jams/{jamId}

**Summary:** Get a specific jam zone

**Operation ID:** `getJam`

Returns a specific jam zone by ID for a ba_jam instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| jamId | path | Yes | The unique identifier of the jam zone to retrieve |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Jam zone retrieved successfully |
| 404 | Instance or zone not found |
| 500 | Internal server error |


### `PUT` /v1/core/instance/{instanceId}/jams/{jamId}

**Summary:** Update an existing jam zone

**Operation ID:** `updateJam`

Updates an existing jam zone by ID. Partial updates are supported.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| jamId | path | Yes | The unique identifier of the jam zone to update |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateJamRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Jam zone updated successfully |
| 400 | Bad request (invalid ROI or parameters) |
| 404 | Instance or zone not found |
| 500 | Internal server error |


### `DELETE` /v1/core/instance/{instanceId}/stops

**Summary:** Delete all stop zones

**Operation ID:** `deleteAllStops`

Deletes all stop zones for a ba_stop instance.

**Real-time Update:** Changes require instance restart.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | All stops deleted successfully |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/stops

**Summary:** Get all stop zones

**Operation ID:** `getAllStops`

Returns all stop zones configured for a ba_stop instance.
Stop zones are used to detect objects that stop inside a defined ROI in the video stream.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of stop zones |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/stops

**Summary:** CORS preflight for stops endpoints

**Operation ID:** `stopsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/stops

**Summary:** Create one or more stop zones

**Operation ID:** `createStop`

Creates one or more stop zones for a ba_stop instance.

**Request Format:**
- Single zone: Send a single object (CreateStopRequest)
- Multiple zones: Send an array of objects [CreateStopRequest, ...]

**Real-time Update:** Stop zones require instance restart to apply changes.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Request body:**

- **application/json**
  - type: `object`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Zone(s) created successfully. Returns single zone object for single request, or object with metadata for array request. |
| 400 | Bad request (invalid coordinates or parameters) |
| 404 | Instance not found |
| 500 | Internal server error |


### `DELETE` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** Delete a specific stop zone

**Operation ID:** `deleteStop`

Deletes a specific stop zone by ID for a ba_stop instance.

**Real-time Update:** Changes require instance restart.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| stopId | path | Yes | The unique identifier of the stop zone to delete |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Stop zone deleted successfully |
| 404 | Instance or stop zone not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** Get a specific stop zone

**Operation ID:** `getStop`

Returns a specific stop zone by ID for a ba_stop instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| stopId | path | Yes | The unique identifier of the stop zone to retrieve |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Stop zone retrieved successfully |
| 404 | Instance or stop zone not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** CORS preflight for stops detail endpoint

**Operation ID:** `stopOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/core/instance/{instanceId}/stops/{stopId}

**Summary:** Update a specific stop zone

**Operation ID:** `updateStop`

Updates a specific stop zone by ID for a ba_stop instance.

**Real-time Update:** Changes require instance restart.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| stopId | path | Yes | The unique identifier of the stop zone to update |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateStopRequest

**Responses:**




## Area SecuRT

### `OPTIONS` /v1/securt/instance/{instanceId}/area/armedPerson

**Summary:** CORS preflight for armed person area

**Operation ID:** `createArmedPersonAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/armedPerson

**Summary:** Create armed person area

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
| 201 | Armed person area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/armedPerson

**Summary:** Create armed person area with ID

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
| 201 | Armed person area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/crossing

**Summary:** CORS preflight for crossing area

**Operation ID:** `createCrossingAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/crossing

**Summary:** Create crossing area

**Operation ID:** `createCrossingArea`

Creates a new crossing area for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Crossing area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/crossing

**Summary:** Create crossing area with ID

**Operation ID:** `createCrossingAreaWithId`

Creates a crossing area with a specific ID for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| areaId | path | Yes | Area ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Crossing area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/crowdEstimation

**Summary:** CORS preflight for crowd estimation area

**Operation ID:** `createCrowdEstimationAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/crowdEstimation

**Summary:** Create crowd estimation area

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
| 201 | Crowd estimation area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/crowdEstimation

**Summary:** Create crowd estimation area with ID

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
| 201 | Crowd estimation area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/crowding

**Summary:** CORS preflight for crowding area

**Operation ID:** `createCrowdingAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/crowding

**Summary:** Create crowding area

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
| 201 | Crowding area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/crowding

**Summary:** Create crowding area with ID

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
| 201 | Crowding area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/dwelling

**Summary:** CORS preflight for dwelling area

**Operation ID:** `createDwellingAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/dwelling

**Summary:** Create dwelling area

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
| 201 | Dwelling area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/dwelling

**Summary:** Create dwelling area with ID

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
| 201 | Dwelling area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/faceCovered

**Summary:** CORS preflight for face covered area

**Operation ID:** `createFaceCoveredAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/faceCovered

**Summary:** Create face covered area (experimental)

**Operation ID:** `createFaceCoveredArea`

Creates a new face covered area for a SecuRT instance. This is an experimental feature.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/FaceCoveredAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Face covered area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/faceCovered

**Summary:** Create face covered area with ID (experimental)

**Operation ID:** `createFaceCoveredAreaWithId`

Creates a new face covered area with a specific ID for a SecuRT instance. This is an experimental feature.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| areaId | path | Yes | Area ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/FaceCoveredAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Face covered area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/fallenPerson

**Summary:** CORS preflight for fallen person area

**Operation ID:** `createFallenPersonAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/fallenPerson

**Summary:** Create fallen person area

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
| 201 | Fallen person area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/fallenPerson

**Summary:** Create fallen person area with ID

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
| 201 | Fallen person area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/intrusion

**Summary:** CORS preflight for intrusion area

**Operation ID:** `createIntrusionAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/intrusion

**Summary:** Create intrusion area

**Operation ID:** `createIntrusionArea`

Creates a new intrusion area for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/IntrusionAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Intrusion area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/intrusion

**Summary:** Create intrusion area with ID

**Operation ID:** `createIntrusionAreaWithId`

Creates an intrusion area with a specific ID

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
| 201 | Intrusion area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/loitering

**Summary:** CORS preflight for loitering area

**Operation ID:** `createLoiteringAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/loitering

**Summary:** Create loitering area

**Operation ID:** `createLoiteringArea`

Creates a new loitering area for a SecuRT instance

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
| 201 | Loitering area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/loitering

**Summary:** Create loitering area with ID

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
| 201 | Loitering area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/objectEnterExit

**Summary:** CORS preflight for object enter/exit area

**Operation ID:** `createObjectEnterExitAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/objectEnterExit

**Summary:** Create object enter/exit area

**Operation ID:** `createObjectEnterExitArea`

Creates a new object enter/exit area for BA Area Enter/Exit solution. Detects objects entering/exiting the area.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectEnterExitAreaWrite
  - example: `{'name': 'Entrance Area', 'coordinates': [{'x': 50, 'y': 150}, {'x': 250, 'y': 150}, {'x': 250, 'y': 350}, {'x': 50, 'y': 350}], 'classes': ['Person', 'Vehicle'], 'color': [0, 220, 0, 255], 'alertOnEnter': True, 'alertOnExit': True}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Object enter/exit area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/objectEnterExit

**Summary:** Create object enter/exit area with ID

**Operation ID:** `createObjectEnterExitAreaWithId`

Creates a new object enter/exit area with a specific area ID for BA Area Enter/Exit solution.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| areaId | path | Yes | Area ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ObjectEnterExitAreaWrite
  - example: `{'name': 'Restricted Zone', 'coordinates': [{'x': 350, 'y': 160}, {'x': 550, 'y': 160}, {'x': 550, 'y': 360}, {'x': 350, 'y': 360}], 'classes': ['Person'], 'color': [0, 0, 220, 255], 'alertOnEnter': True, 'alertOnExit': False}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Object enter/exit area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/objectLeft

**Summary:** CORS preflight for object left area

**Operation ID:** `createObjectLeftAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/objectLeft

**Summary:** Create object left area

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
| 201 | Object left area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/objectLeft

**Summary:** Create object left area with ID

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
| 201 | Object left area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/objectRemoved

**Summary:** CORS preflight for object removed area

**Operation ID:** `createObjectRemovedAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/objectRemoved

**Summary:** Create object removed area

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
| 201 | Object removed area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/objectRemoved

**Summary:** Create object removed area with ID

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
| 201 | Object removed area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/occupancy

**Summary:** CORS preflight for occupancy area

**Operation ID:** `createOccupancyAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/occupancy

**Summary:** Create occupancy area

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
| 201 | Occupancy area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/occupancy

**Summary:** Create occupancy area with ID

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
| 201 | Occupancy area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/vehicleGuard

**Summary:** CORS preflight for vehicle guard area

**Operation ID:** `createVehicleGuardAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/area/vehicleGuard

**Summary:** Create vehicle guard area (experimental)

**Operation ID:** `createVehicleGuardArea`

Creates a new vehicle guard area for a SecuRT instance. This is an experimental feature.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/VehicleGuardAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Vehicle guard area created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}/area/vehicleGuard

**Summary:** Create vehicle guard area with ID (experimental)

**Operation ID:** `createVehicleGuardAreaWithId`

Creates a new vehicle guard area with a specific ID for a SecuRT instance. This is an experimental feature.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| areaId | path | Yes | Area ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/VehicleGuardAreaWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Vehicle guard area created successfully |
| 400 | Invalid request |
| 409 | Area already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `DELETE` /v1/securt/instance/{instanceId}/area/{areaId}

**Summary:** Delete area by ID

**Operation ID:** `deleteArea`

Deletes a specific area by its ID

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| areaId | path | Yes | Area ID |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Area deleted successfully |
| 400 | Invalid request |
| 404 | Area or instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/area/{areaId}

**Summary:** CORS preflight for delete area

**Operation ID:** `deleteAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `DELETE` /v1/securt/instance/{instanceId}/areas

**Summary:** Delete all areas for SecuRT instance

**Operation ID:** `deleteAllSecuRTAreas`

Deletes all analytics areas for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | All areas deleted successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/areas

**Summary:** Get analytics areas for SecuRT instance

**Operation ID:** `getSecuRTAreas`

Returns all analytics areas configured for a SecuRT instance. This includes crossing areas, intrusion areas, loitering areas, crowding areas, occupancy areas, crowd estimation areas, dwelling areas, armed person areas, object left areas, object removed areas, fallen person areas, vehicle guard areas (experimental), and face covered areas (experimental).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Analytics areas retrieved successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/areas

**Summary:** CORS preflight for areas

**Operation ID:** `getSecuRTAreasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |



## Config

### `DELETE` /v1/core/config

**Summary:** Delete configuration section (query parameter)

**Operation ID:** `deleteConfigSectionQuery`

Deletes a specific section of the system configuration at the given path.

**Path Format:**
- Supports both forward slashes `/` and dots `.` as separators
- **Recommended:** Use forward slashes `/` for better compatibility
- Examples:
  - `system/max_running_instances` (with forward slash - recommended)
  - `system.max_running_instances` (with dot)
  - `system/web_server/port` (nested path with forward slash)
  - `system.web_server.port` (nested path with dot)

The configuration is saved to the config file after deletion.

**Auto Restart:**
- By default, the server will NOT automatically restart after configuration deletion
- To enable automatic restart, set `auto_restart=true` as a query parameter
- When `auto_restart=true`, the server will restart after 3 seconds if web server config changed

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | query | Yes | Configuration path. Supports both `/` (forward slash) and `.` (dot) as separators. **Recommended:** Use forward slashes `/` for better compatibility. Examples: `system/max_running_instances`, `system.web_server`, `system/web_server/port`  |

| auto_restart | query | No | If set to `true`, the server will automatically restart after configuration deletion. Default is `false` (no automatic restart).  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration section deleted successfully |
| 400 | Invalid request (empty path) |
| 404 | Configuration section not found |
| 500 | Server error |


### `GET` /v1/core/config

**Summary:** Get system configuration

**Operation ID:** `getConfig`

Returns system configuration. If `path` query parameter is provided, returns the specific configuration section.
Otherwise, returns the complete system configuration.

**Path Format:**
- Use forward slashes `/` or dots `.` to navigate nested configuration keys
- Examples: `system/max_running_instances`, `system.max_running_instances`, `gstreamer/decode_pipelines/auto`

**Recommended:** Use query parameter with forward slashes for better compatibility.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | query | No | Configuration path to get a specific section. Use `/` or `.` as separators. Examples: `system/max_running_instances`, `system.web_server`, `gstreamer/decode_pipelines/auto`  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Full system configuration |
| 500 | Server error |


### `OPTIONS` /v1/core/config

**Summary:** CORS preflight for config

**Operation ID:** `configOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PATCH` /v1/core/config

**Summary:** Update configuration section (query parameter)

**Operation ID:** `updateConfigSectionQuery`

Updates a specific section of the system configuration at the given path.

**Path Format:**
- Use forward slashes `/` or dots `.` as separators
- Examples: `system/max_running_instances`, `system.max_running_instances`

Only the provided fields in the section will be updated.
The configuration is saved to the config file after update.

**Auto Restart:**
- By default, the server will NOT automatically restart after configuration changes
- To enable automatic restart, set `auto_restart=true` as a query parameter or in the JSON body
- When `auto_restart=true`, the server will restart after 3 seconds if web server config changed

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | query | Yes | Configuration path using `/` or `.` as separators |

| auto_restart | query | No | If set to `true`, the server will automatically restart after configuration update. Default is `false` (no automatic restart). Can also be set in the JSON body as `auto_restart` field.  |

**Request body:**

- **application/json**
  - type: `object` — Configuration section value to update. Can optionally include `auto_restart` field (boolean) to trigger server restart.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration section updated successfully |
| 400 | Invalid request or validation failed |
| 500 | Server error |


### `POST` /v1/core/config

**Summary:** Create or update configuration (merge)

**Operation ID:** `createOrUpdateConfig`

Updates the system configuration by merging the provided JSON with the existing configuration.
Only the provided fields will be updated; other fields remain unchanged.
The configuration is saved to the config file after update.

**Auto Restart:**
- By default, the server will NOT automatically restart after configuration changes
- To enable automatic restart, set `auto_restart=true` as a query parameter or in the JSON body
- When `auto_restart=true`, the server will restart after 3 seconds if web server config changed

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| auto_restart | query | No | If set to `true`, the server will automatically restart after configuration update. Default is `false` (no automatic restart). Can also be set in the JSON body as `auto_restart` field.  |

**Request body:**

- **application/json**
  - type: `object` — Configuration object to merge. Can optionally include `auto_restart` field (boolean) to trigger server restart.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration updated successfully |
| 400 | Invalid request or validation failed |
| 500 | Server error |


### `PUT` /v1/core/config

**Summary:** Replace entire configuration

**Operation ID:** `replaceConfig`

Replaces the entire system configuration with the provided JSON.
All existing configuration will be replaced with the new values.
The configuration is saved to the config file after replacement.

**Auto Restart:**
- By default, the server will NOT automatically restart after configuration changes
- To enable automatic restart, set `auto_restart=true` as a query parameter or in the JSON body
- When `auto_restart=true`, the server will restart after 3 seconds if web server config changed

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| auto_restart | query | No | If set to `true`, the server will automatically restart after configuration replacement. Default is `false` (no automatic restart). Can also be set in the JSON body as `auto_restart` field.  |

**Request body:**

- **application/json**
  - type: `object` — Complete configuration object to replace existing config. Can optionally include `auto_restart` field (boolean) to trigger server restart.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration replaced successfully |
| 400 | Invalid request or validation failed |
| 500 | Server error |


### `POST` /v1/core/config/reset

**Summary:** Reset configuration to defaults

**Operation ID:** `resetConfig`

Resets the entire system configuration to default values.
All existing configuration will be replaced with default values.
The configuration is saved to the config file after reset.

**Warning:** This operation will replace all configuration with default values.
Consider backing up your configuration before resetting.

**Auto Restart:**
- By default, the server will NOT automatically restart after configuration reset
- To enable automatic restart, set `auto_restart=true` as a query parameter or in the JSON body
- When `auto_restart=true`, the server will restart after 3 seconds if web server config changed

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| auto_restart | query | No | If set to `true`, the server will automatically restart after configuration reset. Default is `false` (no automatic restart). Can also be set in the JSON body as `auto_restart` field.  |

**Request body:**

- **application/json**
  - type: `object` — Optional JSON body. Can include `auto_restart` field (boolean) to trigger server restart.
    - **auto_restart**: boolean — If true, server will restart after reset

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration reset successfully |
| 500 | Server error |


### `DELETE` /v1/core/config/{path}

**Summary:** Delete configuration section (path parameter)

**Operation ID:** `deleteConfigSection`

Deletes a specific section of the system configuration at the given path.

**Path Format:**
- Use dots `.` as separators (e.g., `system.max_running_instances`)
- Forward slashes `/` with URL encoding (`%2F`) are NOT supported

**Recommended:** Use query parameter instead: `DELETE /v1/core/config?path=system/max_running_instances`

The configuration is saved to the config file after deletion.

**Auto Restart:**
- By default, the server will NOT automatically restart after configuration deletion
- To enable automatic restart, set `auto_restart=true` as a query parameter
- When `auto_restart=true`, the server will restart after 3 seconds if web server config changed

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Configuration path using dots as separators. Examples: `system.max_running_instances`, `system.web_server` Note: Forward slashes are NOT supported in path parameter (use query parameter instead)  |

| auto_restart | query | No | If set to `true`, the server will automatically restart after configuration deletion. Default is `false` (no automatic restart).  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration section deleted successfully |
| 400 | Invalid request (empty path) |
| 404 | Configuration section not found |
| 500 | Server error |


### `GET` /v1/core/config/{path}

**Summary:** Get configuration section (path parameter)

**Operation ID:** `getConfigSection`

Returns a specific section of the system configuration at the given path.

**Path Format:**
- Use dots `.` as separators (e.g., `system.max_running_instances`)
- Forward slashes `/` with URL encoding (`%2F`) are NOT supported due to Drogon routing limitations

**Recommended:** Use query parameter instead: `GET /v1/core/config?path=system/max_running_instances`

Example paths: `system.max_running_instances`, `system.web_server`, `gstreamer.decode_pipelines.auto`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Configuration path using dots as separators. Examples: `system.max_running_instances`, `system.web_server`, `gstreamer.decode_pipelines.auto` Note: Forward slashes are NOT supported in path parameter (use query parameter instead)  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration section |
| 400 | Invalid request (empty path) |
| 404 | Configuration section not found |
| 500 | Server error |


### `OPTIONS` /v1/core/config/{path}

**Summary:** CORS preflight for config path

**Operation ID:** `configPathOptions`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Configuration path |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PATCH` /v1/core/config/{path}

**Summary:** Update configuration section (path parameter)

**Operation ID:** `updateConfigSection`

Updates a specific section of the system configuration at the given path.

**Path Format:**
- Use dots `.` as separators (e.g., `system.max_running_instances`)
- Forward slashes `/` with URL encoding (`%2F`) are NOT supported

**Recommended:** Use query parameter instead: `PATCH /v1/core/config?path=system/max_running_instances`

Only the provided fields in the section will be updated.
The configuration is saved to the config file after update.

**Auto Restart:**
- By default, the server will NOT automatically restart after configuration changes
- To enable automatic restart, set `auto_restart=true` as a query parameter or in the JSON body
- When `auto_restart=true`, the server will restart after 3 seconds if web server config changed

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| path | path | Yes | Configuration path using dots as separators. Examples: `system.max_running_instances`, `system.web_server` Note: Forward slashes are NOT supported in path parameter (use query parameter instead)  |

| auto_restart | query | No | If set to `true`, the server will automatically restart after configuration update. Default is `false` (no automatic restart). Can also be set in the JSON body as `auto_restart` field.  |

**Request body:**

- **application/json**
  - type: `object` — Configuration section value to update

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration section updated successfully |
| 400 | Invalid request or validation failed |
| 500 | Server error |



## Core

### `GET` /hls/{instanceId}/segment_{segmentId}.ts

**Summary:** Get HLS segment

**Operation ID:** `getHlsSegment`

Get HLS video segment file (.ts) for an instance.
This endpoint serves individual video segment files that are referenced in the HLS playlist. Segments are generated automatically when HLS output is enabled for an instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| segmentId | path | Yes | Segment ID (e.g., "0", "1", "2") |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | HLS video segment file |
| 404 | Instance not found, HLS not enabled, or segment not found |
| 500 | Server error |


### `GET` /hls/{instanceId}/stream.m3u8

**Summary:** Get HLS playlist

**Operation ID:** `getHlsPlaylist`

Get HLS (HTTP Live Streaming) playlist file (.m3u8) for an instance.
This endpoint serves the HLS playlist file that clients use to stream video from the instance. The playlist contains references to video segments (.ts files) that are generated automatically.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | HLS playlist file |
| 404 | Instance not found or HLS not enabled |
| 500 | Server error |


### `GET` /v1/core/endpoints

**Summary:** Get endpoint statistics

**Operation ID:** `getEndpointsStats`

Returns statistics for each API endpoint including request counts, response times,
error rates, and other performance metrics.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Endpoint statistics |


### `GET` /v1/core/health

**Summary:** Health check endpoint

**Operation ID:** `getHealth`

Returns the health status of the API service

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Service is healthy |
| 500 | Service is unhealthy |


### `GET` /v1/core/license/check

**Summary:** Check license validity

**Operation ID:** `checkLicense`

Check if the license is valid and active

**Responses:**

| Code | Description |
|------|-------------|
| 200 | License check result |
| 500 | Server error |


### `OPTIONS` /v1/core/license/check

**Summary:** CORS preflight for license check

**Operation ID:** `checkLicenseOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/license/info

**Summary:** Get license information

**Operation ID:** `getLicenseInfo`

Get detailed information about the current license

**Responses:**

| Code | Description |
|------|-------------|
| 200 | License information |
| 500 | Server error |


### `OPTIONS` /v1/core/license/info

**Summary:** CORS preflight for license info

**Operation ID:** `getLicenseInfoOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/metrics

**Summary:** Get Prometheus format metrics

**Operation ID:** `getMetrics`

Returns system metrics in Prometheus format for monitoring and alerting.
This endpoint is typically used by Prometheus scrapers.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Prometheus format metrics |


### `GET` /v1/core/system/config

**Summary:** Get system configuration entities

**Operation ID:** `coreGetSystemConfigV1`

Returns system configuration entities with fieldId, displayName, type, value, group, and availableValues

**Responses:**

| Code | Description |
|------|-------------|
| 200 | System configuration entities |
| 500 | Internal server error |


### `OPTIONS` /v1/core/system/config

**Summary:** CORS preflight for system config

**Operation ID:** `coreSystemConfigOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/core/system/config

**Summary:** Update system configuration

**Operation ID:** `corePutSystemConfigV1`

Update system configuration entities

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SystemConfigUpdateRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration updated successfully |
| 406 | Not Acceptable - Invalid configuration |


### `GET` /v1/core/system/decoders

**Summary:** Get available system decoders

**Operation ID:** `coreGetDecodersV1`

Returns information about available hardware and software decoders in the system

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Decoder information |
| 500 | Internal server error |


### `OPTIONS` /v1/core/system/decoders

**Summary:** CORS preflight for system decoders

**Operation ID:** `coreSystemDecodersOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/system/info

**Summary:** Get system hardware information

**Operation ID:** `getSystemInfo`

Returns detailed hardware information including CPU, GPU, RAM, Disk, Mainboard, OS, and Battery

**Responses:**

| Code | Description |
|------|-------------|
| 200 | System hardware information |
| 500 | Server error |


### `OPTIONS` /v1/core/system/info

**Summary:** CORS preflight for system info

**Operation ID:** `getSystemInfoOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/system/preferences

**Summary:** Get system preferences

**Operation ID:** `coreGetPreferencesV1`

Returns system preferences from rtconfig.json. These settings affect VMS plugins UI and Web Panel.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | System preferences |
| 500 | Internal server error |


### `OPTIONS` /v1/core/system/preferences

**Summary:** CORS preflight for system preferences

**Operation ID:** `coreSystemPreferencesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/system/registry

**Summary:** Get registry key value

**Operation ID:** `coreGetRegistryKeyValueV1`

Returns registry key value from the system

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| key | query | Yes | Registry key path |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Registry key value |
| 404 | Registry key not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/system/registry

**Summary:** CORS preflight for system registry

**Operation ID:** `coreSystemRegistryOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `OPTIONS` /v1/core/system/shutdown

**Summary:** CORS preflight for system shutdown

**Operation ID:** `coreSystemShutdownOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/system/shutdown

**Summary:** Shutdown system

**Operation ID:** `corePostShutdownV1`

Initiates system shutdown

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Shutdown initiated |


### `GET` /v1/core/system/status

**Summary:** Get system status

**Operation ID:** `getSystemStatus`

Returns current system status including CPU usage, RAM usage, load average, and uptime

**Responses:**

| Code | Description |
|------|-------------|
| 200 | System status information |
| 500 | Server error |


### `OPTIONS` /v1/core/system/status

**Summary:** CORS preflight for system status

**Operation ID:** `getSystemStatusOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/version

**Summary:** Get version information

**Operation ID:** `getVersion`

Returns version information about the API service

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Version information |


### `GET` /v1/core/watchdog

**Summary:** Get watchdog status

**Operation ID:** `getWatchdogStatus`

Returns watchdog and health monitor statistics including instance health checks,
restart counts, and monitoring information.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Watchdog status information |



## Fonts

### `GET` /v1/core/font/list

**Summary:** List uploaded font files

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
| 200 | List of font files |
| 500 | Server error |


### `OPTIONS` /v1/core/font/upload

**Summary:** CORS preflight for font upload

**Operation ID:** `uploadFontOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/font/upload

**Summary:** Upload a font file

**Operation ID:** `uploadFont`

Uploads a font file (TTF, OTF, WOFF, WOFF2, etc.) to the server.
The file will be saved in the fonts directory. You can specify a subdirectory
using the `directory` query parameter (e.g., `projects/myproject/fonts`).
If the directory doesn't exist, it will be created automatically.
If no directory is specified, files will be saved in the base fonts directory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Subdirectory path where the font file should be saved. Use forward slashes to separate directory levels (e.g., "projects/myproject/fonts", "themes/custom", "languages/vietnamese"). The directory will be created automatically if it doesn't exist. If not specified, files will be saved in the base fonts directory.  |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **files**: array — Font file(s) to upload. Select multiple files to upload at once (.ttf, .otf, .woff, .woff2, .eot, .ttc)
    - **file**: string — Single font file to upload (alternative to files array)
- **application/octet-stream**
  - type: `string` — Font file as binary data

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Font file(s) uploaded successfully |
| 400 | Invalid request (missing file, invalid format, etc.) |
| 401 | Unauthorized (missing or invalid API key) |
| 409 | File already exists |
| 500 | Server error |


### `DELETE` /v1/core/font/{fontName}

**Summary:** Delete a font file

**Operation ID:** `deleteFont`

Deletes a font file from the server.
You can specify a subdirectory using the `directory` query parameter if the file is in a subdirectory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| fontName | path | Yes | Name of the font file to delete |

| directory | query | No | Subdirectory path where the font file is located (e.g., "projects/myproject/fonts", "themes/custom"). If not specified, the file is assumed to be in the base fonts directory.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Font file deleted successfully |
| 400 | Invalid request |
| 404 | Font file not found |
| 500 | Server error |


### `OPTIONS` /v1/core/font/{fontName}

**Summary:** CORS preflight for font operations

**Operation ID:** `fontOptionsHandler`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/core/font/{fontName}

**Summary:** Rename a font file

**Operation ID:** `renameFont`

Renames a font file on the server.
You can specify a subdirectory using the `directory` query parameter if the file is in a subdirectory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| fontName | path | Yes | Current name of the font file to rename |

| directory | query | No | Subdirectory path where the font file is located (e.g., "projects/myproject/fonts", "themes/custom"). If not specified, the file is assumed to be in the base fonts directory.  |

**Request body:**

- **application/json**
  - type: `object`
    - **newName**: string — New name for the font file (must have valid font file extension)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Font file renamed successfully |
| 400 | Invalid request |
| 404 | Font file not found |
| 409 | New name already exists |
| 500 | Server error |



## Groups

### `GET` /v1/core/groups

**Summary:** List all groups

**Operation ID:** `listGroups`

Returns a list of all groups with summary information including group name, description, instance count, and metadata.

The response includes:
- Total count of groups
- Number of default groups
- Number of custom groups
- Summary information for each group (ID, name, description, instance count, etc.)

This endpoint is useful for getting an overview of all groups in the system without fetching detailed information for each one.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of groups |
| 500 | Server error |


### `OPTIONS` /v1/core/groups

**Summary:** CORS preflight for groups

**Operation ID:** `listGroupsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/groups

**Summary:** Create a new group

**Operation ID:** `createGroup`

Creates a new group for organizing instances. Groups help organize and manage multiple instances together.

**Group Properties:**
- **groupId** (required): Unique identifier for the group (alphanumeric, underscores, hyphens only)
- **groupName** (optional): Display name for the group (defaults to groupId if not provided)
- **description** (optional): Description of the group's purpose

**Validation:**
- Group ID must be unique
- Group ID must match pattern: `^[A-Za-z0-9_-]+$`
- Group name must match pattern: `^[A-Za-z0-9 -_]+$`

**Persistence:**
- Groups are automatically saved to storage
- Group files are stored in `/var/lib/edge_ai_api/groups` (configurable via `GROUPS_DIR` environment variable)

**Returns:** The created group information including timestamps.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateGroupRequest
  - example: `{'groupId': 'cameras', 'groupName': 'Security Cameras', 'description': 'Group for security camera instances'}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Group created successfully |
| 400 | Bad request (validation failed or group already exists) |
| 500 | Internal server error |


### `DELETE` /v1/core/groups/{groupId}

**Summary:** Delete a group

**Operation ID:** `deleteGroup`

Deletes a group from the system. This operation permanently removes the group.

**Prerequisites:**
- Group must exist
- Group must not be a default group
- Group must not be read-only
- Group must have no instances (all instances must be removed or moved to other groups first)

**Behavior:**
- Group configuration will be removed from memory
- Group file will be deleted from storage
- All resources associated with the group will be released

**Note:** This operation cannot be undone. Once deleted, the group must be recreated using the create group endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Group ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Group deleted successfully |
| 400 | Failed to delete (group has instances, is default, or is read-only) |
| 404 | Group not found |
| 500 | Internal server error |


### `GET` /v1/core/groups/{groupId}

**Summary:** Get group details

**Operation ID:** `getGroup`

Returns detailed information about a specific group including group metadata and list of instance IDs.

The response includes:
- Group configuration (ID, name, description)
- Group metadata (isDefault, readOnly, timestamps)
- Instance count
- List of instance IDs in the group

The group must exist. If the group is not found, a 404 error will be returned.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Group ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Group details |
| 404 | Group not found |
| 500 | Server error |


### `OPTIONS` /v1/core/groups/{groupId}

**Summary:** CORS preflight for group

**Operation ID:** `getGroupOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/core/groups/{groupId}

**Summary:** Update a group

**Operation ID:** `updateGroup`

Updates an existing group's information. Only provided fields will be updated.

**Updateable Fields:**
- **groupName**: Update the display name of the group
- **description**: Update the description of the group

**Restrictions:**
- Group must exist
- Group must not be read-only
- Group ID cannot be changed

**Behavior:**
- Only provided fields will be updated
- Other fields remain unchanged
- Updated timestamp is automatically set

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Group ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateGroupRequest
  - example: `{'groupName': 'Updated Group Name', 'description': 'Updated description'}`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Group updated successfully |
| 400 | Bad request (validation failed or group is read-only) |
| 404 | Group not found |
| 500 | Internal server error |


### `GET` /v1/core/groups/{groupId}/instances

**Summary:** Get instances in a group

**Operation ID:** `getGroupInstances`

Returns a list of all instances that belong to a specific group.

The response includes:
- Group ID
- List of instances with summary information (ID, display name, solution, running status)
- Total count of instances in the group

**Use Cases:**
- View all instances in a specific group
- Monitor group status and performance
- Manage instances by group

The group must exist. If the group is not found, a 404 error will be returned.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| groupId | path | Yes | Group ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of instances in the group |
| 404 | Group not found |
| 500 | Server error |


### `OPTIONS` /v1/core/groups/{groupId}/instances

**Summary:** CORS preflight for group instances

**Operation ID:** `getGroupInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |



## Instances

### `DELETE` /api/v1/instances/{instance_id}/fps

**Summary:** Reset FPS to default

**Operation ID:** `resetInstanceFps`

Reset the FPS configuration of a specific instance to the default value (5 FPS).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instance_id | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | FPS configuration reset to default |
| 404 | Instance not found |
| 500 | Server error |


### `GET` /api/v1/instances/{instance_id}/fps

**Summary:** Get current FPS configuration

**Operation ID:** `getInstanceFps`

Retrieve the current FPS configuration for a specific instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instance_id | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Current FPS configuration |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /api/v1/instances/{instance_id}/fps

**Summary:** CORS preflight for FPS endpoints

**Operation ID:** `instanceFpsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /api/v1/instances/{instance_id}/fps

**Summary:** Set FPS configuration

**Operation ID:** `setInstanceFps`

Set or update the FPS configuration for a specific instance. The FPS value must be a positive integer (greater than 0).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instance_id | path | Yes | Instance ID (UUID) |

**Request body:**

- **application/json**
  - type: `object`
    - **fps**: integer — The desired frame processing rate (must be greater than 0)
  - example: `{'fps': 10}`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | FPS configuration updated successfully |
| 400 | Bad Request - Invalid FPS value (e.g., negative or zero) |
| 404 | Instance not found |
| 500 | Server error |


### `DELETE` /v1/core/instance

**Summary:** Delete all instances

**Operation ID:** `deleteAllInstances`

Deletes all instances in the system. This operation permanently removes all instances and stops their pipelines if running.

**Behavior:**
- All instances are deleted concurrently for optimal performance
- If instances are currently running, they will be stopped first
- All instance configurations will be removed from memory
- Persistent instance configuration files will be deleted from storage
- All resources associated with instances will be released

**Response:**
- Returns a summary of the deletion operation
- Includes detailed results for each instance (success/failure status)
- Provides counts of total, deleted, and failed instances

**Note:** This operation cannot be undone. Once deleted, instances must be recreated using the create instance endpoint.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Delete all instances operation completed |
| 500 | Server error |
| 503 | Service unavailable (registry busy) |


### `GET` /v1/core/instance

**Summary:** List all instances

**Operation ID:** `listInstances`

Returns a list of all AI instances with summary information including running status, solution information, and basic configuration details.

The response includes:
- Total count of instances
- Number of running instances
- Number of stopped instances
- Summary information for each instance (ID, display name, running status, solution, etc.)

This endpoint is useful for getting an overview of all instances in the system without fetching detailed information for each one.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of instances |
| 500 | Server error |


### `OPTIONS` /v1/core/instance

**Summary:** CORS preflight for list instances

**Operation ID:** `listInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance

**Summary:** Create a new AI instance

**Operation ID:** `createInstance`

Creates and registers a new AI instance with the specified configuration. Returns the instance ID (UUID) that can be used to control the instance.

**Async pipeline build (when solution is provided):**
- The API returns **201 Created** as soon as the instance is **registered** (ID allocated, stored in registry). - The pipeline is built **asynchronously in the background** (can take 30+ seconds). The response is not delayed until the pipeline is ready. - The response body includes **building**, **status**, and **message** so the client can tell whether the instance is ready: - **building: true**, **status: "building"** — pipeline is still being built; do not call Start until **status** becomes **"ready"**. - **building: false**, **status: "ready"** — pipeline is built; instance can be started. - To know when the instance is ready, poll **GET /v1/core/instance/{instanceId}** until **status** is **"ready"** (or **building** is false), or wait for the build to complete before calling Start. If Start is called while **building** is true, the API returns an error: "Pipeline is still being built in background".

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
- Instance configuration files are stored in `/var/lib/edge_ai_api/instances` (configurable via `INSTANCES_DIR` environment variable)

**Auto Start:**
- If `autoStart: true`, the instance will automatically start **after** the pipeline build completes (in background). Until then, the instance is in **building** state.
- Do not rely on 201 response alone to mean "instance is ready"; check **status** and **building** in the response (or poll GET instance) before calling Start.

**Returns:** The created instance information including the generated UUID. When a solution is provided, the response includes **building**, **status**, and **message** so the client can know whether the instance is ready for Start.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateInstanceRequest

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Instance registered successfully. When a solution is provided, the pipeline is built asynchronously; the response may include **building: true** and **status: "building"**. Poll GET /v1/core/instance/{instanceId} until **status** is **"ready"** before calling Start, or wait for the build to complete.  |
| 400 | Bad request (validation failed) |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/batch/restart

**Summary:** CORS preflight for batch restart

**Operation ID:** `batchRestartInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/batch/restart

**Summary:** Restart multiple instances concurrently

**Operation ID:** `batchRestartInstances`

Restarts multiple instances concurrently by stopping and then starting them. All operations run in parallel for optimal performance.

**Request body:**

- **application/json**
  - type: `object`
    - **instanceIds**: array — Array of instance IDs to restart

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Batch restart operation completed |
| 400 | Invalid request |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/batch/start

**Summary:** CORS preflight for batch start

**Operation ID:** `batchStartInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/batch/start

**Summary:** Start multiple instances concurrently

**Operation ID:** `batchStartInstances`

Starts multiple instances concurrently for faster batch operations. All operations run in parallel for optimal performance.

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
    - **instanceIds**: array — Array of instance IDs to start

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Batch start operation completed |
| 400 | Invalid request |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/batch/stop

**Summary:** CORS preflight for batch stop

**Operation ID:** `batchStopInstancesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/batch/stop

**Summary:** Stop multiple instances concurrently

**Operation ID:** `batchStopInstances`

Stops multiple instances concurrently for faster batch operations. All operations run in parallel for optimal performance.

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
    - **instanceIds**: array — Array of instance IDs to stop

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Batch stop operation completed |
| 400 | Invalid request |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/quick

**Summary:** CORS preflight for quick instance

**Operation ID:** `createQuickInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/quick

**Summary:** Create instance quickly

**Operation ID:** `createQuickInstance`

Creates a new AI instance with simplified parameters. Automatically maps solution types to appropriate solution IDs and provides default values.
This endpoint is designed for quick instance creation with minimal configuration. It accepts simplified parameters and automatically: - Maps solution types (e.g., "face_detection", "ba_crossline") to solution IDs - Provides default values for missing parameters - Converts development paths to production paths

**Request body:**

- **application/json**
  - type: `object`
    - **name**: string — Instance display name
    - **solutionType**: string — Solution type (e.g., "face_detection", "ba_crossline", "ba_jam", "ba_stop")
    - **group**: string — Instance group name
    - **persistent**: boolean — Whether to persist instance configuration
    - **autoStart**: boolean — Whether to automatically start the instance
    - **input**: object — 
    - **inputType**: string — Input type (alternative to input.type)
    - **output**: object — 
    - **outputType**: string — Output type (alternative to output.type)

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Instance created successfully |
| 400 | Invalid request |
| 500 | Server error |


### `GET` /v1/core/instance/status/summary

**Summary:** Get instance status summary

**Operation ID:** `getStatusSummary`

Returns a summary of instance status including total number of configured instances,
number of running instances, and number of stopped instances.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance status summary |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/status/summary

**Summary:** CORS preflight for status summary endpoint

**Operation ID:** `statusSummaryOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `DELETE` /v1/core/instance/{instanceId}

**Summary:** Delete an instance

**Operation ID:** `deleteInstance`

Deletes an instance and stops its pipeline if running. This operation permanently removes the instance from the system.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance deleted successfully |
| 404 | Instance not found |
| 500 | Server error |


### `GET` /v1/core/instance/{instanceId}

**Summary:** Get instance details

**Operation ID:** `getInstance`

Returns detailed information about a specific instance including full configuration, status, and runtime information.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance details |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}

**Summary:** CORS preflight for update instance

**Operation ID:** `updateInstanceOptions`

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PATCH` /v1/core/instance/{instanceId}

**Summary:** Patch instance with partial data

**Operation ID:** `corePatchInstanceV1`

Updates an instance with partial data. This is similar to PUT but explicitly designed for partial updates where only specific fields need to be changed.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateInstanceRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Instance updated successfully |
| 400 | Bad request (parsing error or invalid arguments) |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/core/instance/{instanceId}

**Summary:** Update instance information

**Operation ID:** `updateInstance`

Updates configuration of an existing instance. Only provided fields will be updated. Supports two update formats:

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

| instanceId | path | Yes | Instance ID (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateInstanceRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance updated successfully |
| 400 | Invalid request or validation failed |
| 404 | Instance not found |
| 500 | Server error |


### `GET` /v1/core/instance/{instanceId}/classes

**Summary:** Get instance classes

**Operation ID:** `getInstanceClasses`

Get the list of detection classes supported by the instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of supported classes |
| 404 | Instance not found |
| 500 | Server error |


### `GET` /v1/core/instance/{instanceId}/config

**Summary:** Get instance configuration

**Operation ID:** `getConfig`

Returns the configuration of an instance. This endpoint provides the configuration settings of the instance, which are different from runtime state. Configuration includes settings like AutoRestart, AutoStart, Detector settings, Input configuration, etc.

**Note:** This returns the configuration format, not the runtime state. For runtime state information, use the instance details endpoint.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/config

**Summary:** CORS preflight for set config

**Operation ID:** `setConfigOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/config

**Summary:** Set config value at a specific path

**Operation ID:** `setConfig`

Sets a configuration value at the specified path. The path is a key in the configuration, and for nested structures, it will be nested keys separated by forward slashes "/".

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

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SetConfigRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Config value set successfully |
| 400 | Bad request (invalid input or missing required fields) |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/consume_events

**Summary:** Consume events from instance

**Operation ID:** `coreConsumeEventsV1`

Consumes events from the instance event queue. Events are published by the instance processing pipeline and can be consumed via this endpoint.
**Event Format:** - Each event has a `dataType` (e.g., "detection", "tracking", "analytics") and a `jsonObject` (JSON serialized string) - Events follow the schema at https://bin.cvedia.com/schema/
**Behavior:** - Returns all available events from the queue - Events are removed from the queue after consumption - Returns 204 No Content if no events are available - Returns 200 OK with array of events if events are available

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Events retrieved successfully |
| 204 | No events available |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/consume_events

**Summary:** CORS preflight for consume events endpoint

**Operation ID:** `consumeEventsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/instance/{instanceId}/frame

**Summary:** Get last frame from instance

**Operation ID:** `getLastFrame`

Returns the last processed frame from a running instance. The frame is encoded as JPEG and returned as a base64-encoded string.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Last frame retrieved successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}/frame

**Summary:** CORS preflight for get last frame

**Operation ID:** `getLastFrameOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `OPTIONS` /v1/core/instance/{instanceId}/input

**Summary:** CORS preflight for set input

**Operation ID:** `setInstanceInputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/input

**Summary:** Set input source for an instance

**Operation ID:** `setInstanceInput`

Sets the input source configuration for an instance. Replaces the Input field in the Instance File according to the request.

**Input Types:**
- **RTSP**: Uses `rtsp_src` node for RTSP streams
- **HLS**: Uses `hls_src` node for HLS streams
- **Manual**: Uses `v4l2_src` node for V4L2 devices

The instance must exist and not be read-only. If the instance is currently running, it will be automatically restarted to apply the changes.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SetInputRequest
  - example: `{'type': 'Manual', 'uri': 'http://localhost'}`

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Input settings updated successfully |
| 400 | Bad request (invalid input or missing required fields) |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/jams/batch

**Summary:** CORS preflight for batch update jams

**Operation ID:** `batchUpdateJamsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/jams/batch

**Summary:** Batch update jam zones

**Operation ID:** `batchUpdateJams`

Update multiple jam zones for a ba_jam instance in a single request.
This endpoint allows updating multiple jam zones at once, which is more efficient than updating them individually. The instance will be restarted automatically to apply the changes if it is currently running.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID (UUID) |

**Request body:**

- **application/json**
  - type: `array`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Jam zones updated successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}/lines/batch

**Summary:** CORS preflight for batch update lines

**Operation ID:** `batchUpdateLinesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/lines/batch

**Summary:** Batch update crossing lines

**Operation ID:** `batchUpdateLines`

Update multiple crossing lines for a ba_crossline instance in a single request.
This endpoint allows updating multiple crossing lines at once, which is more efficient than updating them individually. The instance will be restarted automatically to apply the changes if it is currently running.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID (UUID) |

**Request body:**

- **application/json**
  - type: `array`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Lines updated successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}/load

**Summary:** CORS preflight for load instance

**Operation ID:** `loadInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/load

**Summary:** Load instance into memory

**Operation ID:** `coreLoadInstanceV1`

Loads an instance into memory and initializes state storage. This is different from starting an instance - loading prepares the instance and enables state management, while starting begins the processing pipeline.

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

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Instance loaded successfully |
| 404 | Instance not found |
| 406 | Cannot load instance (already loaded or invalid state) |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/output

**Summary:** Get instance output and processing results

**Operation ID:** `getInstanceOutput`

Returns real-time output and processing results for a specific instance at the time of the request.
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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance output and processing results |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}/output

**Summary:** CORS preflight for get instance output

**Operation ID:** `getInstanceOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `OPTIONS` /v1/core/instance/{instanceId}/output/hls

**Summary:** CORS preflight for HLS output endpoint

**Operation ID:** `hlsOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/output/hls

**Summary:** Configure HLS output for instance

**Operation ID:** `coreSetOutputHlsV1`

Enables or disables HLS (HTTP Live Streaming) output for an instance. When enabled, returns the URI of the HLS stream.
**HLS Output:** - HLS stream is accessible via HTTP - Playlist file (.m3u8) is generated automatically - Video segments (.ts files) are generated automatically - Stream can be played in VLC, web browsers, or other HLS-compatible players

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enabled**: boolean — Enable or disable HLS output

**Responses:**

| Code | Description |
|------|-------------|
| 200 | HLS output configured successfully |
| 204 | HLS output disabled successfully |
| 404 | Instance not found |
| 406 | Could not set HLS output |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/output/rtsp

**Summary:** CORS preflight for RTSP output endpoint

**Operation ID:** `rtspOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/output/rtsp

**Summary:** Configure RTSP output for instance

**Operation ID:** `coreSetOutputRtspV1`

Enables or disables RTSP (Real-Time Streaming Protocol) output for an instance at the specified URI.
**RTSP Output:** - RTSP stream can be accessed via RTSP clients (VLC, ffplay, etc.) - URI can be provided in request body or default will be used - Stream supports multiple RTSP clients simultaneously

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Request body:**

- **application/json**
  - type: `object`
    - **enabled**: boolean — Enable or disable RTSP output
    - **uri**: string — RTSP URI (optional, default will be used if not provided)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | RTSP output configured successfully |
| 400 | Bad request (parsing error or invalid arguments) |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/output/stream

**Summary:** Get stream output configuration

**Operation ID:** `getStreamOutput`

Returns the current stream output configuration for an instance. This endpoint retrieves the stream output settings including whether it is enabled and the configured URI.

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

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Stream output configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/output/stream

**Summary:** CORS preflight for stream output endpoints

**Operation ID:** `streamOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/output/stream

**Summary:** Configure stream/record output

**Operation ID:** `configureStreamOutput`

Configures stream or record output for an instance. This endpoint allows you to enable or disable output and set either a stream URI or a local path for recording.

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

| instanceId | path | Yes | The unique identifier of the instance |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ConfigureStreamOutputRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Successfully configured stream output |
| 400 | Bad request (invalid input or missing required fields) |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/preview

**Summary:** Get instance preview

**Operation ID:** `getInstancePreview`

Get preview image or frame from the instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Preview image |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}/push/compressed

**Summary:** CORS preflight for push compressed frame

**Operation ID:** `pushCompressedFrameOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/push/compressed

**Summary:** Push compressed frame into instance

**Operation ID:** `corePostCompressedFrameV1`

Push a compressed frame (JPEG, PNG, BMP, etc.) into an instance for processing. The frame will be decoded using OpenCV and processed by the instance pipeline.
**Supported Formats:** - JPEG/JPEG - PNG - BMP - TIFF - WebP - Other formats supported by OpenCV `imdecode()`
**Request Format:** - Content-Type: `multipart/form-data` - Field `frame`: Binary compressed frame data - Field `timestamp`: int64 timestamp (optional, defaults to current time)
The instance must be running to accept frames. Frames are queued and processed asynchronously.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **frame**: string — Compressed frame data (JPEG, PNG, BMP, etc.)
    - **timestamp**: integer — Frame timestamp in milliseconds (optional)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Frame pushed successfully |
| 400 | Bad request (invalid frame data or missing required fields) |
| 404 | Instance not found |
| 409 | Instance is not currently running |
| 500 | Internal server error (decode failure, queue full, etc.) |


### `OPTIONS` /v1/core/instance/{instanceId}/push/encoded/{codecId}

**Summary:** CORS preflight for push encoded frame

**Operation ID:** `pushEncodedFrameOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/push/encoded/{codecId}

**Summary:** Push encoded frame into instance

**Operation ID:** `corePostEncodedFrameV1`

Push an encoded frame (H.264/H.265) into an instance for processing. The frame will be decoded and processed by the instance pipeline.
**Supported Codecs:** - `h264`: H.264/AVC - `h265`: H.265/HEVC - `hevc`: Alias for h265
**Request Format:** - Content-Type: `multipart/form-data` - Field `frame`: Binary encoded frame data - Field `timestamp`: int64 timestamp (optional, defaults to current time)
The instance must be running to accept frames. Frames are queued and processed asynchronously.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

| codecId | path | Yes | Codec identifier (h264, h265, or hevc) |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **frame**: string — Encoded frame data (H.264/H.265)
    - **timestamp**: integer — Frame timestamp in milliseconds (optional)

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Frame pushed successfully |
| 400 | Bad request (unsupported codec, invalid frame data, or missing required fields) |
| 404 | Instance not found |
| 409 | Instance is not currently running |
| 500 | Internal server error (decode failure, queue full, etc.) |


### `OPTIONS` /v1/core/instance/{instanceId}/restart

**Summary:** CORS preflight for restart instance

**Operation ID:** `restartInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/restart

**Summary:** Restart an instance

**Operation ID:** `restartInstance`

Restarts an instance by stopping it (if running) and then starting it again. This is useful for applying configuration changes or recovering from errors.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance restarted successfully |
| 400 | Failed to restart instance |
| 404 | Instance not found |
| 500 | Server error |


### `POST` /v1/core/instance/{instanceId}/start

**Summary:** Start an instance

**Operation ID:** `startInstance`

Starts the pipeline for an instance. The instance must have a pipeline configured and a valid solution.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance started successfully |
| 400 | Failed to start instance |
| 404 | Instance not found |
| 500 | Server error |


### `GET` /v1/core/instance/{instanceId}/state

**Summary:** Get runtime state of instance

**Operation ID:** `coreGetInstanceStateV1`

Returns the runtime state of an instance. State is different from config - state contains runtime settings that only exist when the instance is loaded, while config contains persistent settings that are stored in files.

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

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance state retrieved successfully |
| 404 | Instance not found |
| 406 | Instance not loaded |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/state

**Summary:** CORS preflight for instance state

**Operation ID:** `instanceStateOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/state

**Summary:** Set runtime state value at a specific path

**Operation ID:** `corePostInstanceStateV1`

Sets a runtime state value at the specified path. State is different from config - state contains runtime settings that only exist when the instance is loaded, while config contains persistent settings.

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

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SetStateRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | State value set successfully |
| 400 | Bad request (invalid input, missing required fields, or invalid JSON) |
| 404 | Instance not found |
| 406 | Instance not loaded |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/statistics

**Summary:** Get instance statistics

**Operation ID:** `getStatistics`

Returns real-time statistics for a specific instance. Statistics include frames processed, framerate, latency, resolution, and queue information.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Statistics retrieved successfully |
| 404 | Instance not found or not running |
| 400 | Invalid request |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/statistics

**Summary:** CORS preflight for get statistics

**Operation ID:** `getStatisticsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/stop

**Summary:** Stop an instance

**Operation ID:** `stopInstance`

Stops the pipeline for an instance, halting all processing and releasing resources.

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

| instanceId | path | Yes | Instance ID (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Instance stopped successfully |
| 400 | Failed to stop instance |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}/stops/batch

**Summary:** CORS preflight for batch update stops

**Operation ID:** `batchUpdateStopsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/stops/batch

**Summary:** Batch update stop zones

**Operation ID:** `batchUpdateStops`

Update multiple stop zones for a ba_stop instance in a single request.
This endpoint allows updating multiple stop zones at once, which is more efficient than updating them individually. The instance will be restarted automatically to apply the changes if it is currently running.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID (UUID) |

**Request body:**

- **application/json**
  - type: `array`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Stop zones updated successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Server error |


### `OPTIONS` /v1/core/instance/{instanceId}/unload

**Summary:** CORS preflight for unload instance

**Operation ID:** `unloadInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/unload

**Summary:** Unload instance from memory

**Operation ID:** `coreUnloadInstanceV1`

Unloads an instance from memory, clears state storage, and releases resources. This is different from stopping an instance - unloading removes the instance from memory and clears runtime state, while stopping only halts the processing pipeline.

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

| instanceId | path | Yes | Unique identifier of the instance (UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Instance unloaded successfully |
| 404 | Instance not found |
| 406 | Instance not loaded |
| 500 | Internal server error |



## Lines Core

### `DELETE` /v1/core/instance/{instanceId}/lines

**Summary:** Delete all crossing lines

**Operation ID:** `deleteAllLines`

Deletes all crossing lines for a ba_crossline instance.

**Real-time Update:** Lines are immediately removed from the running video stream without requiring instance restart.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | All lines deleted successfully |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/lines

**Summary:** Get all crossing lines

**Operation ID:** `getAllLines`

Returns all crossing lines configured for a ba_crossline instance.
Lines are used for counting objects crossing a defined line in the video stream.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of crossing lines |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/lines

**Summary:** CORS preflight for lines endpoints

**Operation ID:** `linesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/instance/{instanceId}/lines

**Summary:** Create one or more crossing lines

**Operation ID:** `createLine`

Creates one or more crossing lines for a ba_crossline instance.
The lines will be drawn on the video stream and used for counting objects.

**Request Format:**
- Single line: Send a single object (CreateLineRequest)
- Multiple lines: Send an array of objects [CreateLineRequest, ...]

**Real-time Update:** Lines are immediately applied to the running video stream without requiring instance restart.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

**Request body:**

- **application/json**
  - type: `object`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Line(s) created successfully. Returns single line object for single request, or object with metadata for array request. |
| 400 | Bad request (invalid coordinates or parameters) |
| 404 | Instance not found |
| 500 | Internal server error |


### `DELETE` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** Delete a specific crossing line

**Operation ID:** `deleteLine`

Deletes a specific crossing line by ID.

**Real-time Update:** The line is immediately removed from the running video stream without requiring instance restart.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| lineId | path | Yes | The unique identifier of the line to delete |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Line deleted successfully |
| 404 | Instance or line not found |
| 500 | Internal server error |


### `GET` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** Get a specific crossing line

**Operation ID:** `getLine`

Returns a specific crossing line by ID for a ba_crossline instance.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| lineId | path | Yes | The unique identifier of the line to retrieve |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Line retrieved successfully |
| 404 | Instance or line not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** CORS preflight for line endpoints

**Operation ID:** `lineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/core/instance/{instanceId}/lines/{lineId}

**Summary:** Update a specific crossing line

**Operation ID:** `updateLine`

Updates a specific crossing line by ID for a ba_crossline instance.
The line will be updated on the video stream and used for counting objects.

**Real-time Update:** The line is immediately applied to the running video stream without requiring instance restart.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | The unique identifier of the instance |

| lineId | path | Yes | The unique identifier of the line to update |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateLineRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Line updated successfully |
| 400 | Bad request (invalid coordinates or parameters) |
| 404 | Instance or line not found |
| 500 | Internal server error |



## Lines SecuRT

### `OPTIONS` /v1/securt/instance/{instanceId}/line/counting

**Summary:** CORS preflight for counting line

**Operation ID:** `createCountingLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/line/counting

**Summary:** Create counting line

**Operation ID:** `createCountingLine`

Creates a new counting line for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CountingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Counting line created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/counting/{lineId}

**Summary:** CORS preflight for counting line with ID

**Operation ID:** `createCountingLineWithIdOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/securt/instance/{instanceId}/line/counting/{lineId}

**Summary:** Create counting line with ID

**Operation ID:** `createCountingLineWithId`

Creates a counting line with a specific ID for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| lineId | path | Yes | Line ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CountingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Counting line created successfully |
| 400 | Invalid request |
| 409 | Line already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/crossing

**Summary:** CORS preflight for crossing line

**Operation ID:** `createCrossingLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/line/crossing

**Summary:** Create crossing line

**Operation ID:** `createCrossingLine`

Creates a new crossing line for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Crossing line created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/crossing/{lineId}

**Summary:** CORS preflight for crossing line with ID

**Operation ID:** `createCrossingLineWithIdOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/securt/instance/{instanceId}/line/crossing/{lineId}

**Summary:** Create crossing line with ID

**Operation ID:** `createCrossingLineWithId`

Creates a crossing line with a specific ID for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| lineId | path | Yes | Line ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CrossingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Crossing line created successfully |
| 400 | Invalid request |
| 409 | Line already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/tailgating

**Summary:** CORS preflight for tailgating line

**Operation ID:** `createTailgatingLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/line/tailgating

**Summary:** Create tailgating line

**Operation ID:** `createTailgatingLine`

Creates a new tailgating line for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/TailgatingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Tailgating line created successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/tailgating/{lineId}

**Summary:** CORS preflight for tailgating line with ID

**Operation ID:** `createTailgatingLineWithIdOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/securt/instance/{instanceId}/line/tailgating/{lineId}

**Summary:** Create tailgating line with ID

**Operation ID:** `createTailgatingLineWithId`

Creates a tailgating line with a specific ID for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| lineId | path | Yes | Line ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/TailgatingLineWrite

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Tailgating line created successfully |
| 400 | Invalid request |
| 409 | Line already exists |
| 404 | Instance not found |
| 500 | Internal server error |


### `DELETE` /v1/securt/instance/{instanceId}/line/{lineId}

**Summary:** Delete line by ID

**Operation ID:** `deleteLine`

Deletes a specific line by its ID

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

| lineId | path | Yes | Line ID |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Line deleted successfully |
| 400 | Invalid request |
| 404 | Instance or line not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/line/{lineId}

**Summary:** CORS preflight for line by ID

**Operation ID:** `deleteLineOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `DELETE` /v1/securt/instance/{instanceId}/lines

**Summary:** Delete all lines for SecuRT instance

**Operation ID:** `deleteAllSecuRTLines`

Deletes all analytics lines for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | All lines deleted successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/lines

**Summary:** Get analytics lines for SecuRT instance

**Operation ID:** `getSecuRTLines`

Returns all analytics lines configured for a SecuRT instance. This includes crossing lines, counting lines, and tailgating lines.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Analytics lines retrieved successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/lines

**Summary:** CORS preflight for lines

**Operation ID:** `getSecuRTLinesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |



## Logs

### `GET` /v1/core/log

**Summary:** List all log files by category

**Operation ID:** `listLogFiles`

Returns a list of all log files organized by category (api, instance, sdk_output, general).
Each category contains an array of log files with their date, size, and path.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of log files by category |
| 500 | Server error |


### `OPTIONS` /v1/core/log

**Summary:** CORS preflight for list logs

**Operation ID:** `listLogFilesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/log/{category}

**Summary:** Get logs by category

**Operation ID:** `getLogsByCategory`

Returns logs from a specific category with optional filtering.

**Categories:** api, instance, sdk_output, general

**Query Parameters:**
- `level`: Filter by log level (case-insensitive, e.g., INFO, ERROR, WARNING)
- `from`: Filter logs from timestamp (ISO 8601 format, e.g., 2024-01-01T00:00:00.000Z)
- `to`: Filter logs to timestamp (ISO 8601 format, e.g., 2024-01-01T23:59:59.999Z)
- `tail`: Get only the last N lines from the latest log file (integer)

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| category | path | Yes | Log category |

| level | query | No | Filter by log level (case-insensitive) |

| from | query | No | Filter logs from timestamp (ISO 8601) |

| to | query | No | Filter logs to timestamp (ISO 8601) |

| tail | query | No | Get only the last N lines from the latest log file |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Logs from the specified category |
| 400 | Bad request (invalid category or parameters) |
| 500 | Server error |


### `OPTIONS` /v1/core/log/{category}

**Summary:** CORS preflight for get logs by category

**Operation ID:** `getLogsByCategoryOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/log/{category}/{date}

**Summary:** Get logs by category and date

**Operation ID:** `getLogsByCategoryAndDate`

Returns logs from a specific category and date with optional filtering.

**Categories:** api, instance, sdk_output, general
**Date Format:** YYYY-MM-DD (e.g., 2024-01-01)

**Query Parameters:**
- `level`: Filter by log level (case-insensitive, e.g., INFO, ERROR, WARNING)
- `from`: Filter logs from timestamp (ISO 8601 format, e.g., 2024-01-01T00:00:00.000Z)
- `to`: Filter logs to timestamp (ISO 8601 format, e.g., 2024-01-01T23:59:59.999Z)
- `tail`: Get only the last N lines from the log file (integer)

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| category | path | Yes | Log category |

| date | path | Yes | Date in YYYY-MM-DD format |

| level | query | No | Filter by log level (case-insensitive) |

| from | query | No | Filter logs from timestamp (ISO 8601) |

| to | query | No | Filter logs to timestamp (ISO 8601) |

| tail | query | No | Get only the last N lines from the log file |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Logs from the specified category and date |
| 400 | Bad request (invalid category, date format, or parameters) |
| 404 | Log file not found for the specified date |
| 500 | Server error |


### `OPTIONS` /v1/core/log/{category}/{date}

**Summary:** CORS preflight for get logs by category and date

**Operation ID:** `getLogsByCategoryAndDateOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |



## Models

### `GET` /v1/core/model/list

**Summary:** List uploaded model files

**Operation ID:** `listModels`

Returns a list of model files that have been uploaded to the server.
- If `directory` parameter is not specified: Recursively searches and lists all model files
  in the entire models directory tree (including all subdirectories).
- If `directory` parameter is specified: Lists only files in that specific directory
  (non-recursive, does not search subdirectories).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Subdirectory path to list files from (e.g., "projects/myproject/models", "detection/yolov8"). If specified, only lists files in that directory (non-recursive). If not specified, recursively searches all subdirectories.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of model files |
| 500 | Server error |


### `OPTIONS` /v1/core/model/upload

**Summary:** CORS preflight for model upload

**Operation ID:** `uploadModelOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/model/upload

**Summary:** Upload a model file

**Operation ID:** `uploadModel`

Uploads a model file (ONNX, weights, etc.) to the server.
The file will be saved in the models directory. You can specify a subdirectory
using the `directory` query parameter (e.g., `projects/myproject/models`).
If the directory doesn't exist, it will be created automatically.
If no directory is specified, files will be saved in the base models directory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Subdirectory path where the model file should be saved. Use forward slashes to separate directory levels (e.g., "projects/myproject/models", "detection/yolov8", "classification/resnet50"). The directory will be created automatically if it doesn't exist. If not specified, files will be saved in the base models directory.  |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **files**: array — Model file(s) to upload. Select multiple files to upload at once (.onnx, .rknn, .weights, .cfg, .pt, .pth, .pb, .tflite)
    - **file**: string — Single model file to upload (alternative to files array)
- **application/octet-stream**
  - type: `string` — Model file as binary data

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Model file(s) uploaded successfully |
| 400 | Invalid request |
| 409 | File already exists |
| 500 | Server error |


### `DELETE` /v1/core/model/{modelName}

**Summary:** Delete a model file

**Operation ID:** `deleteModel`

Deletes a model file from the server.
You can specify a subdirectory using the `directory` query parameter if the file is in a subdirectory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| modelName | path | Yes | Name of the model file to delete |

| directory | query | No | Subdirectory path where the model file is located (e.g., "projects/myproject/models", "detection/yolov8"). If not specified, the file is assumed to be in the base models directory.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Model file deleted successfully |
| 400 | Invalid request |
| 404 | Model file not found |
| 500 | Server error |


### `PUT` /v1/core/model/{modelName}

**Summary:** Rename a model file

**Operation ID:** `renameModel`

Renames a model file on the server.
You can specify a subdirectory using the `directory` query parameter if the file is in a subdirectory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| modelName | path | Yes | Current name of the model file to rename |

| directory | query | No | Subdirectory path where the model file is located (e.g., "projects/myproject/models", "detection/yolov8"). If not specified, the file is assumed to be in the base models directory.  |

**Request body:**

- **application/json**
  - type: `object`
    - **newName**: string — New name for the model file (must have valid model file extension)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Model file renamed successfully |
| 400 | Invalid request |
| 404 | Model file not found |
| 409 | New name already exists |
| 500 | Server error |



## Node

### `GET` /v1/core/node

**Summary:** List all pre-configured nodes

**Operation ID:** `listNodes`

Returns a list of all pre-configured nodes in the node pool.
Can be filtered by availability status and category.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| available | query | No | Filter to show only available (not in use) nodes |

| category | query | No | Filter nodes by category |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of nodes retrieved successfully |
| 500 | Internal server error |


### `OPTIONS` /v1/core/node

**Summary:** CORS preflight for nodes

**Operation ID:** `listNodesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/node

**Summary:** Create a new pre-configured node

**Operation ID:** `createNode`

Creates a new pre-configured node from a template with specified parameters

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateNodeRequest

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Node created successfully |
| 400 | Bad request (missing required fields or invalid parameters) |
| 500 | Internal server error |


### `OPTIONS` /v1/core/node/build-solution

**Summary:** CORS preflight for build solution

**Operation ID:** `buildSolutionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/node/build-solution

**Summary:** Build solution from nodes

**Operation ID:** `buildSolutionFromNodes`

Build a solution configuration from selected pre-configured nodes.
This endpoint allows creating a solution by combining multiple pre-configured nodes. The solution will be created with the specified nodes and their configurations.

**Request body:**

- **application/json**
  - type: `object`
    - **nodes**: array — Array of node IDs to include in the solution
    - **name**: string — Solution name
    - **description**: string — Solution description

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Solution built successfully |
| 400 | Invalid request |
| 500 | Server error |


### `GET` /v1/core/node/preconfigured

**Summary:** List pre-configured nodes

**Operation ID:** `getPreConfiguredNodes`

Get list of all pre-configured nodes in the node pool

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of pre-configured nodes |
| 500 | Server error |


### `POST` /v1/core/node/preconfigured

**Summary:** Create pre-configured node

**Operation ID:** `createPreConfiguredNode`

Create a new pre-configured node from a template

**Request body:**

- **application/json**
  - type: `object`
    - **templateId**: string — Template ID to use for creating the node
    - **parameters**: object — Node parameters

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Node created successfully |
| 400 | Invalid request |
| 500 | Server error |


### `GET` /v1/core/node/preconfigured/available

**Summary:** List available pre-configured nodes

**Operation ID:** `getAvailableNodes`

Get list of available (not in use) pre-configured nodes

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of available pre-configured nodes |
| 500 | Server error |


### `GET` /v1/core/node/stats

**Summary:** Get node pool statistics

**Operation ID:** `getNodeStats`

Returns statistics about the node pool including total templates, total nodes, available nodes, and nodes by category

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Statistics retrieved successfully |
| 500 | Internal server error |


### `OPTIONS` /v1/core/node/stats

**Summary:** CORS preflight for stats

**Operation ID:** `getNodeStatsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/node/template

**Summary:** List all node templates

**Operation ID:** `listTemplates`

Returns a list of all available node templates that can be used to create pre-configured nodes.

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
| 200 | List of templates retrieved successfully |
| 500 | Internal server error |


### `OPTIONS` /v1/core/node/template

**Summary:** CORS preflight for templates

**Operation ID:** `listTemplatesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/node/template/{category}

**Summary:** Get templates by category

**Operation ID:** `getTemplatesByCategory`

Get list of node templates filtered by category

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| category | path | Yes | Template category |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of templates in the category |
| 500 | Server error |


### `GET` /v1/core/node/template/{templateId}

**Summary:** Get template details

**Operation ID:** `getTemplate`

Returns detailed information about a specific node template

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| templateId | path | Yes | Unique template identifier |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Template details retrieved successfully |
| 404 | Template not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/node/template/{templateId}

**Summary:** CORS preflight for template

**Operation ID:** `getTemplateOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `DELETE` /v1/core/node/{nodeId}

**Summary:** Delete a node

**Operation ID:** `deleteNode`

Deletes a pre-configured node. Only nodes that are not currently in use can be deleted.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| nodeId | path | Yes | Unique node identifier |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Node deleted successfully |
| 404 | Node not found |
| 409 | Conflict (node is currently in use) |
| 500 | Internal server error |


### `GET` /v1/core/node/{nodeId}

**Summary:** Get node details

**Operation ID:** `getNode`

Returns detailed information about a specific pre-configured node

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| nodeId | path | Yes | Unique node identifier |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Node details retrieved successfully |
| 404 | Node not found |
| 500 | Internal server error |


### `OPTIONS` /v1/core/node/{nodeId}

**Summary:** CORS preflight for node

**Operation ID:** `getNodeOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/core/node/{nodeId}

**Summary:** Update a node

**Operation ID:** `updateNode`

Updates an existing node's parameters. Only nodes that are not currently in use can be updated.
Note: Update operation deletes the old node and creates a new one with updated parameters.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| nodeId | path | Yes | Unique node identifier |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateNodeRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Node updated successfully |
| 400 | Bad request (missing parameters field) |
| 404 | Node not found |
| 409 | Conflict (node is currently in use) |
| 500 | Internal server error |



## ONVIF

### `OPTIONS` /v1/onvif/camera/{cameraid}/credentials

**Summary:** CORS preflight for ONVIF credentials

**Operation ID:** `setONVIFCredentialsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/onvif/camera/{cameraid}/credentials

**Summary:** Set credentials for an ONVIF camera

**Operation ID:** `setONVIFCredentials`

Set authentication credentials for a specific ONVIF camera.
**Camera ID:** - Can be either the camera IP address (e.g., "192.168.1.100") or UUID - IP address format: 0-255.0-255.0-255.0-255
**Credentials:** - Username and password are required - Credentials are stored securely and used for subsequent ONVIF operations - If camera is not found, a suggestion may be provided if a similar IP is detected
**Usage:** 1. Discover cameras using POST /v1/onvif/discover 2. Get camera list using GET /v1/onvif/cameras 3. Set credentials using this endpoint 4. Get streams using GET /v1/onvif/streams/{cameraid}

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| cameraid | path | Yes | Camera identifier (IP address or UUID) |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/ONVIFCredentialsRequest
  - example: `{'username': 'admin', 'password': 'password123'}`

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Credentials set successfully |
| 400 | Bad request (missing username or password, invalid JSON) |
| 404 | Camera not found |
| 500 | Internal server error |


### `GET` /v1/onvif/cameras

**Summary:** Get all discovered ONVIF cameras

**Operation ID:** `getONVIFCameras`

Retrieve a list of all ONVIF cameras that have been discovered on the network.
**Camera Information:** - Each camera includes IP address, UUID, manufacturer, model, and serial number - Cameras are filtered based on whitelist support (if configured) - Only cameras that pass whitelist validation are returned
**Usage:** 1. First call POST /v1/onvif/discover to discover cameras 2. Then call this endpoint to retrieve the list of discovered cameras 3. Use the camera ID (IP or UUID) to get streams or set credentials

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of discovered ONVIF cameras |
| 500 | Internal server error |


### `OPTIONS` /v1/onvif/cameras

**Summary:** CORS preflight for ONVIF cameras

**Operation ID:** `getONVIFCamerasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `OPTIONS` /v1/onvif/discover

**Summary:** CORS preflight for ONVIF discovery

**Operation ID:** `discoverONVIFCamerasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/onvif/discover

**Summary:** Discover ONVIF cameras

**Operation ID:** `discoverONVIFCameras`

Discover ONVIF cameras on the network using WS-Discovery protocol.
This endpoint initiates an asynchronous discovery process that searches for ONVIF-compliant cameras on the local network.
**Timeout Parameter:** - Optional query parameter `timeout` (1-30 seconds, default: 5) - Controls how long to wait for camera responses during discovery
**Response:** - Returns 204 No Content when discovery is initiated - Use GET /v1/onvif/cameras to retrieve discovered cameras after discovery completes

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| timeout | query | No | Discovery timeout in seconds (1-30, default: 5) |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Discovery process started successfully |
| 500 | Internal server error |


### `GET` /v1/onvif/streams/{cameraid}

**Summary:** Get streams for an ONVIF camera

**Operation ID:** `getONVIFStreams`

Retrieve available video streams for a specific ONVIF camera.
**Camera ID:** - Can be either the camera IP address (e.g., "192.168.1.100") or UUID - IP address format: 0-255.0-255.0-255.0-255
**Stream Information:** - Each stream includes profile token, resolution (width/height), frame rate (fps), and RTSP URI - Streams are retrieved using ONVIF Media service - Camera credentials must be set before retrieving streams (if authentication is required)
**Usage:** 1. Discover cameras using POST /v1/onvif/discover 2. Get camera list using GET /v1/onvif/cameras 3. Set credentials using POST /v1/onvif/camera/{cameraid}/credentials (if needed) 4. Get streams using this endpoint

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| cameraid | path | Yes | Camera identifier (IP address or UUID) |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of available streams for the camera |
| 400 | Bad request (missing or invalid camera ID) |
| 404 | Camera not found |
| 500 | Internal server error |


### `OPTIONS` /v1/onvif/streams/{cameraid}

**Summary:** CORS preflight for ONVIF streams

**Operation ID:** `getONVIFStreamsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |



## Recognition

### `DELETE` /v1/recognition/face-database/connection

**Summary:** Delete face database connection configuration

**Operation ID:** `deleteFaceDatabaseConnection`

Deletes the face database connection configuration. After deletion, the system will fall back to using
the default `face_database.txt` file for storing face recognition data.

**Behavior:**
- Removes the database connection configuration from `config.json`
- Sets `enabled` to `false` in the configuration
- System immediately switches to using `face_database.txt` file
- If no database configuration exists, returns a success response indicating no configuration was found

**Note:** This operation does not delete any data from the database itself. It only removes the connection
configuration. To delete data from the database, use the face deletion endpoints.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Database connection configuration deleted successfully |
| 500 | Server error (failed to update configuration, etc.) |


### `GET` /v1/recognition/face-database/connection

**Summary:** Get face database connection configuration

**Operation ID:** `getFaceDatabaseConnection`

Retrieves the current face database connection configuration. If no database is configured or
the database connection is disabled, returns information about the default `face_database.txt` file.

**Response:**
- If database is configured: Returns the connection configuration details
- If database is not configured: Returns `enabled: false` and the path to the default file

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Configuration retrieved successfully |
| 500 | Server error |


### `OPTIONS` /v1/recognition/face-database/connection

**Summary:** CORS preflight for face database connection

**Operation ID:** `faceDatabaseConnectionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/recognition/face-database/connection

**Summary:** Configure face database connection

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
| 200 | Database connection configured successfully |
| 400 | Bad request (missing required fields, invalid database type, etc.) |
| 500 | Server error (failed to save configuration, etc.) |


### `GET` /v1/recognition/faces

**Summary:** List face subjects

**Operation ID:** `listFaceSubjects`

Retrieves a list of all saved face subjects with pagination support.
You can filter by subject name or retrieve all subjects.

This endpoint returns paginated results with face information including image_id and subject name.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| page | query | No | Page number of examples to return. Used for pagination. Default value is 0. |

| size | query | No | Number of faces per page (page size). Used for pagination. Default value is 20. |

| subject | query | No | Specifies the subject examples to return. If empty, returns examples for all subjects. |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of face subjects retrieved successfully |
| 401 | Unauthorized (missing or invalid API key) |
| 500 | Server error |


### `OPTIONS` /v1/recognition/faces

**Summary:** CORS preflight for register face subject

**Operation ID:** `registerFaceSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/recognition/faces

**Summary:** Register face subject

**Operation ID:** `registerFaceSubject`

Registers a face subject by storing the image. You can add as many images as you want to train the system.
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

| subject | query | Yes | Subject name to register (e.g., "subject1") |

| det_prob_threshold | query | No | Detection probability threshold (0.0 to 1.0) |

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
| 200 | Face subject registered successfully |
| 400 | Invalid request. Possible reasons: - Missing required query parameter: subject - Invalid image format or corrupted image data - No face detected in the uploaded image (registration will be rejected) - Face detected but confidence below threshold  |
| 401 | Unauthorized (missing or invalid API key) |
| 500 | Server error |


### `DELETE` /v1/recognition/faces/all

**Summary:** Delete all face subjects

**Operation ID:** `deleteAllFaceSubjects`

Deletes all face subjects from the database. This operation is irreversible.
All image IDs and subject mappings will be removed.

**Warning:** This endpoint will delete all registered faces. Use with caution.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | All face subjects deleted successfully |
| 500 | Server error |


### `OPTIONS` /v1/recognition/faces/all

**Summary:** CORS preflight for delete all face subjects

**Operation ID:** `deleteAllFaceSubjectsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `OPTIONS` /v1/recognition/faces/delete

**Summary:** CORS preflight for delete multiple face subjects

**Operation ID:** `deleteMultipleFaceSubjectsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/recognition/faces/delete

**Summary:** Delete multiple face subjects

**Operation ID:** `deleteMultipleFaceSubjects`

Deletes multiple face subjects by their image IDs. If some IDs do not exist,
they will be ignored. This endpoint is available since version 1.0.

**Request body:**

- **application/json**
  - type: `array` — Array of image IDs to delete
  - example: `['6b135f5b-a365-4522-b1f1-4c9ac2dd0728', '7c246g6c-b476-5633-c2g2-5d0bd3ee1839']`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Face subjects deleted successfully |
| 400 | Bad request (invalid JSON, not an array, etc.) |
| 401 | Unauthorized (missing or invalid API key) |
| 500 | Server error |


### `DELETE` /v1/recognition/faces/{image_id}

**Summary:** Delete face subject by ID or subject name

**Operation ID:** `deleteFaceSubject`

Deletes a face subject by its image ID or subject name.

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
| 200 | Face subject(s) deleted successfully |
| 400 | Bad request (missing image_id, etc.) |
| 401 | Unauthorized (missing or invalid API key) |
| 404 | Face subject not found |
| 500 | Server error |


### `OPTIONS` /v1/recognition/faces/{image_id}

**Summary:** CORS preflight for delete face subject

**Operation ID:** `deleteFaceSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `OPTIONS` /v1/recognition/recognize

**Summary:** CORS preflight for face recognition

**Operation ID:** `recognizeFacesOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/recognition/recognize

**Summary:** Recognize faces from uploaded image

**Operation ID:** `recognizeFaces`

Recognizes faces from an uploaded image. The image can be provided as base64-encoded data
in a multipart/form-data request. Returns face detection results including bounding boxes,
landmarks, recognized subjects, and execution times.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| limit | query | No | Maximum number of faces to recognize (0 = no limit). Recognizes the largest faces first. |

| prediction_count | query | No | Number of predictions to return per face |

| det_prob_threshold | query | No | Detection probability threshold (0.0 to 1.0) |

| threshold | query | No | Optional similarity threshold (0.0 to 1.0). If provided, subjects with similarity below this threshold will be filtered out. If omitted, returns top-N similarities without filtering. |

| face_plugins | query | No | Face plugins to enable (comma-separated) |

| status | query | No | Status filter |

| detect_faces | query | No | Whether to detect faces (true) or only recognize existing faces (false) |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **file**: string — Image file as base64-encoded data or binary data

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Face recognition results |
| 400 | Invalid request (missing file, invalid image format, etc.) |
| 401 | Unauthorized (missing or invalid API key) |
| 500 | Server error |


### `OPTIONS` /v1/recognition/search

**Summary:** CORS preflight for search appearance subject

**Operation ID:** `searchAppearanceSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/recognition/search

**Summary:** Search appearance subject

**Operation ID:** `searchAppearanceSubject`

Search for similar faces in the database given an input face image.
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

| threshold | query | No | Minimum similarity threshold for matches (0.0 - 1.0) |

| limit | query | No | Maximum number of results to return (0 = no limit) |

| det_prob_threshold | query | No | Face detection probability threshold |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **file**: string — Image file containing a face to search
- **application/json**
  - type: `object`
    - **file**: string — Base64 encoded image data

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Search results |
| 400 | Invalid request |
| 500 | Server error |


### `OPTIONS` /v1/recognition/subjects/{subject}

**Summary:** CORS preflight for rename subject

**Operation ID:** `renameSubjectOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/recognition/subjects/{subject}

**Summary:** Rename face subject

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
| 200 | Subject renamed successfully |
| 400 | Bad request (subject not found, missing subject, invalid JSON, etc.) |
| 500 | Server error |



## SecuRT Instance

### `GET` /v1/securt/experimental/instance/{instanceId}/surrender_detection

**Summary:** Get surrender detection configuration (Experimental)

**Operation ID:** `getSurrenderDetection`

Get surrender detection configuration for a SecuRT instance.
**Note:** This is an experimental feature and may be subject to change.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Surrender detection configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/experimental/instance/{instanceId}/surrender_detection

**Summary:** CORS preflight for surrender detection

**Operation ID:** `surrenderDetectionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/experimental/instance/{instanceId}/surrender_detection

**Summary:** Set surrender detection (Experimental)

**Operation ID:** `setSurrenderDetection`

Enable or disable surrender detection for a SecuRT instance.
**Note:** This is an experimental feature and may be subject to change.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Enable surrender detection

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Surrender detection configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance

**Summary:** CORS preflight for create instance

**Operation ID:** `createSecuRTInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance

**Summary:** Create a new SecuRT instance

**Operation ID:** `createSecuRTInstance`

Creates a new SecuRT instance with the provided configuration. The instance ID will be auto-generated if not provided.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInstanceWrite
  - example: `{'name': 'My SecuRT Instance', 'detectorMode': 'SmartDetection', 'detectionSensitivity': 'Low', 'movementSensitivity': 'Low', 'sensorModality': 'RGB', 'frameRateLimit': 0, 'metadataMode': False, 'statisticsMode': False, 'diagnosticsMode': False, 'debugMode': False}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Instance created successfully |
| 400 | Invalid request |
| 409 | Instance already exists or creation failed |
| 500 | Internal server error |


### `DELETE` /v1/securt/instance/{instanceId}

**Summary:** Delete SecuRT instance

**Operation ID:** `deleteSecuRTInstance`

Deletes an existing SecuRT instance. The instance must exist.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Instance deleted successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}

**Summary:** CORS preflight for instance operations

**Operation ID:** `securtInstanceOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PATCH` /v1/securt/instance/{instanceId}

**Summary:** Update SecuRT instance

**Operation ID:** `updateSecuRTInstance`

Updates an existing SecuRT instance with the provided configuration. Only provided fields will be updated.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInstanceWrite
  - example: `{'name': 'Updated Instance Name', 'detectorMode': 'Detection', 'detectionSensitivity': 'Medium'}`

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Instance updated successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `PUT` /v1/securt/instance/{instanceId}

**Summary:** Create SecuRT instance with specific ID

**Operation ID:** `createSecuRTInstanceWithId`

Creates a SecuRT instance with the specified instance ID. If the instance already exists, returns a conflict error.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInstanceWrite
  - example: `{'name': 'My SecuRT Instance', 'detectorMode': 'SmartDetection', 'detectionSensitivity': 'Low', 'movementSensitivity': 'Low', 'sensorModality': 'RGB', 'frameRateLimit': 0, 'metadataMode': False, 'statisticsMode': False, 'diagnosticsMode': False, 'debugMode': False}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Instance created successfully |
| 400 | Invalid request |
| 409 | Instance already exists |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/analytics_entities

**Summary:** Get analytics entities for SecuRT instance

**Operation ID:** `securtGetAnalyticsEntitiesV1`

Returns all analytics entities (areas and lines) configured for a SecuRT instance. This includes all types of areas (crossing, intrusion, loitering, crowding, occupancy, crowd estimation, dwelling, armed person, object left, object removed, fallen person) and all types of lines (crossing, counting, tailgating).

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Analytics entities retrieved successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/analytics_entities

**Summary:** CORS preflight for analytics entities

**Operation ID:** `securtGetAnalyticsEntitiesV1Options`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/securt/instance/{instanceId}/attributes_extraction

**Summary:** Get attributes extraction

**Operation ID:** `getAttributesExtraction`

Get configured attributes extraction for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Attributes extraction configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/attributes_extraction

**Summary:** CORS preflight for attributes extraction

**Operation ID:** `attributesExtractionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/attributes_extraction

**Summary:** Set attributes extraction

**Operation ID:** `setAttributesExtraction`

Configure attributes extraction for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **enabled**: boolean — Enable attributes extraction
    - **attributes**: array — Array of attributes to extract

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Attributes extraction configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `DELETE` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** Delete all exclusion areas

**Operation ID:** `deleteExclusionAreas`

Delete all exclusion areas for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 204 | All exclusion areas deleted successfully |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** Get exclusion areas

**Operation ID:** `getExclusionAreas`

Get all exclusion areas for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of exclusion areas |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** CORS preflight for exclusion areas

**Operation ID:** `exclusionAreasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/exclusion_areas

**Summary:** Add exclusion area

**Operation ID:** `addExclusionArea`

Add an exclusion area to a SecuRT instance to exclude certain regions from analytics

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **coordinates**: array — Array of coordinates defining the exclusion area polygon

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Exclusion area added successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/face_detection

**Summary:** CORS preflight for face detection

**Operation ID:** `faceDetectionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/face_detection

**Summary:** Set face detection

**Operation ID:** `setFaceDetection`

Enable or disable face detection for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Enable face detection

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Face detection configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/feature_extraction

**Summary:** Get feature extraction

**Operation ID:** `getFeatureExtraction`

Get configured feature extraction types for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Feature extraction configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/feature_extraction

**Summary:** CORS preflight for feature extraction

**Operation ID:** `featureExtractionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/feature_extraction

**Summary:** Set feature extraction

**Operation ID:** `setFeatureExtraction`

Configure feature extraction types for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **types**: array — Array of feature extraction types

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Feature extraction configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/input

**Summary:** CORS preflight for input

**Operation ID:** `setSecuRTInstanceInputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/input

**Summary:** Set input source for SecuRT instance

**Operation ID:** `setSecuRTInstanceInput`

Sets the input source configuration for a SecuRT instance. Supports multiple input types including File, RTSP, RTMP, and HLS streams.
**Input Types:**
- **File**: Video file from local path or URL
- **RTSP**: RTSP stream URL (uses `rtsp_src` node)
- **RTMP**: RTMP stream URL (uses `rtmp_src` node)
- **HLS**: HLS stream URL (.m3u8) (uses `hls_src` node)
**Additional Parameters:**
For RTSP input, you can specify: - `RTSP_TRANSPORT`: Transport protocol ("tcp" or "udp") - `GST_DECODER_NAME`: GStreamer decoder name (e.g., "avdec_h264", "decodebin") - `USE_URISOURCEBIN`: Use urisourcebin format for auto-detection ("true" or "false")
The instance must exist. If the instance is currently running, it will be automatically restarted to apply the changes.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTInputRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Input settings updated successfully |
| 400 | Invalid request (invalid input type or missing required fields) |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/lpr

**Summary:** Get license plate recognition (LPR) configuration

**Operation ID:** `getLPR`

Get LPR configuration for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | LPR configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/lpr

**Summary:** CORS preflight for LPR

**Operation ID:** `lprOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/lpr

**Summary:** Set license plate recognition (LPR)

**Operation ID:** `setLPR`

Enable or disable license plate recognition for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Enable license plate recognition

**Responses:**

| Code | Description |
|------|-------------|
| 204 | LPR configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/masking_areas

**Summary:** CORS preflight for masking areas

**Operation ID:** `maskingAreasOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/masking_areas

**Summary:** Set masking areas

**Operation ID:** `setMaskingAreas`

Configure masking areas for a SecuRT instance to hide certain regions from processing

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **areas**: array — Array of masking area polygons, each polygon is an array of coordinates

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Masking areas configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/motion_area

**Summary:** CORS preflight for motion area

**Operation ID:** `motionAreaOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/motion_area

**Summary:** Set motion area

**Operation ID:** `setMotionArea`

Configure motion detection area for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **coordinates**: array — Array of coordinates defining the motion area polygon

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Motion area configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/output

**Summary:** CORS preflight for output

**Operation ID:** `setSecuRTInstanceOutputOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/output

**Summary:** Set output destination for SecuRT instance

**Operation ID:** `setSecuRTInstanceOutput`

Configures output destination for a SecuRT instance. Supports multiple output types including MQTT, RTMP, RTSP, and HLS.
**Output Types:**
- **MQTT**: Publish analytics results to MQTT broker - Required: `broker` (MQTT broker address) - Optional: `port` (default: 1883), `topic` (default: securt/{instanceId}), `username`, `password`
- **RTMP**: Stream video to RTMP server - Required: `uri` (must start with rtmp://)
- **RTSP**: Stream video to RTSP server - Required: `uri` (must start with rtsp://)
- **HLS**: Stream video via HLS - Required: `uri` (must start with hls://, http://, or https://)
**Behavior:**
- If the instance is currently running, it will be automatically restarted to apply the changes
- Multiple outputs can be configured by calling this endpoint multiple times
- Output configuration is stored in instance AdditionalParams

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/SecuRTOutputRequest

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Output settings updated successfully |
| 400 | Invalid request (invalid output type or missing required fields) |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/performance_profile

**Summary:** Get performance profile

**Operation ID:** `getPerformanceProfile`

Get configured performance profile for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Performance profile configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/performance_profile

**Summary:** CORS preflight for performance profile

**Operation ID:** `performanceProfileOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/performance_profile

**Summary:** Set performance profile

**Operation ID:** `setPerformanceProfile`

Configure performance profile for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **profile**: string — Performance profile name

**Responses:**

| Code | Description |
|------|-------------|
| 204 | Performance profile configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/pip

**Summary:** Get picture-in-picture (PIP) configuration

**Operation ID:** `getPIP`

Get PIP configuration for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | PIP configuration |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/pip

**Summary:** CORS preflight for PIP

**Operation ID:** `pipOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/securt/instance/{instanceId}/pip

**Summary:** Set picture-in-picture (PIP)

**Operation ID:** `setPIP`

Enable or disable picture-in-picture feature for a SecuRT instance

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Request body:**

- **application/json**
  - type: `object`
    - **enable**: boolean — Enable picture-in-picture

**Responses:**

| Code | Description |
|------|-------------|
| 204 | PIP configured successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `GET` /v1/securt/instance/{instanceId}/stats

**Summary:** Get SecuRT instance statistics

**Operation ID:** `getSecuRTInstanceStats`

Returns statistics for a SecuRT instance including frame rate, latency, frames processed, track count, and running status.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| instanceId | path | Yes | Instance ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Statistics retrieved successfully |
| 400 | Invalid request |
| 404 | Instance not found |
| 500 | Internal server error |


### `OPTIONS` /v1/securt/instance/{instanceId}/stats

**Summary:** CORS preflight for stats

**Operation ID:** `getSecuRTInstanceStatsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |



## Solutions

### `GET` /v1/core/solution

**Summary:** List all solutions

**Operation ID:** `listSolutions`

Returns a list of all available solutions with summary information including solution type and pipeline node count.

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of solutions |
| 500 | Server error |


### `OPTIONS` /v1/core/solution

**Summary:** CORS preflight for solutions

**Operation ID:** `listSolutionsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/solution

**Summary:** Create a new solution

**Operation ID:** `createSolution`

Creates a new custom solution configuration. Custom solutions can be modified and deleted, unlike default solutions.

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/CreateSolutionRequest
  - example: `{'solutionId': 'custom_face_detection', 'solutionName': 'Custom Face Detection', 'solutionType': 'face_detection', 'pipeline': [{'nodeType': 'rtsp_src', 'nodeName': 'source_{instanceId}', 'parameters': {'url': 'rtsp://localhost/stream'}}, {'nodeType': 'yunet_face_detector', 'nodeName': 'detector_{instanceId}', 'parameters': {'model_path': 'models/face/yunet.onnx'}}, {'nodeType': 'file_des', 'nodeName': 'output_{instanceId}', 'parameters': {'output_path': '/tmp/output'}}], 'defaults': {'RTSP_URL': 'rtsp://localhost/stream'}}`

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Solution created successfully |
| 400 | Bad request (validation failed) |
| 409 | Solution already exists |
| 500 | Internal server error |


### `GET` /v1/core/solution/defaults

**Summary:** List default solutions

**Operation ID:** `listDefaultSolutions`

Get list of all available default solutions from examples/default_solutions/

**Responses:**

| Code | Description |
|------|-------------|
| 200 | List of default solutions |
| 500 | Server error |


### `OPTIONS` /v1/core/solution/defaults

**Summary:** CORS preflight for default solutions

**Operation ID:** `listDefaultSolutionsOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `OPTIONS` /v1/core/solution/defaults/{solutionId}

**Summary:** CORS preflight for load default solution

**Operation ID:** `loadDefaultSolutionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/solution/defaults/{solutionId}

**Summary:** Load default solution

**Operation ID:** `loadDefaultSolution`

Load a default solution from examples/default_solutions/ into the solution registry.
This endpoint allows loading a default solution template into the system for use in creating instances.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Default solution ID to load |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Default solution loaded successfully |
| 404 | Default solution not found |
| 500 | Server error |


### `DELETE` /v1/core/solution/{solutionId}

**Summary:** Delete a solution

**Operation ID:** `deleteSolution`

Deletes a custom solution. Default solutions cannot be deleted.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Solution ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Solution deleted successfully |
| 403 | Cannot delete default solution |
| 404 | Solution not found |
| 500 | Internal server error |


### `GET` /v1/core/solution/{solutionId}

**Summary:** Get solution details

**Operation ID:** `getSolution`

Returns detailed information about a specific solution including full pipeline configuration.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Solution ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Solution details |
| 404 | Solution not found |
| 500 | Server error |


### `OPTIONS` /v1/core/solution/{solutionId}

**Summary:** CORS preflight for solution

**Operation ID:** `getSolutionOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `PUT` /v1/core/solution/{solutionId}

**Summary:** Update a solution

**Operation ID:** `updateSolution`

Updates an existing custom solution configuration. Default solutions cannot be updated.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Solution ID |

**Request body:**

- **application/json**
  - `$ref`: #/components/schemas/UpdateSolutionRequest

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Solution updated successfully |
| 400 | Bad request (validation failed) |
| 403 | Cannot update default solution |
| 404 | Solution not found |
| 500 | Internal server error |


### `GET` /v1/core/solution/{solutionId}/instance-body

**Summary:** Get example instance creation body

**Operation ID:** `getSolutionInstanceBody`

Returns an example request body for creating an instance with this solution, along with detailed schema metadata for UI flexibility.
This includes:
- **Standard Fields**: Common instance creation fields with default values
- **Additional Parameters**: Solution-specific parameters with example values based on parameter names
- **Schema**: Detailed metadata for all fields including types, validation rules, UI hints, descriptions, examples, and categories

The response body can be used directly to create an instance via POST /v1/core/instance.
The schema field provides rich metadata for UI to dynamically render forms, validate inputs, and provide better user experience.

**Minimal Mode** (`?minimal=true`):
- Returns only essential fields: `name`, `solution`, `group`, `autoStart`
- Only includes required additionalParams (parameters without default values)
- Excludes optional output parameters (MQTT, RTMP, Screen if not set)
- Simplified schema without flexibleInputOutput
- Use this mode for simple, user-friendly forms with minimal fields

**Full Mode** (default):
- Returns all available fields including advanced options
- Includes all additionalParams (input and output)
- Complete schema with flexibleInputOutput options
- Use this mode for advanced configuration or when user clicks "Advanced Options"

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Solution ID |

| minimal | query | No | If true, returns only essential fields for minimal UI form. Returns only required parameters and common optional fields (name, solution, group, autoStart). Recommended for initial form rendering to avoid overwhelming users with too many fields.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Example instance creation body |
| 404 | Solution not found |
| 500 | Server error |


### `OPTIONS` /v1/core/solution/{solutionId}/instance-body

**Summary:** CORS preflight for solution instance body

**Operation ID:** `getSolutionInstanceBodyOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `GET` /v1/core/solution/{solutionId}/parameters

**Summary:** Get solution parameter schema

**Operation ID:** `getSolutionParameters`

Returns the parameter schema required to create an instance with this solution.
This includes:
- **Additional Parameters**: Solution-specific parameters extracted from pipeline nodes (e.g., RTSP_URL, MODEL_PATH, FILE_PATH)
- **Standard Fields**: Common instance creation fields (name, group, persistent, autoStart, etc.)

Parameters marked as required must be provided in the `additionalParams` object when creating an instance.
Parameters with default values are optional.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| solutionId | path | Yes | Solution ID |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Solution parameter schema |
| 404 | Solution not found |
| 500 | Server error |


### `OPTIONS` /v1/core/solution/{solutionId}/parameters

**Summary:** CORS preflight for solution parameters

**Operation ID:** `getSolutionParametersOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |



## Video

### `GET` /v1/core/video/list

**Summary:** List uploaded video files

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
| 200 | List of video files |
| 500 | Server error |


### `OPTIONS` /v1/core/video/upload

**Summary:** CORS preflight for video upload

**Operation ID:** `uploadVideoOptions`

**Responses:**

| Code | Description |
|------|-------------|
| 200 | CORS preflight response |


### `POST` /v1/core/video/upload

**Summary:** Upload a video file

**Operation ID:** `uploadVideo`

Uploads a video file (MP4, AVI, MKV, etc.) to the server.
The file will be saved in the videos directory. You can specify a subdirectory
using the `directory` query parameter (e.g., `projects/myproject/videos`).
If the directory doesn't exist, it will be created automatically.
If no directory is specified, files will be saved in the base videos directory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| directory | query | No | Subdirectory path where the video file should be saved. Use forward slashes to separate directory levels (e.g., "projects/myproject/videos", "users/user123", "category1/subcategory"). The directory will be created automatically if it doesn't exist. If not specified, files will be saved in the base videos directory.  |

**Request body:**

- **multipart/form-data**
  - type: `object`
    - **files**: array — Video file(s) to upload. Select multiple files to upload at once (.mp4, .avi, .mkv, .mov, .flv, .wmv, .webm, etc.)
    - **file**: string — Single video file to upload (alternative to files array)
- **application/octet-stream**
  - type: `string` — Video file as binary data

**Responses:**

| Code | Description |
|------|-------------|
| 201 | Video file(s) uploaded successfully |
| 400 | Invalid request |
| 409 | File already exists |
| 500 | Server error |


### `DELETE` /v1/core/video/{videoName}

**Summary:** Delete a video file

**Operation ID:** `deleteVideo`

Deletes a video file from the server.
You can specify a subdirectory using the `directory` query parameter if the file is in a subdirectory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| videoName | path | Yes | Name of the video file to delete |

| directory | query | No | Subdirectory path where the video file is located (e.g., "projects/myproject/videos", "users/user123"). If not specified, the file is assumed to be in the base videos directory.  |

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Video file deleted successfully |
| 400 | Invalid request |
| 404 | Video file not found |
| 500 | Server error |


### `PUT` /v1/core/video/{videoName}

**Summary:** Rename a video file

**Operation ID:** `renameVideo`

Renames a video file on the server.
You can specify a subdirectory using the `directory` query parameter if the file is in a subdirectory.

**Parameters:**

| Name | In | Required | Description |
|------|-----|----------|-------------|

| videoName | path | Yes | Current name of the video file to rename |

| directory | query | No | Subdirectory path where the video file is located (e.g., "projects/myproject/videos", "users/user123"). If not specified, the file is assumed to be in the base videos directory.  |

**Request body:**

- **application/json**
  - type: `object`
    - **newName**: string — New name for the video file (must have valid video file extension)

**Responses:**

| Code | Description |
|------|-------------|
| 200 | Video file renamed successfully |
| 400 | Invalid request |
| 404 | Video file not found |
| 409 | New name already exists |
| 500 | Server error |


