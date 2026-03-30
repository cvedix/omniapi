# Table of Contents

## [Core API](#core-api)
### [Health check endpoint](#health-check-endpoint)
### [Version information](#get-version-information)
### [System info](#get-system-hardware-information)
### [System status](#get-system-status)
### [Resource status](#get-resource-status)
### [Watchdog](#get-watchdog-status)
### [Endpoints statistics](#get-endpoints-statistics)
### [Metrics](#get-prometheus-format-metrics)

## [Logs API](#logs-api)
### [List all log files by category](#list-all-log-files-by-category)
### [Get logs by category](#get-logs-by-category)
### [Get logs by category and date](#get-logs-by-category-and-date)

## [Config API](#config-api)
See also **[Server settings and monitoring API](SERVER_SETTINGS_AND_MONITORING_API.md)** for a full list of config paths (threads, cores, RAM/CPU monitoring, log retention, etc.) and which APIs to use.
### [Get system configuration](#get-system-configuration)
### [Create or update configuration (merge)](#create-or-update-configuration-merge)
### [Replace entire configuration](#replace-entire-configuration)
### [Update configuration section (query parameter)](#update-configuration-section-query-parameter)
### [Delete configuration section (query parameter)](#delete-configuration-section-query-parameter)
### [Reset configuration to defaults](#reset-configuration-to-defaults)
### [Get configuration section (path parameter)](#get-configuration-section-path-parameter)
### [Update configuration section (path parameter)](#update-configuration-section-path-parameter)
### [Delete configuration section (path parameter)](#delete-configuration-section-path-parameter)

## [Instances API](#instances-api)
### [Get instance status summary](#get-instance-status-summary)
### [List all instances](#list-all-instances)
### [Delete all instance](#delete-all-instance)
### [Create a new AI instance](#create-a-new-ai-instance)
### [Create a new AI instance (Quick)](#create-a-new-ai-instance-quick)
### [Get instance details](#get-instance-details)
### [Update instance information](#update-instance-information)
### [Delete an instance](#delete-an-instance)
### [Start an instance](#start-an-instance)
### [Stop an instance](#stop-an-instance)
### [Restart an instance](#restart-an-instance)
### [Get instance output and processing results](#get-instance-output-and-processing-results)
### [Get last frame from instance](#get-last-frame-from-instance)
### [Get instance preview frame](#get-instance-preview-frame)
### [Start multiple instances concurrently](#start-multiple-instances-concurrently)
### [Stop multiple instances concurrently](#stop-multiple-instances-concurrently)
### [Restart multiple instances concurrently](#restart-multiple-instances-concurrently)
### [Set input source for an instance](#set-input-source-for-an-instance)
### [Get instance configuration](#get-instance-configuration)
### [Set config value at a specific path](#set-config-value-at-a-specific-path)
### [Get instance statistics](#get-instance-statistics)
### [Get instance classes](#get-instance-classes)
### [Get stream output configuration](#get-stream-output-configuration)
### [Configure stream/record output](#configure-streamrecord-output)

## [Lines API](#lines-api)
### [Get all crossing lines](#get-all-crossing-lines)
### [Create a new crossing line](#create-a-new-crossing-line)
### [Delete all crossing lines](#delete-all-crossing-lines)
### [Get a specific crossing line](#get-a-specific-crossing-line)
### [Update a specific crossing line](#update-a-specific-crossing-line)
### [Delete a specific crossing line](#delete-a-specific-crossing-line)
### [Batch update crossing lines](#batch-update-crossing-lines)

## [Groups API](#groups-api)
### [List all groups](#list-all-groups)
### [Create a new group](#create-a-new-group)
### [Get group details](#get-group-details)
### [Update a group](#update-a-group)
### [Delete a group](#delete-a-group)
### [Get instances in a group](#get-instances-in-a-group)

## [Models API](#models-api)
### [Upload a model file](#upload-a-model-file)
### [List uploaded model files](#list-uploaded-model-files)
### [Rename a model file](#rename-a-model-file)
### [Delete a model file](#delete-a-model-file)

## [Video API](#video-api)
### [Upload a video file](#upload-a-video-file)
### [List uploaded video files](#list-uploaded-video-files)
### [Rename a video file](#rename-a-video-file)
### [Delete a video file](#delete-a-video-file)

## [Fonts API](#fonts-api)
### [Upload a font file](#upload-a-font-file)
### [List uploaded font files](#list-uploaded-font-files)
### [Rename a font file](#rename-a-font-file)

## [Recognition API](#recognition-api)
### [Recognize faces from uploaded image](#recognize-faces-from-uploaded-image)
### [List face subjects](#list-face-subjects)
### [Register face subjects](#register-face-subjects)
### [Delete face subject by ID or subject name](#delete-face-subject-by-id-or-subject-name)
### [Delete multiple face subjects](#delete-multiple-face-subjects)
### [Delete all face subjects](#delete-all-face-subjects)
### [Search appearance subject](#search-appearance-subject)


## Core API
### Health check endpoint
Returns the health status of the API service.       \
API path: /v1/core/health                           

**No parameter**                                    

**No request body**             

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/health' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Service is healthy
```
{
  "status": "healthy",
  "timestamp": "2024-01-01T00:00:00.000Z",
  "uptime": 3600,
  "service": "edgeos-api",
  "version": "1.0.0"
}
```
* 500 - Service is unhealthy
```
{
  "error": "string",
  "code": 0,
  "message": "string"
}
```
### Get version information
Returns version information about the API service.  \
API path: /v1/core/version                          

**No parameter**                                    

**No request body**                                 

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/version' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Version information
  ```
  {
    "version": "1.0.0",
    "build_time": "Jan 01 2024 00:00:00",
    "git_commit": "abc123def456",
    "api_version": "v1",
    "service": "edgeos-api"
  }
  ```
### Get system hardware information
Returns detailed hardware information including CPU, GPU, RAM, Disk, Mainboard, OS, and Battery.  \
API path: /v1/core/system/info                                                                    

**No parameter**                                                                                  

**No request body**                                                                               

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/system/info' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - System hardware information
  ```
  {
    "battery": [],
    "cpu": {
      "cache_size": 6291456,
      "current_frequency": -1,
      "logical_cores": 8,
      "max_frequency": -1,
      "min_frequency": -1,
      "model": "Intel(R) Core(TM) i5-10210U CPU @ 1.60GHz",
      "physical_cores": 4,
      "regular_frequency": -1,
      "vendor": "GenuineIntel"
    },
    "disk": [
      {
        "free_size_bytes": 1013149704192,
        "model": "Virtual Disk",
        "serial_number": "<unknown>",
        "size_bytes": 1099511627776,
        "vendor": "Msft",
        "volumes": [
          "/"
        ]
      },
      {
        "free_size_bytes": 1013149704192,
        "model": "Virtual Disk",
        "serial_number": "<unknown>",
        "size_bytes": 195080192,
        "vendor": "Msft",
        "volumes": [
          "/"
        ]
      },
      {
        "free_size_bytes": 1013149704192,
        "model": "Virtual Disk",
        "serial_number": "<unknown>",
        "size_bytes": 2147487744,
        "vendor": "Msft",
        "volumes": [
          "/"
        ]
      },
      {
        "free_size_bytes": 1013149704192,
        "model": "Virtual Disk",
        "serial_number": "<unknown>",
        "size_bytes": 407490560,
        "vendor": "Msft",
        "volumes": [
          "/"
        ]
      }
    ],
    "gpu": [],
    "mainboard": {
      "name": "<unknown>",
      "serial_number": "<unknown>",
      "vendor": "<unknown>",
      "version": "<unknown>"
    },
    "os": {
      "architecture": "64 bit",
      "endianess": "little endian",
      "kernel": "6.6.87.2-microsoft-standard-WSL2",
      "name": "Ubuntu 24.04.3 LTS",
      "short_name": "Linux",
      "version": "24.04.3 LTS (Noble Numbat)"
    },
    "ram": {
      "available_mib": 4999,
      "free_mib": 4943,
      "model": "<unknown>",
      "name": "<unknown>",
      "serial_number": "<unknown>",
      "size_mib": 5824,
      "vendor": "<unknown>"
    }
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get system status
Returns current system status including CPU usage, RAM usage, load average, and uptime.   \
API path: /v1/core/system/status                                                          

**No parameter**                                                                          

**No request body**                                                                       

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/system/status' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - System status information
  ```
  {
    "cpu": {
      "usage_percent": 25.5,
      "current_frequency_mhz": 3792,
      "temperature_celsius": 45.5
    },
    "ram": {
      "total_mib": 65437,
      "used_mib": 11032,
      "free_mib": 54405,
      "available_mib": 54405,
      "usage_percent": 16.85,
      "cached_mib": 5000,
      "buffers_mib": 1000
    },
    "load_average": {
      "1min": 0.75,
      "5min": 0.82,
      "15min": 0.88
    },
    "uptime_seconds": 86400
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get resource status
Returns configured resource limits (max_running_instances, max_cpu_percent, max_ram_percent), current usage (instance_count, cpu_usage_percent, ram_usage_percent), and over-limit flags. Use this to monitor and tune limits via config; when limits are exceeded, creating new instances returns 503. \
API path: /v1/core/system/resource-status

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/system/resource-status' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Resource limits, current usage, and over-limit flags
  ```
  {
    "limits": {
      "max_running_instances": 0,
      "max_cpu_percent": 0,
      "max_ram_percent": 0,
      "thread_num": 0,
      "min_threads": 16,
      "max_threads": 64
    },
    "current": {
      "instance_count": 0,
      "cpu_usage_percent": 12.5,
      "ram_usage_percent": 45.2
    },
    "over_limits": {
      "at_instance_limit": false,
      "over_cpu_limit": false,
      "over_ram_limit": false
    }
  }
  ```
  * `limits.max_running_instances`: 0 = unlimited. When > 0, new instance creation returns 429 when instance_count >= limit.
  * `limits.max_cpu_percent` / `limits.max_ram_percent`: 0 = disabled. When > 0, new instance creation returns 503 when system CPU or RAM usage >= limit. Configure via `system.monitoring` (GET/POST /v1/core/config).
  * `limits.thread_num`, `limits.min_threads`, `limits.max_threads`: HTTP server thread limits. thread_num 0 = auto. Configure via `system.performance` (GET/PATCH /v1/core/config?path=system/performance); change takes effect after server restart.
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get watchdog status
Returns watchdog and health monitor statistics including instance health checks, restart counts, and monitoring information.                    \
API path: /v1/core/watchdog      

**No parameter**                

**No request body**             

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/watchdog' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Watchdog status information
  ```
  {
    "health_monitor": {
      "cpu_usage_percent": 0,
      "error_count": 0,
      "memory_usage_mb": 65,
      "request_count": 0,
      "running": true
    },
    "watchdog": {
      "is_healthy": true,
      "missed_heartbeats": 0,
      "recovery_actions": 0,
      "running": true,
      "seconds_since_last_heartbeat": 0,
      "total_heartbeats": 880
    }
  }
  ```
### Get endpoint statistics
Returns statistics for each API endpoint including request counts, response times, error rates, and other performance metrics.                            \
API path: /v1/core/endpoint         

**No parameter**                    

**No request body**                 

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/endpoints' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Endpoint statistics
  ```
  {
    "endpoints": [],
    "total_endpoints": 0
  }
  ```
### Get Prometheus format metrics
Returns system metrics in Prometheus format for monitoring and alerting. This endpoint is typically used by Prometheus scrapers.                           \
API path: /v1/core/metrics          

**No parameter**                    

**No request body**                 

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/metrics' \
  -H 'accept: text/plain'
```
**Responses schema**
* 200 - Prometheus format metrics
  ```
  # HELP http_requests_total Total number of HTTP requests
  # TYPE http_requests_total counter
  http_requests_total{method="GET",path="/v1/core/health"} 100
  ```

## Logs API
### List all log files by category
Returns a list of all log files organized by category (api, instance, sdk_output, general). Each category contains an array of log files with their date, size, and path.     \
API path: /v1/core/log                                  

**No parameter**                                        

**No request body**                                     

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/log' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of log files by category
  ```
  {
    "categories": {
      "api": [
        {
          "date": "2024-01-01",
          "size": 1024,
          "path": "/var/log/edgeos-api/api_2024-01-01.log"
        }
      ],
      "instance": [
        {
          "date": "2024-01-01",
          "size": 2048,
          "path": "/var/log/edgeos-api/instance_2024-01-01.log"
        }
      ],
      "sdk_output": [
        {
          "date": "2024-01-01",
          "size": 512,
          "path": "/var/log/edgeos-api/sdk_output_2024-01-01.log"
        }
      ],
      "general": [
        {
          "date": "2024-01-01",
          "size": 4096,
          "path": "/var/log/edgeos-api/general_2024-01-01.log"
        }
      ]
    }
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get logs by category
Returns logs from a specific category with optional filtering.    \
API path: /v1/core/log/{category}                                 

**Parameter** 
* *Categories*: {api, instance, sdk_output, general}
* *Query Parameters*: 
  * `level`: Filter by log level (case-insensitive, e.g., INFO, ERROR, WARNING) 
  * `from`: Filter logs from timestamp (ISO 8601 format, e.g., 2024-01-01T00:00:00.000Z) 
  * `to`: Filter logs to timestamp (ISO 8601 format, e.g., 2024-01-01T23:59:59.999Z) 
  * `tail`: Get only the last N lines from the latest log file (integer) 

**No request body**                                               

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/log/api?level=INFO&from=2024-01-01T00%3A00%3A00.000Z&to=2024-01-01T23%3A59%3A59.999Z&tail=100' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Logs from the specified category
  ```
  {
    "category": "api",
    "category_dir": "/var/log/edgeos-api/api",
    "files_count": 1,
    "total_lines": 1000,
    "filtered_lines": 500,
    "logs": [
      {
        "timestamp": "2024-01-01T12:00:00.000Z",
        "level": "INFO",
        "message": "API request received"
      },
      {
        "timestamp": "2024-01-01T12:00:01.000Z",
        "level": "ERROR",
        "message": "Failed to process request"
      }
    ]
  }
  ```
* 400 - Bad request (invalid category or parameters)
  ```
  {
    "error": "Bad request",
    "message": "Category parameter is required"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get logs by category and date
Returns logs from a specific category and date with optional filtering.     \
API path: /v1/core/log/{category}/{date}                                    

**Parameter** 
* *Categories*: {api, instance, sdk_output, general}
* *Date Format*: YYYY-MM-DD (e.g., 2024-01-01) 
* *Query Parameters*: 
  * `level`: Filter by log level (case-insensitive, e.g., INFO, ERROR, WARNING) 
  * `from`: Filter logs from timestamp (ISO 8601 format, e.g., 2024-01-01T00:00:00.000Z) 
  * `to`: Filter logs to timestamp (ISO 8601 format, e.g., 2024-01-01T23:59:59.999Z) 
  * `tail`: Get only the last N lines from the latest log file (integer) 

**No request body**                                                         

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/log/general/2025-12-26?level=INFO&from=2024-01-01T00%3A00%3A00.000Z&to=2026-01-01T23%3A59%3A59.999Z&tail=100' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Logs from the specified category and date
  ```
  {
    "category": "api",
    "date": "2024-01-01",
    "total_lines": 1000,
    "filtered_lines": 500,
    "logs": [
      {
        "timestamp": "2024-01-01T12:00:00.000Z",
        "level": "INFO",
        "message": "API request received"
      },
      {
        "timestamp": "2024-01-01T12:00:01.000Z",
        "level": "ERROR",
        "message": "Failed to process request"
      }
    ]
  }
  ```
* 400 - Bad request (invalid category or parameters)
  ```
  {
    "error": "Bad request",
    "message": "Category parameter is required"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## AI API
### Process single image/frame
Processes a single image/frame through the AI processing pipeline. Supports priority-based queuing and rate limiting.            \
API path: /v1/core/ai/process         

**No parameter**                      

**Request body**
```
{                                   
  "image": "/9j/4AAQSkZJRg...",     
  "config": "{\"model\": \"yolo\", \"threshold\": 0.5}", 
  "priority": "medium" 
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/ai/process' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "image": "/9j/4AAQSkZJRg...",
  "config": "{\"model\": \"yolo\", \"threshold\": 0.5}",
  "priority": "medium"
}'
```
**Responses schema**
* 200 - Processing job submitted successfully
  ```
  {
    "job_id": "job_1234567890",
    "status": "queued"
  }
  ```
* 400 - Bad request (invalid JSON or missing required fields)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 429 - Rate limit exceeded
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 501 - Not implemented
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Process batch of images/frames
Processes multiple images/frames in a batch through the AI processing pipeline. Currently returns 501 Not Implemented. \
API path: /v1/core/ai/batch 

**No parameter** 

**Request body**
```
{                           
  "images": [               
    "string"                
  ],                        
  "config": "string"        
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/ai/batch' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "images": [
    "string"
  ],
  "config": "string"
}'
```
**Responses schema**
* 200 - Batch processing job submitted successfully
  ```
  {
    "job_ids": [
      "string"
    ]
  }
  ```
* 400 - Bad request
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 501 - Not implemented
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get AI processing status
Returns the current status of the AI processing system including queue size, queue max capacity, GPU availability, and other resource information. \
API path: /v1/core/ai/status 

**No parameter** 

**No request body** 

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/ai/status' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - AI processing status
  ```
  {
    "queue_size": 5,
    "queue_max": 100,
    "gpu_total": 2,
    "gpu_available": 1
  }
  ```
### Get AI processing metrics
Returns detailed metrics about AI processing including performance statistics, cache statistics, and rate limiter information. \
API path: /v1/core/ai/metrics 

**No parameter** 

**No request body** 

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/ai/metrics' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - AI processing metrics
  ```
  {
    "cache": {
      "size": 0,
      "max_size": 0,
      "hit_rate": 0
    },
    "rate_limiter": {
      "total_keys": 0,
      "active_keys": 0
    }
  }
  ```

## Config API
### Get system configuration
Returns system configuration. If path query parameter is provided, returns the specific configuration section. Otherwise, returns the complete system configuration. \
API path: /v1/core/config 

**Parameter**
* *path* (string): Configuration path to get a specific section
  * Path Format: Use forward slashes / or dots . to navigate nested configuration keys
  * Examples: system/max_running_instances, system.max_running_instances, gstreamer/decode_pipelines/auto
  * Recommended: Use query parameter with forward slashes for better compatibility.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/config?path=system%2Fmax_running_instances' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Full system configuration
  ```
  {
    "auto_device_list": [
      "hailo.auto",
      "blaize.auto"
    ],
    "decoder_priority_list": [
      "blaize.auto",
      "software"
    ],
    "gstreamer": {
      "decode_pipelines": {
        "auto": {
          "pipeline": "decodebin ! videoconvert"
        }
      }
    },
    "system": {
      "web_server": {
        "enabled": true,
        "ip_address": "0.0.0.0",
        "port": 8080
      }
    }
  }
  ```
* 404 - Configuration section not found
  ```
  {
    "error": "Not found",
    "message": "Configuration section not found: system/max_running_instances"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Create or update configuration (merge)
Updates the system configuration by merging the provided JSON with the existing configuration. Only the provided fields will be updated; other fields remain unchanged. The configuration is saved to the config file after update. \
API path: /v1/core/config 

**No parameter**

**Request body**
```
{
  "system": {
    "web_server": {
      "port": 8080
    }
  }
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/config' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "system": {
    "web_server": {
      "port": 8080
    }
  }
}'
```
**Responses schema**
* 200 - Configuration updated successfully
  ```
  {
    "message": "Configuration updated successfully",
    "config": {
      "additionalProp1": {}
    }
  }
  ```
* 400 - Invalid request or validation failed
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

### Replace entire configuration
Replaces the entire system configuration with the provided JSON. All existing configuration will be replaced with the new values. The configuration is saved to the config file after replacement. \
API path: /v1/core/config 

**No parameter** 

**Request body**
```
{
  "auto_device_list": [
    "hailo.auto"
  ],
  "system": {
    "web_server": {
      "enabled": true,
      "port": 8080
    }
  }
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/config' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "auto_device_list": [
    "hailo.auto"
  ],
  "system": {
    "web_server": {
      "enabled": true,
      "port": 8080
    }
  }
}'
```
**Responses schema**
* 200 - Configuration replaced successfully
  ```
  {
    "message": "Configuration replaced successfully",
    "config": {
      "additionalProp1": {}
    }
  }
  ```
* 400 - Invalid request or validation failed
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Update configuration section (query parameter)
Updates a specific section of the system configuration at the given path. \
Only the provided fields in the section will be updated. The configuration is saved to the config file after update. \
API path: /v1/core/config 

**Parameter**
* *path* (string): Configuration path using / or . as separators
  * Path Format: Use forward slashes / or dots . as separators
  * Examples: system/max_running_instances, system.max_running_instances

**Request body**
```
{
  "port": 8080
}
```
**Request schema**
```
curl -X 'PATCH' \
  'http://localhost:8080/v1/core/config?path=system%2Fmax_running_instances' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "port": 8080
}'
```
**Responses schema**
* 200 - Configuration section updated successfully
  ```
  {
    "message": "Configuration section updated successfully",
    "path": "system/max_running_instances",
    "value": {
      "additionalProp1": {}
    }
  }
  ```
* 400 - Invalid request or validation failed
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete configuration section (query parameter)
Deletes a specific section of the system configuration at the given path. \
The configuration is saved to the config file after deletion. \
API path: /v1/core/config

**Parameter**
* *path* (string): Configuration path using / or . as separators
    * Path Format: Use forward slashes / or dots . as separators
    * Examples: system/max_running_instances, system.max_running_instances

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/config?path=system%2Fmax_running_instances' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Configuration section deleted successfully
  ```
  {
    "message": "Configuration section deleted successfully",
    "path": "system/max_running_instances"
  }
  ```
* 400 - Invalid request (empty path)
  ```
  {
    "error": "Invalid request",
    "message": "Path parameter is required"
  }
  ```
* 404 - Configuration section not found
  ```
  {
    "error": "Not found",
    "message": "Configuration section not found: system/max_running_instances"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Reset configuration to defaults
Resets the entire system configuration to default values. All existing configuration will be replaced with default values. The configuration is saved to the config file after reset. \
Warning: This operation will replace all configuration with default values. Consider backing up your configuration before resetting.  \
API path: /v1/core/config/reset

**No parameter**

**No request body**

**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/config/reset' \
  -H 'accept: application/json' \
  -d ''
```
**Responses schema**
* 200 - Configuration reset successfully
  ```
  {
    "message": "Configuration reset to defaults successfully",
    "config": {
      "additionalProp1": {}
    }
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get configuration section (path parameter)
Returns a specific section of the system configuration at the given path. \
API path: /v1/core/config/{path}

**Parameter**
* *path* (string): Configuration path using dots as separators. 
  * Path Format: Use dots . as separators (e.g., system.max_running_instances). Forward slashes / with URL encoding (%2F) are NOT supported due to Drogon routing limitations. Recommended: Use query parameter instead: GET /v1/core/config?path=system/max_running_instances
  * Example paths: system.max_running_instances, system.web_server, gstreamer.decode_pipelines.auto

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/config/system.max_running_instances' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Configuration section
  ```
  {
    "enabled": true,
    "ip_address": "0.0.0.0",
    "port": 8080
  }
  ```
* 400 - Invalid request (empty path)
  ```
  {
    "error": "Not found",
    "message": "Configuration section not found: system/max_running_instances"
  }
  ```
* 404 - Configuration section not found
  ```
  {
    "error": "Not found",
    "message": "Configuration section not found: system/max_running_instances"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Update configuration section (path parameter)
Updates a specific section of the system configuration at the given path. \
API path: /v1/core/config/{path}  

**Parameter**
* *path* (string): Configuration path using dots as separators. 
  * Path Format: Use dots . as separators (e.g., system.max_running_instances). Forward slashes / with URL encoding (%2F) are NOT supported due to Drogon routing limitations. Recommended: Use query parameter instead: GET /v1/core/config?path=system/max_running_instances
  * Example paths: system.max_running_instances, system.web_server, gstreamer.decode_pipelines.auto

**Request body**
```
{
  "port": 8080
}
```
**Request schema**
```
curl -X 'PATCH' \
  'http://localhost:8080/v1/core/config/system.max_running_instances' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "port": 8080
}'
```
**Responses schema**
* 200 - Configuration section updated successfully
  ```
  {
    "message": "Configuration section updated successfully",
    "path": "system/web_server",
    "value": {
      "additionalProp1": {}
    }
  }
  ```
* 400 - Invalid request or validation failed
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete configuration section (path parameter)
Delete a specific section of the system configuration at the given path. \
API path: /v1/core/config/{path} 

**Parameter**
* *path* (string): Configuration path using dots as separators. 
  * Path Format: Use dots . as separators (e.g., system.max_running_instances). Forward slashes / with URL encoding (%2F) are NOT supported due to Drogon routing limitations. Recommended: Use query parameter instead: GET /v1/core/config?path=system/max_running_instances
  * Example paths: system.max_running_instances, system.web_server, gstreamer.decode_pipelines.auto

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/config/system.max_running_instances' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Configuration section deleted successfully
  ```
  {
    "message": "Configuration section deleted successfully",
    "path": "system.max_running_instances"
  }
  ```
* 400 - Invalid request (empty path)
  ```
  {
    "error": "Invalid request",
    "message": "Path parameter is required"
  }
  ```
* 404 - Configuration section not found
  ```
  {
    "error": "Not found",
    "message": "Configuration section not found: system/max_running_instances"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Instances API
### Get instance status summary
Returns a summary of instance status including total number of configured instances, number of running instances, and number of stopped instances. \
API path: /v1/core/instance/status/summary

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/status/summary' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Instance status summary
  ```
  {
    "total": 5,
    "configured": 5,
    "running": 3,
    "stopped": 2,
    "timestamp": "2024-01-01T00:00:00.000Z"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### List all instances
Returns a list of all AI instances with summary information including running status, solution information, and basic configuration details. \
The response includes:
* Total count of instances
* Number of running instances
* Number of stopped instances
* Summary information for each instance (ID, display name, running status, solution, etc.)

This endpoint is useful for getting an overview of all instances in the system without fetching detailed information for each one. \
API path: /v1/core/instance

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of instances
  ```
  {
    "total": 3,
    "running": 1,
    "stopped": 2,
    "instances": [
      {
        "instanceId": "550e8400-e29b-41d4-a716-446655440000",
        "displayName": "Face Detection Camera 1",
        "running": true,
        "solutionId": "face_detection"
      }
    ]
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete all instances
Deletes all instances in the system. This operation permanently removes all instances and stops their pipelines if running. \
Behavior:
* All instances are deleted concurrently for optimal performance
* If instances are currently running, they will be stopped first
* All instance configurations will be removed from memory
* Persistent instance configuration files will be deleted from storage
* All resources associated with instances will be released

Response:
* Returns a summary of the deletion operation
* Includes detailed results for each instance (success/failure status)
* Provides counts of total, deleted, and failed instances

Note: This operation cannot be undone. Once deleted, instances must be recreated using the create instance endpoint. \
API path: /v1/core/instance \

**No parameter**

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/instance' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Delete all instances operation completed
  ```
  {
    "success": true,
    "message": "Delete all instances operation completed",
    "total": 3,
    "deleted": 3,
    "failed": 0,
    "results": [
      {
        "instanceId": "550e8400-e29b-41d4-a716-446655440000",
        "success": true,
        "status": "deleted"
      },
      {
        "instanceId": "550e8400-e29b-41d4-a716-446655440001",
        "success": true,
        "status": "deleted"
      },
      {
        "instanceId": "550e8400-e29b-41d4-a716-446655440002",
        "success": true,
        "status": "deleted"
      }
    ]
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 503 - Service unavailable (registry busy)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Create a new AI instance
Creates and registers a new AI instance with the specified configuration. Returns the instance ID (UUID) that can be used to control the instance. \
Configuration Options:
* Basic Settings: name, group, solution, persistent mode
* Performance: frame rate limits, input pixel limits, input orientation
* Detection: detector mode, sensitivity levels, sensor modality
* Processing Modes: metadata mode, statistics mode, diagnostics mode, debug mode
* Auto Management: auto-start, auto-restart, blocking readahead queue
* Solution Parameters: RTSP_URL, MODEL_PATH, FILE_PATH, RTMP_URL, etc. (via additionalParams)

Solution Types:
* face_detection: Face detection with RTSP input
* face_detection_file: Face detection with file input
* object_detection: Object detection (YOLO)
* face_detection_rtmp: Face detection with RTMP streaming
* Custom solutions registered via solution management endpoints

Persistence:
* If persistent: true, the instance will be saved to a JSON file in the instances directory
* Persistent instances are automatically loaded when the server restarts
* Instance configuration files are stored in /var/lib/edgeos-api/instances (configurable via INSTANCES_DIR environment variable)

Auto Start:
* If autoStart: true, the instance will automatically start after creation
* The pipeline will be built and started immediately
Returns: The created instance information including the generated UUID that can be used for subsequent operations. \
API path: /v1/core/instance

**No parameter**

**Request body**
```
{
  "name": "Face Detection Camera 1",
  "group": "cameras",
  "solution": "face_detection",
  "persistent": true,
  "frameRateLimit": 30,
  "metadataMode": true,
  "statisticsMode": true,
  "diagnosticsMode": false,
  "debugMode": false,
  "detectorMode": "SmartDetection",
  "detectionSensitivity": "Medium",
  "movementSensitivity": "Low",
  "sensorModality": "RGB",
  "autoStart": true,
  "autoRestart": false,
  "blockingReadaheadQueue": false,
  "inputOrientation": 0,
  "inputPixelLimit": 1920,
  "RTSP_URL": "rtsp://localhost:8554/stream",
  "MODEL_NAME": "yunet_2023mar"
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "name": "Face Detection Camera 1",
  "group": "cameras",
  "solution": "face_detection",
  "persistent": true,
  "frameRateLimit": 30,
  "metadataMode": true,
  "statisticsMode": true,
  "diagnosticsMode": false,
  "debugMode": false,
  "detectorMode": "SmartDetection",
  "detectionSensitivity": "Medium",
  "movementSensitivity": "Low",
  "sensorModality": "RGB",
  "autoStart": true,
  "autoRestart": false,
  "blockingReadaheadQueue": false,
  "inputOrientation": 0,
  "inputPixelLimit": 1920,
  "RTSP_URL": "rtsp://localhost:8554/stream",
  "MODEL_NAME": "yunet_2023mar"
}'
```
**Responses schema**
* 200 - Instance created successfully
  ```
  {
    "instanceId": "550e8400-e29b-41d4-a716-446655440000",
    "displayName": "Face Detection Camera 1",
    "group": "cameras",
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "persistent": true,
    "loaded": true,
    "running": true,
    "fps": 0,
    "version": "2025.0.1.2",
    "frameRateLimit": 30,
    "metadataMode": true,
    "statisticsMode": true,
    "diagnosticsMode": false,
    "debugMode": false,
    "readOnly": false,
    "autoStart": true,
    "autoRestart": false,
    "systemInstance": false,
    "inputPixelLimit": 1920,
    "inputOrientation": 0,
    "detectorMode": "SmartDetection",
    "detectionSensitivity": "Medium",
    "movementSensitivity": "Low",
    "sensorModality": "RGB",
    "originator": {
      "address": ""
    }
  }
  ```
* 400 - Bad request (validation failed)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get instance details
Returns detailed information about a specific instance including full configuration, status, and runtime information. \
The response includes:
* Instance configuration (display name, group, solution, etc.)
* Runtime status (running, loaded, FPS, etc.)
* Configuration settings (detector mode, sensitivity, frame rate limits, etc.)
* Input/Output configuration
* Originator information

The instance must exist. If the instance is not found, a 404 error will be returned. \
API path: /v1/core/instance/{instanceId}

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/550e8400-e29b-41d4-a716-446655440000' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Instance details
  ```
  {
    "instanceId": "550e8400-e29b-41d4-a716-446655440000",
    "displayName": "Face Detection Camera 1",
    "group": "cameras",
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "persistent": true,
    "loaded": true,
    "running": true,
    "fps": 0,
    "version": "string",
    "frameRateLimit": 0,
    "metadataMode": true,
    "statisticsMode": true,
    "diagnosticsMode": true,
    "debugMode": true,
    "readOnly": true,
    "autoStart": true,
    "autoRestart": true,
    "systemInstance": true,
    "inputPixelLimit": 0,
    "inputOrientation": 0,
    "detectorMode": "string",
    "detectionSensitivity": "string",
    "movementSensitivity": "string",
    "sensorModality": "string",
    "originator": {
      "address": "string"
    }
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: 550e8400-e29b-41d4-a716-446655440000"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Update instance information
Updates configuration of an existing instance. Only provided fields will be updated. Supports two update formats: \
Update Format 1: Field-by-field (camelCase)
* Update individual fields like name, group, autoStart, frameRateLimit, etc.
* Use additionalParams to update solution-specific parameters (RTSP_URL, MODEL_PATH, etc.)

Update Format 2: Direct Config (PascalCase)
* Update using PascalCase format matching instance config file
* Can update Input, Output, Detector, Zone, etc. directly

Behavior:
* If the instance is currently running, it will be automatically restarted to apply the changes
* If the instance is not running, changes will take effect when the instance is started
* The instance must exist and not be read-only
* Only provided fields will be updated; other fields remain unchanged. 

API path: /v1/core/instance/{instanceId}

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**Request body**
```
{
  "name": "Face Detection Camera 1",
  "group": "cameras",
  "persistent": true,
  "frameRateLimit": 0,
  "metadataMode": true,
  "statisticsMode": true,
  "diagnosticsMode": true,
  "debugMode": true,
  "detectorMode": "SmartDetection",
  "detectionSensitivity": "Low",
  "movementSensitivity": "Low",
  "sensorModality": "RGB",
  "autoStart": true,
  "autoRestart": true,
  "inputOrientation": 3,
  "inputPixelLimit": 0,
  "RTSP_URL": "rtsp://localhost:8554/stream",
  "MODEL_NAME": "yunet_2023mar",
  "MODEL_PATH": "./models/yunet.onnx",
  "FILE_PATH": "/path/to/video.mp4",
  "RTMP_URL": "rtmp://localhost:1935/live/stream",
  "additionalParams": {
    "additionalProp1": "string",
    "additionalProp2": "string",
    "additionalProp3": "string"
  }
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/instance/550e8400-e29b-41d4-a716-446655440000' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "name": "Face Detection Camera 1",
  "group": "cameras",
  "persistent": true,
  "frameRateLimit": 0,
  "metadataMode": true,
  "statisticsMode": true,
  "diagnosticsMode": true,
  "debugMode": true,
  "detectorMode": "SmartDetection",
  "detectionSensitivity": "Low",
  "movementSensitivity": "Low",
  "sensorModality": "RGB",
  "autoStart": true,
  "autoRestart": true,
  "inputOrientation": 3,
  "inputPixelLimit": 0,
  "RTSP_URL": "rtsp://localhost:8554/stream",
  "MODEL_NAME": "yunet_2023mar",
  "MODEL_PATH": "./models/yunet.onnx",
  "FILE_PATH": "/path/to/video.mp4",
  "RTMP_URL": "rtmp://localhost:1935/live/stream",
  "additionalParams": {
    "additionalProp1": "string",
    "additionalProp2": "string",
    "additionalProp3": "string"
  }
}'
```
**Responses schema**
* 200 - Instance updated successfully
  ```
  {
    "instanceId": "550e8400-e29b-41d4-a716-446655440000",
    "displayName": "Face Detection Camera 1",
    "group": "cameras",
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "persistent": true,
    "loaded": true,
    "running": true,
    "fps": 0,
    "version": "string",
    "frameRateLimit": 0,
    "metadataMode": true,
    "statisticsMode": true,
    "diagnosticsMode": true,
    "debugMode": true,
    "readOnly": true,
    "autoStart": true,
    "autoRestart": true,
    "systemInstance": true,
    "inputPixelLimit": 0,
    "inputOrientation": 0,
    "detectorMode": "string",
    "detectionSensitivity": "string",
    "movementSensitivity": "string",
    "sensorModality": "string",
    "originator": {
      "address": "string"
    },
    "message": "Instance updated successfully"
  }
  ```
* 400 - Invalid request or validation failed
  ```
  {
    "error": "Failed to update",
    "message": "Could not update instance. Check if instance exists and is not read-only."
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: 550e8400-e29b-41d4-a716-446655440000"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

### Delete an instance
Deletes an instance and stops its pipeline if running. This operation permanently removes the instance from the system. \
Behavior:
* If the instance is currently running, it will be stopped first
* The instance configuration will be removed from memory
* If the instance is persistent, its configuration file will be deleted from storage
* All resources associated with the instance will be released

Prerequisites:
* Instance must exist
* Instance must not be read-only (system instances cannot be deleted)

Note: This operation cannot be undone. Once deleted, the instance must be recreated using the create instance endpoint. \
API path: /v1/core/instance/{instanceId}

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/instance/string' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Instance updated successfully
  ```
  {
    "success": true,
    "message": "string",
    "instanceId": "string"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Start an instance
Starts the pipeline for an instance. The instance must have a pipeline configured and a valid solution. \
Prerequisites:
* Instance must exist
* Instance must have a valid solution configuration
* Instance must not be read-only
* Input source must be properly configured (if required by solution)

Behavior:
* If the instance is already running, the request will succeed without error
* The pipeline will be built and started based on the instance's solution configuration
* Processing will begin according to the configured input source and detector settings
* Returns the updated instance information with running: true

Note: Starting an instance may take a few seconds as the pipeline is initialized. \
API path: /v1/core/instance/{instanceId}/start

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**No request body**

**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/string/start' \
  -H 'accept: application/json' \
  -d ''
```
**Responses schema**
* 200 - Instance started successfully
  ```
  {
    "instanceId": "550e8400-e29b-41d4-a716-446655440000",
    "displayName": "Face Detection Camera 1",
    "group": "cameras",
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "persistent": true,
    "loaded": true,
    "running": true,
    "fps": 0,
    "version": "string",
    "frameRateLimit": 0,
    "metadataMode": true,
    "statisticsMode": true,
    "diagnosticsMode": true,
    "debugMode": true,
    "readOnly": true,
    "autoStart": true,
    "autoRestart": true,
    "systemInstance": true,
    "inputPixelLimit": 0,
    "inputOrientation": 0,
    "detectorMode": "string",
    "detectionSensitivity": "string",
    "movementSensitivity": "string",
    "sensorModality": "string",
    "originator": {
      "address": "string"
    },
    "message": "string"
  }
  ```
* 400 - Failed to start instance
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Stop an instance
Stops the pipeline for an instance, halting all processing and releasing resources. \
Behavior:
* If the instance is already stopped, the request will succeed without error
* The pipeline will be gracefully stopped and all resources will be released
* Processing will stop immediately
* Returns the updated instance information with running: false

Note: Stopping an instance may take a few seconds as the pipeline is cleaned up. \
The instance configuration is preserved and can be started again using the start endpoint. \
API path: /v1/core/instance/{instanceId}/stop

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**No request body**

**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/string/stop' \
  -H 'accept: application/json' \
  -d ''
```
**Responses schema**
* 200 - Instance stopped successfully
  ```
  {
    "instanceId": "550e8400-e29b-41d4-a716-446655440000",
    "displayName": "Face Detection Camera 1",
    "group": "cameras",
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "persistent": true,
    "loaded": true,
    "running": true,
    "fps": 0,
    "version": "string",
    "frameRateLimit": 0,
    "metadataMode": true,
    "statisticsMode": true,
    "diagnosticsMode": true,
    "debugMode": true,
    "readOnly": true,
    "autoStart": true,
    "autoRestart": true,
    "systemInstance": true,
    "inputPixelLimit": 0,
    "inputOrientation": 0,
    "detectorMode": "string",
    "detectionSensitivity": "string",
    "movementSensitivity": "string",
    "sensorModality": "string",
    "originator": {
      "address": "string"
    },
    "message": "string"
  }
  ```
* 400 - Failed to stop instance
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Restart an instance
Restarts an instance by stopping it (if running) and then starting it again. This is useful for applying configuration changes or recovering from errors. \
Behavior:
* If the instance is running, it will be stopped first
* Then the instance will be started again
* This ensures a clean restart with the current configuration
* Returns the updated instance information after restart

Use Cases:
* Apply configuration changes that require a restart
* Recover from errors or unexpected states
* Refresh the pipeline with updated settings

Note: Restarting an instance may take several seconds as the pipeline is stopped and then restarted. \
API path: /v1/core/instance/{instanceId}/restart

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**No request body**

**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/string/restart' \
  -H 'accept: application/json' \
  -d ''
```
**Responses schema**
* 200 - Instance restarted successfully
  ```
  {
    "instanceId": "550e8400-e29b-41d4-a716-446655440000",
    "displayName": "Face Detection Camera 1",
    "group": "cameras",
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "persistent": true,
    "loaded": true,
    "running": true,
    "fps": 0,
    "version": "string",
    "frameRateLimit": 0,
    "metadataMode": true,
    "statisticsMode": true,
    "diagnosticsMode": true,
    "debugMode": true,
    "readOnly": true,
    "autoStart": true,
    "autoRestart": true,
    "systemInstance": true,
    "inputPixelLimit": 0,
    "inputOrientation": 0,
    "detectorMode": "string",
    "detectionSensitivity": "string",
    "movementSensitivity": "string",
    "sensorModality": "string",
    "originator": {
      "address": "string"
    },
    "message": "string"
  }
  ```
* 400 - Failed to restart instance
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get instance output and processing results
Returns real-time output and processing results for a specific instance at the time of the request. This endpoint provides comprehensive information about:
* Current processing metrics (FPS, frame rate limit)
* Input source (FILE or RTSP)
* Output type and details (FILE output files or RTMP stream)
* Detection settings and processing modes
* Current status and processing state

For instances with FILE output, it includes file count, total size, latest file information, and activity status. For instances with RTMP output, it includes RTMP and RTSP URLs. \
API path: /v1/core/instance/{instanceId}/output

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/abc-123-def/output' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Instance output and processing results
  * Instance with FILE output
    ```
    {
      "timestamp": "2025-01-15 14:30:25.123",
      "instanceId": "abc-123-def",
      "displayName": "face_detection_file_source",
      "solutionId": "face_detection",
      "solutionName": "Face Detection",
      "running": true,
      "loaded": true,
      "metrics": {
        "fps": 25.5,
        "frameRateLimit": 0
      },
      "input": {
        "type": "FILE",
        "path": "/path/to/video.mp4"
      },
      "output": {
        "type": "FILE",
        "files": {
          "exists": true,
          "directory": "./output/abc-123-def",
          "fileCount": 15,
          "totalSizeBytes": 15728640,
          "totalSize": "15 MB",
          "latestFile": "face_detection_20250115_143025.mp4",
          "latestFileTime": "2025-01-15 14:30:25",
          "recentFileCount": 3,
          "isActive": true
        }
      },
      "detection": {
        "sensitivity": "Low",
        "mode": "SmartDetection",
        "movementSensitivity": "Low",
        "sensorModality": "RGB"
      },
      "modes": {
        "statisticsMode": true,
        "metadataMode": false,
        "debugMode": true,
        "diagnosticsMode": false
      },
      "status": {
        "running": true,
        "processing": true,
        "message": "Instance is running and processing frames"
      }
    }
    ```
  * Instance with RTMP output
    ```
    {
      "timestamp": "2025-01-15 14:30:25.123",
      "instanceId": "xyz-789",
      "displayName": "face_detection_rtmp_stream",
      "solutionId": "face_detection_rtmp",
      "solutionName": "Face Detection RTMP",
      "running": true,
      "loaded": true,
      "metrics": {
        "fps": 30,
        "frameRateLimit": 25
      },
      "input": {
        "type": "FILE",
        "path": "/path/to/video.mp4"
      },
      "output": {
        "type": "RTMP_STREAM",
        "rtmpUrl": "rtmp://localhost:1935/live/face_stream",
        "rtspUrl": "rtsp://localhost:8554/live/face_stream_0"
      },
      "detection": {
        "sensitivity": "High",
        "mode": "SmartDetection",
        "movementSensitivity": "Low",
        "sensorModality": "RGB"
      },
      "modes": {
        "statisticsMode": true,
        "metadataMode": true,
        "debugMode": false,
        "diagnosticsMode": false
      },
      "status": {
        "running": true,
        "processing": true,
        "message": "Instance is running and processing frames"
      }
    }
    ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: abc-123-def"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get last frame from instance 
Returns the last processed frame from a running instance. The frame is encoded as JPEG and returned as a base64-encoded string.
Features:
* Frame is captured from the pipeline after processing (includes OSD/detection overlays)
* Frame is automatically cached when pipeline processes new frames
* Frame cache is automatically cleaned up when instance stops or is deleted

Important Notes:
* Frame capture only works if the pipeline has an app_des_node
* If pipeline doesn't have app_des_node, frame will be empty string
* Instance must be running to have a frame
* Frame is cached automatically each time pipeline processes a new frame. 

API path: /v1/core/instance/{instanceId}/frame

**Parameter**
* *instanceId* (string): Instance ID (UUID).

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/string/frame' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Last frame retrieved successfully
  ```
  {
    "frame": "/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAYEBQYFBAYGBQYHBwYIChAKCgkJChQODwwQFxQYGBcUFhYaHSUfGhsjHBYWICwgIyYnKSopGR8tMC0oMCUoKSj/2wBDAQcHBwoIChMKChMoGhYaKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCj/wAARCAABAAEDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAv/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/8QAFQEBAQAAAAAAAAAAAAAAAAAAAAX/xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oADAMBAAIRAxEAPwCdABmX/9k=",
    "running": true
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance ID not found: string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Start multiple instances concurrently
Starts multiple instances concurrently for faster batch operations. All operations run in parallel for optimal performance.

Behavior:
* All instances are started in parallel, not sequentially
* Each instance is started independently
* Results are returned for each instance (success or failure)
* Instances that are already running will be marked as successful

Request Format:
* Provide an array of instance IDs in the instanceIds field
* All instance IDs must exist in the system

Response:
* Returns detailed results for each instance
* Includes total count, success count, and failed count
* Each result includes the instance ID, success status, running status, and any error messages

Use Cases:
* Start multiple instances after server restart
* Batch operations for managing multiple cameras or streams
* Efficiently start a group of related instances

Note: Starting multiple instances simultaneously may consume significant system resources. Monitor system performance when starting many instances at once. \
API path: /v1/core/instance/batch/start

**No parameter**

**Request body**
```
{
  "instanceIds": [
    "instance-1",
    "instance-2",
    "instance-3"
  ]
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/batch/start' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "instanceIds": [
    "instance-1",
    "instance-2",
    "instance-3"
  ]
}'
```
**Responses schema**
* 200 - Batch start operation completed
  ```
  {
    "results": [
      {
        "instanceId": "instance-1",
        "success": true,
        "status": "started",
        "running": true
      },
      {
        "instanceId": "instance-2",
        "success": false,
        "status": "failed",
        "error": "Could not start instance. Check if instance exists and has a pipeline."
      }
    ],
    "total": 2,
    "success": 1,
    "failed": 1,
    "message": "Batch start operation completed"
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

### Stop multiple instances concurrently
Stops multiple instances concurrently for faster batch operations. All operations run in parallel for optimal performance.

Behavior:
* All instances are stopped in parallel, not sequentially
* Each instance is stopped independently
* Results are returned for each instance (success or failure)
* Instances that are already stopped will be marked as successful

Request Format:
* Provide an array of instance IDs in the instanceIds field
* All instance IDs must exist in the system

Response:
* Returns detailed results for each instance
* Includes total count, success count, and failed count
* Each result includes the instance ID, success status, running status, and any error messages

Use Cases:
* Stop multiple instances for maintenance
* Batch operations for managing multiple cameras or streams
* Efficiently stop a group of related instances

Note: Stopping multiple instances simultaneously will release system resources. This is useful for managing system load. \
API path: /v1/core/instance/batch/stop

**No parameter**

**Request body**
```
{
  "instanceIds": [
    "instance-1",
    "instance-2",
    "instance-3"
  ]
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/batch/stop' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "instanceIds": [
    "instance-1",
    "instance-2",
    "instance-3"
  ]
}'
```
**Responses schema**
* 200 - Batch stop operation completed
  ```
  {
    "results": [
      {
        "instanceId": "instance-1",
        "success": true,
        "status": "stopped",
        "running": false
      },
      {
        "instanceId": "instance-2",
        "success": false,
        "status": "failed",
        "error": "Could not stop instance. Check if instance exists."
      }
    ],
    "total": 2,
    "success": 1,
    "failed": 1,
    "message": "Batch stop operation completed"
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Restart multiple instances concurrently
Restarts multiple instances concurrently by stopping and then starting them. All operations run in parallel for optimal performance. \
API path: /v1/core/instance/batch/restart

**No parameter**

**Request body**
```
{
  "instanceIds": [
    "instance-1",
    "instance-2",
    "instance-3"
  ]
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/batch/restart' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "instanceIds": [
    "instance-1",
    "instance-2",
    "instance-3"
  ]
}'
```
**Responses schema**
* 200 - Batch restart operation completed
  ```
  {
    "results": [
      {
        "instanceId": "instance-1",
        "success": true,
        "status": "restarted",
        "running": true
      },
      {
        "instanceId": "instance-2",
        "success": false,
        "status": "failed",
        "error": "Could not restart instance. Check if instance exists and has a pipeline."
      }
    ],
    "total": 2,
    "success": 1,
    "failed": 1,
    "message": "Batch restart operation completed"
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Set input source for an instance
Sets the input source configuration for an instance. Replaces the Input field in the Instance File according to the request.
Input Types:
* RTSP: Uses rtsp_src node for RTSP streams
* HLS: Uses hls_src node for HLS streams
* Manual: Uses v4l2_src node for V4L2 devices

The instance must exist and not be read-only. If the instance is currently running, it will be automatically restarted to apply the changes. \
API path: /v1/core/instance/{instanceId}/input

**No parameter**

**Request body**
```
{
  "type": "Manual",
  "uri": "http://localhost"
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/string/input' \
  -H 'accept: */*' \
  -H 'Content-Type: application/json' \
  -d '{
  "type": "Manual",
  "uri": "http://localhost"
}'
```
**Responses schema**
* 204 - Input settings updated successfully
* 400 - Bad request (invalid input or missing required fields)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Instance not found",
    "message": "Instance not found: string"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get instance configuration
Returns the configuration of an instance. This endpoint provides the configuration settings of the instance, which are different from runtime state. Configuration includes settings like AutoRestart, AutoStart, Detector settings, Input configuration, etc. \
Note: This returns the configuration format, not the runtime state. For runtime state information, use the instance details endpoint. \
API path: /v1/core/instance/{instanceId}/config

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/291a85b8-d4c0-5765-dcb2-48c9b51f48d0/config' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Instance configuration
  ```
  {
    "AutoRestart": false,
    "AutoStart": false,
    "Detector": {
      "preset_values": {
        "MosaicInference": {
          "Detector/conf_threshold": 0.7
        }
      }
    },
    "DetectorRegions": {},
    "DisplayName": "Test Instance",
    "Input": {
      "match_fps": true,
      "media_format": {
        "color_format": 0,
        "default_format": true,
        "height": 0,
        "is_software": false,
        "name": "Same as Source"
      },
      "media_type": "Images",
      "output_fps": 8,
      "uri": "https://some_uri"
    },
    "InstanceId": "291a85b8-d4c0-5765-dcb2-48c9b51f48d0"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: 291a85b8-d4c0-5765-dcb2-48c9b51f48d0"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Set config value at a specific path
Sets a configuration value at the specified path. The path is a key in the configuration, and for nested structures, it will be nested keys separated by forward slashes "/". \
Note: This operation will overwrite the value at the given path.  \
Path Examples:
* Simple path: "DisplayName" - Sets top-level DisplayName
* Nested path: "Output/handlers/Mqtt" - Sets Mqtt handler in Output.handlers

jsonValue Format:
* For string values: "\"my string\"" (escaped JSON string)
* For object values: "{\"key\":\"value\"}" (escaped JSON object)

The instance must exist and not be read-only. If the instance is currently running, it will be automatically restarted to apply the changes. \
API path: /v1/core/instance/{instanceId}/config

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**Request body**
* Set string value
  ```
  {
    "path": "my/nested/path",
    "jsonValue": "\"my string\""
  }
  ```
* Set object value
  ```
  {
    "path": "Output/handlers/Mqtt",
    "jsonValue": "{\"uri\":\"mqtt://localhost:1883/events\"}"
  }
  ```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/291a85b8-d4c0-5765-dcb2-48c9b51f48d0/config' \
  -H 'accept: */*' \
  -H 'Content-Type: application/json' \
  -d '{
  "path": "my/nested/path",
  "jsonValue": "\"my string\""
}'
```
**Responses schema**
* 204 - Config value set successfully
* 400 - Bad request (invalid input or missing required fields)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: 291a85b8-d4c0-5765-dcb2-48c9b51f48d0"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get instance statistics
Returns real-time statistics for a specific instance. Statistics include frames processed, framerate, latency, resolution, and queue information. \
Statistics Include:
* Frames processed count
* Source framerate (FPS from source)
* Current framerate (processing FPS)
* Average latency (milliseconds)
* Input queue size
* Dropped frames count
* Resolution and format information
* Source resolution

Important Notes:
* Statistics are calculated in real-time
* Statistics reset to 0 when instance is restarted
* Statistics are not persisted (only calculated when requested)
* Instance must be running to get statistics 
API path: /v1/core/instance/{instanceId}/statistics

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/statistics' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Statistics retrieved successfully
  ```
  {
    "frames_processed": 1250,
    "source_framerate": 30,
    "current_framerate": 25.5,
    "latency": 200,
    "start_time": 1764900520,
    "input_queue_size": 2,
    "dropped_frames_count": 5,
    "resolution": "1280x720",
    "format": "BGR",
    "source_resolution": "1920x1080"
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Instance not found or not running
  ```
  {
    "error": "Not found",
    "message": "Instance not found or not running: a5204fc9-9a59-f80f-fb9f-bf3b42214943"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get stream output configuration
Returns the current stream output configuration for an instance. This endpoint retrieves the stream output settings including whether it is enabled and the configured URI. \
Response Format:
* enabled: Boolean indicating whether stream output is enabled
* uri: The configured stream URI (empty string if disabled)

The URI can be in one of the following formats:
* RTMP: rtmp://host:port/path/stream_key
* RTSP: rtsp://host:port/path/stream_key
* HLS: hls://host:port/path/stream_key 

API path: /v1/core/instance/{instanceId}/output/stream

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/output/stream' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Stream output configuration
  * Stream output enabled
  ```
  {
    "enabled": true,
    "uri": "rtmp://localhost:1935/live/stream"
  }
  ```
  * Stream output disabled
  ```
  {
    "enabled": false,
    "uri": ""
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: 291a85b8-d4c0-5765-dcb2-48c9b51f48d0"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Configure stream/record output
Configures stream or record output for an instance. This endpoint allows you to enable or disable output and set either a stream URI or a local path for recording. \
Record Output Mode (using path parameter):
* When enabled is true and path is provided, video will be saved as MP4 files to the specified local directory
* The path must exist and have write permissions (will be created if it doesn't exist)
* Uses rtmp_des_node to push data to local stream
* Files are saved in MP4 format to the specified path

Stream Output Mode (using uri parameter):
* RTMP: rtmp://host:port/path/stream_key - For RTMP streaming (e.g., to MediaMTX, YouTube Live, etc.)
* RTSP: rtsp://host:port/path/stream_key - For RTSP streaming
* HLS: hls://host:port/path/stream_key - For HLS streaming

Behavior:
* When enabled is true, either path (for record output) or uri (for stream output) field is required
* When enabled is false, output is disabled
* If the instance is currently running, it will be automatically restarted to apply the changes
* The stream output uses the rtmp_des_node in the pipeline

MediaMTX Setup:
* For local streaming, ensure MediaMTX is installed and running: https://github.com/bluenviron/mediamtx
* Default MediaMTX RTMP endpoint: rtmp://localhost:1935/live/stream
* Default MediaMTX RTSP endpoint: `rtsp://localhost:8554/live/stream" 

API path: /v1/core/instance/{instanceId}/output/stream

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**Request body**
```
{
  "enabled": true,
  "path": "/mnt/sb1/data"
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/output/stream' \
  -H 'accept: */*' \
  -H 'Content-Type: application/json' \
  -d '{
  "enabled": true,
  "path": "/mnt/sb1/data"
}'
```
**Responses schema**
* 204 - Successfully configured stream output
* 400 - Bad request (invalid input or missing required fields)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: 291a85b8-d4c0-5765-dcb2-48c9b51f48d0"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Lines API
### Get all crossing lines
Returns all crossing lines configured for a ba_crossline instance. Lines are used for counting objects crossing a defined line in the video stream. \
API path: /v1/core/instance/{instanceId}/lines

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/lines' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of crossing lines
  ```
  {
    "crossingLines": [
      {
        "id": "b1234567-89ab-cdef-0123-456789abcdef",
        "name": "Counting Line 1",
        "coordinates": [
          {
            "x": 100,
            "y": 300
          },
          {
            "x": 600,
            "y": 300
          }
        ],
        "direction": "Both",
        "classes": [
          "Person",
          "Vehicle"
        ],
        "color": [
          255,
          0,
          0,
          255
        ]
      }
    ]
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: b1234567-89ab-cdef-0123-456789abcdef"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

### Create a new crossing line
Creates a new crossing line for a ba_crossline instance. The line will be drawn on the video stream and used for counting objects. \
Real-time Update: The line is immediately applied to the running video stream without requiring instance restart. \
API path: /v1/core/instance/{instanceId}/lines

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**Request body**
```
{
  "name": "Counting Line 1",
  "coordinates": [
    {
      "x": 100,
      "y": 300
    },
    {
      "x": 600,
      "y": 300
    }
  ],
  "direction": "Both",
  "classes": [
    "Person",
    "Vehicle"
  ],
  "color": [
    255,
    0,
    0,
    255
  ]
}
```
**Request schema**
  ```
  curl -X 'POST' \
    'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/lines' \
    -H 'accept: application/json' \
    -H 'Content-Type: application/json' \
    -d '{
    "name": "Counting Line 1",
    "coordinates": [
      {
        "x": 100,
        "y": 300
      },
      {
        "x": 600,
        "y": 300
      }
    ],
    "direction": "Both",
    "classes": [
      "Person",
      "Vehicle"
    ],
    "color": [
      255,
      0,
      0,
      255
    ]
  }'
  ```
**Responses schema**
* 201 - Line created successfully
  ```
  {
    "id": "b1234567-89ab-cdef-0123-456789abcdef",
    "name": "Counting Line 1",
    "coordinates": [
      {
        "x": 100,
        "y": 300
      },
      {
        "x": 600,
        "y": 300
      }
    ],
    "direction": "Both",
    "classes": [
      "Person",
      "Vehicle"
    ],
    "color": [
      255,
      0,
      0,
      255
    ]
  }
  ```
* 400 - Bad request (invalid coordinates or parameters)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: b1234567-89ab-cdef-0123-456789abcdef"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete all crossing lines
Deletes all crossing lines for a ba_crossline instance. \
Real-time Update: Lines are immediately removed from the running video stream without requiring instance restart. \
API path: /v1/core/instance/{instanceId}/lines

**Parameter**
* *instanceId* (string): Unique identifier of the instance (UUID).

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/lines' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - All lines deleted successfully
  ```
  {
    "message": "All lines deleted successfully"
  }
  ```
* 404 - Instance not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: a5204fc9-9a59-f80f-fb9f-bf3b42214943"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get a specific crossing line
Returns a specific crossing line by ID for a ba_crossline instance. \
API path: /v1/core/instance/{instanceId}/lines/{lineId}

**Parameter**
* *instanceId* (string): The unique identifier of the instance.
* *lineId* (string): The unique identifier of the line to retrieve.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/lines/b1234567-89ab-cdef-0123-456789abcdef' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Line retrieved successfully
  ```
  {
    "id": "b1234567-89ab-cdef-0123-456789abcdef",
    "name": "Counting Line 1",
    "coordinates": [
      {
        "x": 100,
        "y": 300
      },
      {
        "x": 600,
        "y": 300
      }
    ],
    "direction": "Both",
    "classes": [
      "Person",
      "Vehicle"
    ],
    "color": [
      255,
      0,
      0,
      255
    ]
  }
  ```
* 404 - Instance or line not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: a5204fc9-9a59-f80f-fb9f-bf3b42214943"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Update a specific crossing line
Updates a specific crossing line by ID for a ba_crossline instance. The line will be updated on the video stream and used for counting objects. \
Real-time Update: The line is immediately applied to the running video stream without requiring instance restart. \
API path: /v1/core/instance/{instanceId}/lines/{lineId}

**Parameter**
* *instanceId* (string): The unique identifier of the instance.
* *lineId* (string): The unique identifier of the line to retrieve.

**Request body**
```
{
  "name": "Updated Counting Line",
  "coordinates": [
    {
      "x": 200,
      "y": 400
    },
    {
      "x": 700,
      "y": 400
    }
  ],
  "direction": "Up",
  "classes": [
    "Person",
    "Vehicle"
  ],
  "color": [
    0,
    255,
    0,
    255
  ]
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/lines/b1234567-89ab-cdef-0123-456789abcdef' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "name": "Updated Counting Line",
  "coordinates": [
    {
      "x": 200,
      "y": 400
    },
    {
      "x": 700,
      "y": 400
    }
  ],
  "direction": "Up",
  "classes": [
    "Person",
    "Vehicle"
  ],
  "color": [
    0,
    255,
    0,
    255
  ]
}'
```
**Responses schema**
* 200 - Line updated successfully
  ```
  {
    "id": "b1234567-89ab-cdef-0123-456789abcdef",
    "name": "Counting Line 1",
    "coordinates": [
      {
        "x": 100,
        "y": 300
      },
      {
        "x": 600,
        "y": 300
      }
    ],
    "direction": "Both",
    "classes": [
      "Person",
      "Vehicle"
    ],
    "color": [
      255,
      0,
      0,
      255
    ]
  }
  ```
* 400 - Bad request (invalid coordinates or parameters)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 404 - Instance or line not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: a5204fc9-9a59-f80f-fb9f-bf3b42214943"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete a specific crossing line
Deletes a specific crossing line by ID. \
Real-time Update: The line is immediately removed from the running video stream without requiring instance restart. \
API path: /v1/core/instance/{instanceId}/lines/{lineId}

**Parameter**
* *instanceId* (string): The unique identifier of the instance.
* *lineId* (string): The unique identifier of the line to retrieve.

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/instance/a5204fc9-9a59-f80f-fb9f-bf3b42214943/lines/b1234567-89ab-cdef-0123-456789abcdef' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Line deleted successfully
  ```
  {
    "message": "Line deleted successfully"
  }
  ```
* 404 - Instance or line not found
  ```
  {
    "error": "Not found",
    "message": "Instance not found: a5204fc9-9a59-f80f-fb9f-bf3b42214943"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Solution API
### List all solutions
Returns a list of all available solutions with summary information including solution type and pipeline node count. \
API path: /v1/core/solution

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/solution' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of solutions
  ```
  {
    "total": 2,
    "default": 1,
    "custom": 1,
    "solutions": [
      {
        "solutionId": "face_detection",
        "solutionName": "Face Detection",
        "solutionType": "face_detection",
        "isDefault": true,
        "pipelineNodeCount": 3
      }
    ]
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Create a new solution
Creates a new custom solution configuration. Custom solutions can be modified and deleted, unlike default solutions. \
API path: /v1/core/solution

**No parameter**

**Request body**
```
{
  "solutionId": "custom_face_detection",
  "solutionName": "Custom Face Detection",
  "solutionType": "face_detection",
  "pipeline": [
    {
      "nodeType": "rtsp_src",
      "nodeName": "source_{instanceId}",
      "parameters": {
        "url": "rtsp://localhost/stream"
      }
    },
    {
      "nodeType": "yunet_face_detector",
      "nodeName": "detector_{instanceId}",
      "parameters": {
        "model_path": "models/face/yunet.onnx"
      }
    },
    {
      "nodeType": "file_des",
      "nodeName": "output_{instanceId}",
      "parameters": {
        "output_path": "/tmp/output"
      }
    }
  ],
  "defaults": {
    "RTSP_URL": "rtsp://localhost/stream"
  }
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/solution' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "solutionId": "custom_face_detection",
  "solutionName": "Custom Face Detection",
  "solutionType": "face_detection",
  "pipeline": [
    {
      "nodeType": "rtsp_src",
      "nodeName": "source_{instanceId}",
      "parameters": {
        "url": "rtsp://localhost/stream"
      }
    },
    {
      "nodeType": "yunet_face_detector",
      "nodeName": "detector_{instanceId}",
      "parameters": {
        "model_path": "models/face/yunet.onnx"
      }
    },
    {
      "nodeType": "file_des",
      "nodeName": "output_{instanceId}",
      "parameters": {
        "output_path": "/tmp/output"
      }
    }
  ],
  "defaults": {
    "RTSP_URL": "rtsp://localhost/stream"
  }
}'
```
**Responses schema**
* 201 - Solution created successfully
  ```
  {
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "solutionType": "face_detection",
    "isDefault": false,
    "pipeline": [
      {
        "nodeType": "rtsp_src",
        "nodeName": "source_{instanceId}",
        "parameters": {
          "url": "rtsp://localhost/stream"
        }
      }
    ],
    "defaults": {
      "RTSP_URL": "rtsp://localhost/stream"
    }
  }
  ```
* 400 - Bad request (validation failed)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 409 - Solution already exists
  ```
  {
    "error": "Conflict",
    "message": "Solution with ID 'custom_face_detection' already exists. Use PUT /v1/core/solution/custom_face_detection to update it."
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get solution details
Returns detailed information about a specific solution including full pipeline configuration. \
API path: /v1/core/solution/{solutionId}

**Parameter**
* *solutionId* (string): Solution ID.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/solution/face_detection' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Solution details
  ```
  {
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "solutionType": "face_detection",
    "isDefault": false,
    "pipeline": [
      {
        "nodeType": "rtsp_src",
        "nodeName": "source_{instanceId}",
        "parameters": {
          "url": "rtsp://localhost/stream"
        }
      }
    ],
    "defaults": {
      "RTSP_URL": "rtsp://localhost/stream"
    }
  }
  ```
* 404 - Solution not found
  ```
  {
    "error": "Not found",
    "message": "Solution not found: face_detection"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Update a solution
Updates an existing custom solution configuration. Default solutions cannot be updated. \
API path: /v1/core/solution/{solutionId}

**Parameter**
* *solutionId* (string): Solution ID.

**Request body**
```
{
  "solutionName": "Updated Solution Name",
  "solutionType": "face_detection",
  "pipeline": [
    {
      "nodeType": "string",
      "nodeName": "string",
      "parameters": {
        "additionalProp1": "string",
        "additionalProp2": "string",
        "additionalProp3": "string"
      }
    }
  ],
  "defaults": {
    "additionalProp1": "string",
    "additionalProp2": "string",
    "additionalProp3": "string"
  }
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/solution/face_detection' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "solutionName": "Updated Solution Name",
  "solutionType": "face_detection",
  "pipeline": [
    {
      "nodeType": "string",
      "nodeName": "string",
      "parameters": {
        "additionalProp1": "string",
        "additionalProp2": "string",
        "additionalProp3": "string"
      }
    }
  ],
  "defaults": {
    "additionalProp1": "string",
    "additionalProp2": "string",
    "additionalProp3": "string"
  }
}'
```
**Responses schema**
* 200 - Solution updated successfully
  ```
  {
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "solutionType": "face_detection",
    "isDefault": false,
    "pipeline": [
      {
        "nodeType": "rtsp_src",
        "nodeName": "source_{instanceId}",
        "parameters": {
          "url": "rtsp://localhost/stream"
        }
      }
    ],
    "defaults": {
      "RTSP_URL": "rtsp://localhost/stream"
    }
  }
  ```
* 400 - Bad request (validation failed)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 403 - Cannot update default solution
  ```
  {
    "error": "Forbidden",
    "message": "Cannot update default solution: face_detection"
  }
  ```
* 404 - Solution not found
  ```
  {
    "error": "Not found",
    "message": "Solution not found: face_detection"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete a solution 
Deletes a custom solution. Default solutions cannot be deleted. \
API path: /v1/core/solution/{solutionId}

**Parameter**
* *solutionId* (string): Solution ID.

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/solution/string' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Solution deleted successfully
  ```
  {
  "message": "Solution deleted successfully",
  "solutionId": "string"
  }
  ```
* 403 - Cannot delete default solution
  ```
  {
    "error": "Forbidden",
    "message": "Cannot delete default solution: string"
  }
  ```
* 404 - Solution not found
  ```
  {
    "error": "Not found",
    "message": "Solution not found: string"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get solution parameter schema
Returns the parameter schema required to create an instance with this solution. This includes:
* Additional Parameters: Solution-specific parameters extracted from pipeline nodes (e.g., RTSP_URL, MODEL_PATH, FILE_PATH)
* Standard Fields: Common instance creation fields (name, group, persistent, autoStart, etc.)
* Parameters marked as required must be provided in the additionalParams object when creating an instance. Parameters with default values are optional. 

API path: /v1/core/solution/{solutionId}/parameters

**Parameter**
* *solutionId* (string): Solution ID.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/solution/face_detection/parameters' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Solution parameter schema
  ```
  {
    "solutionId": "face_detection",
    "solutionName": "Face Detection",
    "solutionType": "face_detection",
    "pipeline": [
      {
        "nodeType": "rtsp_src",
        "nodeName": "source_{instanceId}",
        "displayName": "RTSP Source",
        "description": "Receive video stream from RTSP URL",
        "category": "source",
        "requiredParameters": [
          "rtsp_url"
        ],
        "optionalParameters": [
          "channel",
          "resize_ratio"
        ],
        "defaultParameters": {
          "channel": "0",
          "resize_ratio": "1.0"
        },
        "parameters": {
          "rtsp_url": "${RTSP_URL}"
        }
      },
      {
        "nodeType": "yunet_face_detector",
        "nodeName": "detector_{instanceId}",
        "displayName": "YuNet Face Detector",
        "description": "Detect faces using YuNet model",
        "category": "detector",
        "requiredParameters": [
          "model_path"
        ],
        "optionalParameters": [
          "score_threshold",
          "nms_threshold",
          "top_k"
        ],
        "defaultParameters": {
          "score_threshold": "0.7",
          "nms_threshold": "0.5",
          "top_k": "50"
        },
        "parameters": {
          "model_path": "${MODEL_PATH}"
        }
      }
    ],
    "additionalParams": {
      "RTSP_URL": {
        "name": "RTSP_URL",
        "type": "string",
        "required": true,
        "description": "Required parameter for rtsp_src (rtsp_url)",
        "usedInNodes": [
          "rtsp_src"
        ]
      },
      "MODEL_PATH": {
        "name": "MODEL_PATH",
        "type": "string",
        "required": true,
        "description": "Required parameter for yunet_face_detector (model_path)",
        "usedInNodes": [
          "yunet_face_detector"
        ]
      }
    },
    "requiredAdditionalParams": [
      "RTSP_URL"
    ],
    "standardFields": {
      "name": {
        "type": "string",
        "required": true,
        "description": "Instance name (pattern: ^[A-Za-z0-9 -_]+$)",
        "pattern": "^[A-Za-z0-9 -_]+$"
      },
      "group": {
        "type": "string",
        "required": false,
        "description": "Group name (pattern: ^[A-Za-z0-9 -_]+$)",
        "pattern": "^[A-Za-z0-9 -_]+$"
      },
      "persistent": {
        "type": "boolean",
        "required": false,
        "default": false,
        "description": "Save instance to JSON file"
      },
      "autoStart": {
        "type": "boolean",
        "required": false,
        "default": false,
        "description": "Automatically start instance when created"
      }
    },
    "requiredStandardFields": [
      "name"
    ]
  }
  ```
* 404 - Solution not found
  ```
  {
    "error": "Not found",
    "message": "Solution not found: face_detection"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get example instance creation body
Returns an example request body for creating an instance with this solution, along with detailed schema metadata for UI flexibility. This includes:
* Standard Fields: Common instance creation fields with default values
* Additional Parameters: Solution-specific parameters with example values based on parameter names
* Schema: Detailed metadata for all fields including types, validation rules, UI hints, descriptions, examples, and categories

The response body can be used directly to create an instance via POST /v1/core/instance. The schema field provides rich metadata for UI to dynamically render forms, validate inputs, and provide better user experience. \
API path: /v1/core/solution/{solutionId}/instance-body

**Parameter**
* *solutionId* (string): Solution ID.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/solution/face_detection/instance-body' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Example instance creation body
  ```
  {
    "name": "example_instance",
    "group": "default",
    "solution": "face_detection",
    "persistent": false,
    "autoStart": false,
    "frameRateLimit": 0,
    "detectionSensitivity": "Low",
    "detectorMode": "SmartDetection",
    "metadataMode": false,
    "statisticsMode": false,
    "diagnosticsMode": false,
    "debugMode": false,
    "additionalParams": {
      "input": {
        "RTSP_URL": "rtsp://example.com/stream",
        "MODEL_PATH": "./models/face/yunet.onnx"
      },
      "output": {
        "MQTT_BROKER_URL": "localhost",
        "MQTT_PORT": "1883",
        "MQTT_TOPIC": "events",
        "ENABLE_SCREEN_DES": "false"
      }
    },
    "schema": {
      "standardFields": {
        "name": {
          "name": "name",
          "type": "string",
          "required": true,
          "description": "Instance name (pattern: ^[A-Za-z0-9 -_]+$)",
          "pattern": "^[A-Za-z0-9 -_]+$",
          "uiHints": {
            "inputType": "text",
            "widget": "input"
          },
          "examples": [
            "my_instance",
            "camera_01"
          ]
        },
        "frameRateLimit": {
          "name": "frameRateLimit",
          "type": "integer",
          "required": false,
          "description": "Frame rate limit (FPS, 0 = unlimited)",
          "default": 0,
          "minimum": 0,
          "uiHints": {
            "inputType": "number",
            "widget": "input"
          },
          "examples": [
            "0",
            "10",
            "30"
          ]
        }
      },
      "additionalParams": {
        "input": {
          "RTSP_URL": {
            "name": "RTSP_URL",
            "type": "string",
            "required": true,
            "description": "RTSP stream URL (e.g., rtsp://camera-ip:8554/stream)",
            "example": "rtsp://example.com/stream",
            "uiHints": {
              "inputType": "url",
              "widget": "url-input",
              "placeholder": "rtsp://camera-ip:8554/stream"
            },
            "validation": {
              "pattern": "^(rtsp|rtmp|http|https|file|udp)://.+",
              "patternDescription": "Must be a valid URL (rtsp://, rtmp://, http://, https://, file://, or udp://)"
            },
            "examples": [
              "rtsp://192.168.1.100:8554/stream1",
              "rtsp://admin:password@camera-ip:554/stream"
            ],
            "category": "connection",
            "usedInNodes": [
              "rtsp_src"
            ]
          }
        },
        "output": {
          "MQTT_BROKER_URL": {
            "name": "MQTT_BROKER_URL",
            "type": "string",
            "required": false,
            "description": "MQTT broker address (hostname or IP). Leave empty to disable MQTT output.",
            "example": "localhost",
            "uiHints": {
              "inputType": "text",
              "widget": "input",
              "placeholder": "localhost"
            },
            "examples": [
              "localhost",
              "192.168.1.100",
              "mqtt.example.com"
            ],
            "category": "output"
          }
        }
      },
      "flexibleInputOutput": {
        "description": "Flexible input/output options that can be added to any instance",
        "input": {
          "description": "Choose ONE input source. Pipeline builder auto-detects input type.",
          "mutuallyExclusive": true,
          "parameters": {
            "FILE_PATH": {
              "name": "FILE_PATH",
              "type": "string",
              "required": false,
              "description": "Local video file path or URL (supports file://, rtsp://, rtmp://, http://, https://). Pipeline builder auto-detects input type.",
              "example": "./cvedix_data/test_video/example.mp4",
              "uiHints": {
                "inputType": "file",
                "widget": "file-picker",
                "placeholder": "/path/to/video.mp4"
              },
              "category": "connection"
            }
          }
        },
        "output": {
          "description": "Add any combination of outputs. Pipeline builder auto-adds nodes.",
          "mutuallyExclusive": false,
          "parameters": {
            "MQTT_BROKER_URL": {
              "name": "MQTT_BROKER_URL",
              "type": "string",
              "required": false,
              "description": "MQTT broker address (enables MQTT output). Leave empty to disable.",
              "example": "localhost",
              "uiHints": {
                "inputType": "text",
                "widget": "input",
                "placeholder": "localhost"
              },
              "category": "output"
            }
          }
        }
      }
    }
  }
  ```
* 404 - Solution not found
  ```
  {
    "error": "Not found",
    "message": "Solution not found: string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Groups API
### List all groups
Returns a list of all groups with summary information including group name, description, instance count, and metadata.
The response includes:
* Total count of groups
* Number of default groups
* Number of custom groups
* Summary information for each group (ID, name, description, instance count, etc.)

This endpoint is useful for getting an overview of all groups in the system without fetching detailed information for each one. \
API path: /v1/core/groups

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/groups' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of groups
  ```
  {
    "total": 2,
    "default": 0,
    "custom": 2,
    "groups": [
      {
        "groupId": "cameras",
        "groupName": "Security Cameras",
        "description": "Group for security camera instances",
        "instanceCount": 3,
        "isDefault": false,
        "readOnly": false
      },
      {
        "groupId": "test",
        "groupName": "Test Group",
        "description": "Group for testing",
        "instanceCount": 1,
        "isDefault": false,
        "readOnly": false
      }
    ]
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Create a new group
Creates a new group for organizing instances. Groups help organize and manage multiple instances together.

Group Properties:
* groupId (required): Unique identifier for the group (alphanumeric, underscores, hyphens only)
* groupName (optional): Display name for the group (defaults to groupId if not provided)
* description (optional): Description of the group's purpose

Validation:
* Group ID must be unique
* Group ID must match pattern: ^[A-Za-z0-9_-]+$
* Group name must match pattern: ^[A-Za-z0-9 -_]+$

Persistence:
* Groups are automatically saved to storage
* Group files are stored in /var/lib/edgeos-api/groups (configurable via GROUPS_DIR environment variable)

Returns: The created group information including timestamps. \
API path: /v1/core/groups

**No parameter**

**Request body**
```
{
  "groupId": "cameras",
  "groupName": "Security Cameras",
  "description": "Group for security camera instances"
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/groups' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "groupId": "cameras",
  "groupName": "Security Cameras",
  "description": "Group for security camera instances"
}'
```
**Responses schema**
* 201 - Group created successfully
  ```
  {
    "groupId": "cameras",
    "groupName": "Security Cameras",
    "description": "Group for security camera instances",
    "isDefault": false,
    "readOnly": false,
    "instanceCount": 3,
    "createdAt": "2024-01-01T00:00:00.000Z",
    "updatedAt": "2024-01-01T00:00:00.000Z"
  }
  ```
* 400 - Bad request (validation failed or group already exists)
  ```
  {
    "error": "Invalid request",
    "message": "Group already exists: cameras"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get group details
Returns detailed information about a specific group including group metadata and list of instance IDs. \
The response includes:
* Group configuration (ID, name, description)
* Group metadata (isDefault, readOnly, timestamps)
* Instance count
* List of instance IDs in the group

The group must exist. If the group is not found, a 404 error will be returned. \
API path: /v1/core/groups/{groupId}

**Parameter**
* *groupId* (string): Group ID.

**No request body**
**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/groups/cameras' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Group details
  ```
  {
    "groupId": "cameras",
    "groupName": "Security Cameras",
    "description": "Group for security camera instances",
    "isDefault": false,
    "readOnly": false,
    "instanceCount": 3,
    "createdAt": "2024-01-01T00:00:00.000Z",
    "updatedAt": "2024-01-01T00:00:00.000Z",
    "instanceIds": [
      "string"
    ]
  }
  ```
* 404 - Group not found
  ```
  {
    "error": "Not found",
    "message": "Group not found: cameras"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Update a group
Updates an existing group's information. Only provided fields will be updated. \
Updateable Fields:
* groupName: Update the display name of the group
* description: Update the description of the group

Restrictions:
* Group must exist
* Group must not be read-only
* Group ID cannot be changed

Behavior:
* Only provided fields will be updated
* Other fields remain unchanged
* Updated timestamp is automatically set 

API path: /v1/core/groups/{groupId}

**Parameter**
* *groupId* (string): Group ID.

**Request body**
```
{
  "groupName": "Updated Group Name",
  "description": "Updated description"
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/groups/cameras' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "groupName": "Updated Group Name",
  "description": "Updated description"
}'
```
**Responses schema**
* 200 - Group updated successfully
  ```
  {
    "groupId": "cameras",
    "groupName": "Security Cameras",
    "description": "Group for security camera instances",
    "isDefault": false,
    "readOnly": false,
    "instanceCount": 3,
    "createdAt": "2024-01-01T00:00:00.000Z",
    "updatedAt": "2024-01-01T00:00:00.000Z"
  }
  ```
* 400 - Bad request (validation failed or group is read-only)
  ```
  {
    "error": "Invalid request",
    "message": "Request body must be valid JSON"
  }
  ```
* 404 - Group not found
  ```
  {
    "error": "Not found",
    "message": "Group not found: cameras"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete a group 
Deletes a group from the system. This operation permanently removes the group. \
Prerequisites:
* Group must exist
* Group must not be a default group
* Group must not be read-only
* Group must have no instances (all instances must be removed or moved to other groups first)

Behavior:
* Group configuration will be removed from memory
* Group file will be deleted from storage
* All resources associated with the group will be released

Note: This operation cannot be undone. Once deleted, the group must be recreated using the create group endpoint. \
API path: /v1/core/groups/{groupId}

**Parameter**
* *groupId* (string): Group ID.

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/groups/string' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Group deleted successfully
  ```
  {
    "success": true,
    "message": "string",
    "groupId": "string"
  }
  ```
* 400 - Failed to delete (group has instances, is default, or is read-only)
  ```
  {
    "error": "Failed to delete",
    "message": "Could not delete group. Check if group exists, is not default, and has no instances."
  }
  ```
* 404 - Group not found
  ```
    {
      "error": "Not found",
      "message": "Group not found: string"
    }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get instances in a group
Returns a list of all instances that belong to a specific group. \
The response includes:
* Group ID
* List of instances with summary information (ID, display name, solution, running status)
* Total count of instances in the group

Use Cases:
* View all instances in a specific group
* Monitor group status and performance
* Manage instances by group

The group must exist. If the group is not found, a 404 error will be returned. \
API path: /v1/core/groups/{groupId}/instances

**Parameter**
* *groupId* (string): Group ID.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/groups/cameras/instances' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of instances in the group
  ```
  {
    "groupId": "cameras",
    "count": 2,
    "instances": [
      {
        "instanceId": "abc-123",
        "displayName": "Camera 1",
        "solutionId": "face_detection",
        "running": true,
        "loaded": true
      },
      {
        "instanceId": "def-456",
        "displayName": "Camera 2",
        "solutionId": "face_detection",
        "running": false,
        "loaded": true
      }
    ]
  }
  ```
* 404 - Group not found
  ```
  {
    "error": "Not found",
    "message": "Group not found: cameras"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Models API
### Upload a model file
Uploads a model file (ONNX, weights, etc.) to the server. The file will be saved in the models directory. \
API path: /v1/core/model/upload

**No parameter**

**Request body**
* *file/files* (binary): Model file(s) to upload. Select multiple/single file(s) to upload at once.

**Request schema**
**Responses schema**
* 201 - Model file(s) uploaded successfully
  ```
  {
    "success": true,
    "message": "string",
    "count": 0,
    "files": [
      {
        "filename": "string",
        "originalFilename": "string",
        "path": "string",
        "size": 0,
        "url": "string"
      }
    ],
    "warnings": [
      "string"
    ]
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - File already exists
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### List uploaded model files
Returns a list of all model files that have been uploaded to the server. \
API path: /v1/core/model/list

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/model/list' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of model files
  ```
  {
    "success": true,
    "models": [
      {
        "filename": "string",
        "path": "string",
        "size": 0,
        "modified": "string"
      }
    ],
    "count": 0,
    "directory": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Rename a model file
Renames a model file on the server. \
API path: /v1/core/model/{modelName}

**Parameter**
* *modelName* (string): Current name of the model file to rename.

**Request body**
```
{
  "newName": "my_model_v2.onnx"
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/model/modelName' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "newName": "my_video_v2.mp4"
}'
```
**Responses schema**
* 200 - Model file renamed successfully
  ```
  {
    "success": true,
    "message": "string",
    "oldName": "string",
    "newName": "string",
    "path": "string"
  }
  ```
* 400 - Invalid request 
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Model file not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - New name already exists
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete a model file
Deletes a model file from the server. \
API path: /v1/core/model/{modelName}

**Parameter**
* *modelName* (string): Current name of the model file to delete.

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/model/string' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Model file deleted successfully
  ```
  {
    "success": true,
    "message": "string",
    "filename": "string"
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Model file not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Video API
### Upload a video file
Uploads a video file (MP4, AVI, MKV, etc.) to the server. The file will be saved in the videos directory. \
API path: /v1/core/video/upload

**No parameter**

**Request body**
* *file/files* (binary): Video file(s) to upload. Select multiple/single file(s) to upload at once.

**Request schema**
**Responses schema**
* 201 - Video file(s) uploaded successfully
  ```
  {
    "success": true,
    "message": "string",
    "count": 0,
    "files": [
      {
        "filename": "string",
        "originalFilename": "string",
        "path": "string",
        "size": 0,
        "url": "string"
      }
    ],
    "warnings": [
      "string"
    ]
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - File already exists
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### List uploaded video files
Returns a list of all video files that have been uploaded to the server. \
API path: /v1/core/video/list

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/video/list' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of video files
  ```
  {
    "success": true,
    "videos": [
      {
        "filename": "string",
        "path": "string",
        "size": 0,
        "modified": "string"
      }
    ],
    "count": 0,
    "directory": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Rename a video file
Renames a video file on the server. \
API path: /v1/core/video/{videoName}

**Parameter**
* *videoName* (string): Current name of the video file to rename.

**Request body**
```
{
  "newName": "my_video_v2.mp4"
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/video/videoName' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "newName": "my_video_v2.mp4"
}'
```
**Responses schema**
* 200 - Video file renamed successfully
  ```
  {
    "success": true,
    "message": "string",
    "oldName": "string",
    "newName": "string",
    "path": "string"
  }
  ```
* 400 - Invalid request (missing file, invalid format, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Video file not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - New name already exists
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete a video file
Deletes a video file from the server. \
API path: /v1/core/video/{videoName}

**Parameter**
* *videoName* (string): Current name of the video file to delete.

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/video/string' \
  -H 'accept: application/json'
```

**Responses schema**
* 200 - Video file deleted successfully
  ```
  {
    "success": true,
    "message": "string",
    "filename": "string"
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Video file not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Fonts API
### Upload a font file
Uploads a font file (TTF, OTF, WOFF, WOFF2, etc.) to the server. The file will be saved in the fonts directory. \
API path: /v1/core/font/upload

**No parameter**

**Request body**
* *file/files* (binary): Font file(s) to upload. Select multiple/single file(s) to upload at once.

**Request schema**

**Responses schema**
* 201 - Font file(s) uploaded successfully
  ```
  {
    "success": true,
    "message": "string",
    "count": 0,
    "files": [
      {
        "filename": "string",
        "originalFilename": "string",
        "path": "string",
        "size": 0,
        "url": "string"
      }
    ]
  }
  ```
* 400 - Invalid request (missing file, invalid format, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 401 - Unauthorized (missing or invalid API key)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - File already exists
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### List uploaded font files
Returns a list of all font files that have been uploaded to the server. \
API path: /v1/core/font/list

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/font/list' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of font files
  ```
  {
    "success": true,
    "fonts": [
      {
        "filename": "string",
        "path": "string",
        "size": 0,
        "modified": "string"
      }
    ],
    "count": 0,
    "directory": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Rename a font file
Renames a font file on the server. \
API path: /v1/core/font/{fontName}

**Parameter**
* *fontName* (string): Current name of the font file to rename

**Request body**
```
{
  "newName": "string"
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/font/fontName' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "newName": "string"
}'
```
**Responses schema**
* 200 - Font file renamed successfully
  ```
  {
    "success": true,
    "message": "string",
    "oldName": "string",
    "newName": "string"
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Font file not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - New name already exists
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Node API
### List all pre-configured nodes
Returns a list of all pre-configured nodes in the node pool. Can be filtered by availability status and category.  \
API path: /v1/core/node

**Parameters**
* *available* (string):Filter to show only available (not in use) nodes. Available values : true, false, 1, 0
* *category* (string): Filter nodes by category. Available values : source, detector, processor, destination, broker

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/node?available=true&category=source' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of nodes retrieved successfully
  ```
  {
    "nodes": [
      {
        "nodeId": "node_a1b2c3d4",
        "templateId": "rtsp_src_template",
        "displayName": "RTSP Source",
        "nodeType": "rtsp_src",
        "category": "source",
        "description": "Receive video stream from RTSP URL",
        "inUse": false,
        "parameters": {
          "rtsp_url": "rtsp://localhost:8554/stream1",
          "channel": "0",
          "resize_ratio": "1.0"
        },
        "requiredParameters": [
          "rtsp_url"
        ],
        "optionalParameters": [
          "channel",
          "resize_ratio"
        ],
        "templateDetailUrl": "/v1/core/node/template/rtsp_src_template",
        "parameterSchema": {
          "rtsp_url": {
            "name": "rtsp_url",
            "type": "string",
            "required": true,
            "description": "RTSP stream URL (e.g., rtsp://camera-ip:8554/stream)",
            "category": "connection",
            "uiHints": {
              "inputType": "url",
              "widget": "url-input",
              "placeholder": "rtsp://camera-ip:8554/stream"
            },
            "validation": {
              "pattern": "^(rtsp|rtmp|http|https|file|udp)://.+",
              "patternDescription": "Must be a valid URL (rtsp://, rtmp://, http://, https://, file://, or udp://)"
            },
            "examples": [
              "rtsp://192.168.1.100:8554/stream1",
              "rtsp://admin:password@camera-ip:554/stream"
            ]
          },
          "channel": {
            "name": "channel",
            "type": "integer",
            "required": false,
            "default": "0",
            "currentValue": "0",
            "description": "Channel number for multi-channel processing (0-15)",
            "category": "connection",
            "uiHints": {
              "inputType": "number",
              "widget": "input",
              "placeholder": "0"
            },
            "validation": {
              "min": 0,
              "max": 15
            }
          }
        },
        "input": [
          {
            "type": "external_source",
            "description": "Receives input from external source (file, RTSP, RTMP, etc.)"
          },
          {
            "type": "video_frames",
            "description": "Video frames from multiple channels/nodes (supports multiple inputs)",
            "format": "BGR/RGB image frames",
            "multiple": true,
            "minInputs": 2,
            "maxInputs": -1
          }
        ],
        "output": [
          {
            "type": "rtsp_stream",
            "description": "RTSP video stream",
            "protocol": "RTSP",
            "accessInfo": "Access via RTSP URL: rtsp://<host>:8000/stream (default: rtsp://localhost:8000/stream)",
            "example": "rtsp://localhost:8000/stream",
            "configurableParameters": [
              {
                "name": "port",
                "description": "RTSP server port",
                "default": "8000",
                "affects": "RTSP URL port"
              },
              {
                "name": "stream_name",
                "description": "RTSP stream name/path",
                "default": "stream",
                "affects": "RTSP URL path"
              }
            ]
          }
        ],
        "createdAt": "2025-01-15T10:30:00Z",
        "message": "Node created successfully",
        "oldNodeId": "node_old123",
        "newNodeId": "node_new456"
      }
    ],
    "total": 20,
    "available": 15,
    "inUse": 5
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Create a new pre-configured node
Creates a new pre-configured node from a template with specified parameters. \
API path: /v1/core/node

**No parameter**

**Request body**
```
{
  "templateId": "rtsp_src_template",
  "parameters": {
    "rtsp_url": "rtsp://localhost:8554/stream1",
    "channel": "0",
    "resize_ratio": "1.0"
  }
}
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/core/node' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "templateId": "rtsp_src_template",
  "parameters": {
    "rtsp_url": "rtsp://localhost:8554/stream1",
    "channel": "0",
    "resize_ratio": "1.0"
  }
}'
```
**Responses schema**
* 201 - Node created successfully
  ```
  {
    "nodeId": "node_a1b2c3d4",
    "templateId": "rtsp_src_template",
    "displayName": "RTSP Source",
    "nodeType": "rtsp_src",
    "category": "source",
    "description": "Receive video stream from RTSP URL",
    "inUse": false,
    "parameters": {
      "rtsp_url": "rtsp://localhost:8554/stream1",
      "channel": "0",
      "resize_ratio": "1.0"
    },
    "requiredParameters": [
      "rtsp_url"
    ],
    "optionalParameters": [
      "channel",
      "resize_ratio"
    ],
    "templateDetailUrl": "/v1/core/node/template/rtsp_src_template",
    "parameterSchema": {
      "rtsp_url": {
        "name": "rtsp_url",
        "type": "string",
        "required": true,
        "description": "RTSP stream URL (e.g., rtsp://camera-ip:8554/stream)",
        "category": "connection",
        "uiHints": {
          "inputType": "url",
          "widget": "url-input",
          "placeholder": "rtsp://camera-ip:8554/stream"
        },
        "validation": {
          "pattern": "^(rtsp|rtmp|http|https|file|udp)://.+",
          "patternDescription": "Must be a valid URL (rtsp://, rtmp://, http://, https://, file://, or udp://)"
        },
        "examples": [
          "rtsp://192.168.1.100:8554/stream1",
          "rtsp://admin:password@camera-ip:554/stream"
        ]
      },
      "channel": {
        "name": "channel",
        "type": "integer",
        "required": false,
        "default": "0",
        "currentValue": "0",
        "description": "Channel number for multi-channel processing (0-15)",
        "category": "connection",
        "uiHints": {
          "inputType": "number",
          "widget": "input",
          "placeholder": "0"
        },
        "validation": {
          "min": 0,
          "max": 15
        }
      }
    },
    "input": [
      {
        "type": "external_source",
        "description": "Receives input from external source (file, RTSP, RTMP, etc.)"
      },
      {
        "type": "video_frames",
        "description": "Video frames from multiple channels/nodes (supports multiple inputs)",
        "format": "BGR/RGB image frames",
        "multiple": true,
        "minInputs": 2,
        "maxInputs": -1
      }
    ],
    "output": [
      {
        "type": "rtsp_stream",
        "description": "RTSP video stream",
        "protocol": "RTSP",
        "accessInfo": "Access via RTSP URL: rtsp://<host>:8000/stream (default: rtsp://localhost:8000/stream)",
        "example": "rtsp://localhost:8000/stream",
        "configurableParameters": [
          {
            "name": "port",
            "description": "RTSP server port",
            "default": "8000",
            "affects": "RTSP URL port"
          },
          {
            "name": "stream_name",
            "description": "RTSP stream name/path",
            "default": "stream",
            "affects": "RTSP URL path"
          }
        ]
      }
    ],
    "createdAt": "2025-01-15T10:30:00Z",
    "message": "Node created successfully",
    "oldNodeId": "node_old123",
    "newNodeId": "node_new456"
  }
  ```
* 400 - Bad request (missing required fields or invalid parameters)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get node details
Returns detailed information about a specific pre-configured node. \
API path: /v1/core/node/{nodeId}

**Parameter**
* *nodeId* (string): Unique node identifier.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/node/node_a1b2c3d4' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Node details retrieved successfully
  ```
  {
    "nodeId": "node_rtsp_des_default",
    "templateId": "rtsp_des_template",
    "displayName": "RTSP Destination",
    "nodeType": "rtsp_des",
    "category": "destination",
    "description": "Output video stream via RTSP",
    "inUse": false,
    "parameters": {
      "channel": "0",
      "port": "8000"
    },
    "requiredParameters": [
      "port"
    ],
    "optionalParameters": [
      "channel",
      "stream_name"
    ],
    "templateDetailUrl": "/v1/core/node/template/rtsp_des_template",
    "input": [
      {
        "type": "video_frames",
        "description": "Video frames from previous node",
        "format": "BGR/RGB image frames"
      },
      {
        "type": "metadata",
        "description": "Metadata from previous nodes",
        "format": "JSON metadata"
      }
    ],
    "output": [
      {
        "type": "rtsp_stream",
        "description": "RTSP video stream",
        "protocol": "RTSP",
        "accessInfo": "Access via RTSP URL: rtsp://<host>:8000/stream (default: rtsp://localhost:8000/stream)",
        "example": "rtsp://localhost:8000/stream",
        "configurableParameters": [
          {
            "name": "port",
            "description": "RTSP server port",
            "default": "8000",
            "affects": "RTSP URL port"
          },
          {
            "name": "stream_name",
            "description": "RTSP stream name/path",
            "default": "stream",
            "affects": "RTSP URL path"
          }
        ]
      }
    ],
    "createdAt": "2025-12-16T11:33:52Z"
  }
  ```
* 404 - Node not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Update a node
Updates an existing node's parameters. Only nodes that are not currently in use can be updated. Note: Update operation deletes the old node and creates a new one with updated parameters. \
API path: /v1/core/node/{nodeId}

**Parameter**
* *nodeId* (string): Unique node identifier.

**Request body**
```
{
  "parameters": {
    "rtsp_url": "rtsp://localhost:8554/stream2",
    "channel": "0",
    "resize_ratio": "0.5"
  }
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/core/node/node_a1b2c3d4' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "parameters": {
    "rtsp_url": "rtsp://localhost:8554/stream2",
    "channel": "0",
    "resize_ratio": "0.5"
  }
}'
```
**Responses schema**
* 200 - Node updated successfully
  ```
  {
    "nodeId": "node_a1b2c3d4",
    "templateId": "rtsp_src_template",
    "displayName": "RTSP Source",
    "nodeType": "rtsp_src",
    "category": "source",
    "description": "Receive video stream from RTSP URL",
    "inUse": false,
    "parameters": {
      "rtsp_url": "rtsp://localhost:8554/stream1",
      "channel": "0",
      "resize_ratio": "1.0"
    },
    "requiredParameters": [
      "rtsp_url"
    ],
    "optionalParameters": [
      "channel",
      "resize_ratio"
    ],
    "templateDetailUrl": "/v1/core/node/template/rtsp_src_template",
    "parameterSchema": {
      "rtsp_url": {
        "name": "rtsp_url",
        "type": "string",
        "required": true,
        "description": "RTSP stream URL (e.g., rtsp://camera-ip:8554/stream)",
        "category": "connection",
        "uiHints": {
          "inputType": "url",
          "widget": "url-input",
          "placeholder": "rtsp://camera-ip:8554/stream"
        },
        "validation": {
          "pattern": "^(rtsp|rtmp|http|https|file|udp)://.+",
          "patternDescription": "Must be a valid URL (rtsp://, rtmp://, http://, https://, file://, or udp://)"
        },
        "examples": [
          "rtsp://192.168.1.100:8554/stream1",
          "rtsp://admin:password@camera-ip:554/stream"
        ]
      },
      "channel": {
        "name": "channel",
        "type": "integer",
        "required": false,
        "default": "0",
        "currentValue": "0",
        "description": "Channel number for multi-channel processing (0-15)",
        "category": "connection",
        "uiHints": {
          "inputType": "number",
          "widget": "input",
          "placeholder": "0"
        },
        "validation": {
          "min": 0,
          "max": 15
        }
      }
    },
    "input": [
      {
        "type": "external_source",
        "description": "Receives input from external source (file, RTSP, RTMP, etc.)"
      },
      {
        "type": "video_frames",
        "description": "Video frames from multiple channels/nodes (supports multiple inputs)",
        "format": "BGR/RGB image frames",
        "multiple": true,
        "minInputs": 2,
        "maxInputs": -1
      }
    ],
    "output": [
      {
        "type": "rtsp_stream",
        "description": "RTSP video stream",
        "protocol": "RTSP",
        "accessInfo": "Access via RTSP URL: rtsp://<host>:8000/stream (default: rtsp://localhost:8000/stream)",
        "example": "rtsp://localhost:8000/stream",
        "configurableParameters": [
          {
            "name": "port",
            "description": "RTSP server port",
            "default": "8000",
            "affects": "RTSP URL port"
          },
          {
            "name": "stream_name",
            "description": "RTSP stream name/path",
            "default": "stream",
            "affects": "RTSP URL path"
          }
        ]
      }
    ],
    "createdAt": "2025-01-15T10:30:00Z",
    "message": "Node created successfully",
    "oldNodeId": "node_old123",
    "newNodeId": "node_new456"
  }
  ```
* 400 - Bad request (missing parameters field)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Node not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - Conflict (node is currently in use)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete a node  
Deletes a pre-configured node. Only nodes that are not currently in use can be deleted. \
API path: /v1/core/node/{nodeId}

**Parameter**
* *nodeId* (string): Unique node identifier.

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/core/node/node_a1b2c3d4' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Node deleted successfully
  ```
  {
    "message": "Node deleted successfully",
    "nodeId": "node_a1b2c3d4"
  }
  ```
* 404 - Node not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 409 - Conflict (node is currently in use)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### List all node templates
Returns a list of all available node templates that can be used to create pre-configured nodes. \
Available Node Types: \
Source Nodes:
* rtsp_src - RTSP Source
* file_src - File Source
* app_src - App Source
* image_src - Image Source
* rtmp_src - RTMP Source
* udp_src - UDP Source

Detector Nodes:
* yunet_face_detector - YuNet Face Detector
* yolo_detector - YOLO Detector
* yolov11_detector - YOLOv11 Detector
* mask_rcnn_detector - Mask R-CNN Detector
* openpose_detector - OpenPose Detector
* enet_seg - ENet Segmentation
* trt_yolov8_detector - TensorRT YOLOv8 Detector (requires CVEDIX_WITH_TRT)
* trt_yolov8_seg_detector - TensorRT YOLOv8 Segmentation (requires CVEDIX_WITH_TRT)
* trt_yolov8_pose_detector - TensorRT YOLOv8 Pose (requires CVEDIX_WITH_TRT)
* trt_yolov8_classifier - TensorRT YOLOv8 Classifier (requires CVEDIX_WITH_TRT)
* trt_vehicle_detector - TensorRT Vehicle Detector (requires CVEDIX_WITH_TRT)
* trt_vehicle_plate_detector - TensorRT Vehicle Plate Detector (requires CVEDIX_WITH_TRT)
* trt_vehicle_plate_detector_v2 - TensorRT Vehicle Plate Detector v2 (requires CVEDIX_WITH_TRT)
* trt_insight_face_recognition - TensorRT InsightFace Recognition (requires CVEDIX_WITH_TRT)
* rknn_yolov8_detector - RKNN YOLOv8 Detector (requires CVEDIX_WITH_RKNN)
* rknn_yolov11_detector - RKNN YOLOv11 Detector (requires CVEDIX_WITH_RKNN)
* rknn_face_detector - RKNN Face Detector (requires CVEDIX_WITH_RKNN)
* ppocr_text_detector - PaddleOCR Text Detector (requires CVEDIX_WITH_PADDLE)
* face_swap - Face Swap
* insight_face_recognition - InsightFace Recognition
* mllm_analyser - MLLM Analyser

Processor Nodes:
* sface_feature_encoder - SFace Feature Encoder
* sort_track - SORT Tracker
* face_osd_v2 - Face OSD v2
* ba_crossline - BA Crossline
* ba_crossline_osd - BA Crossline OSD
* classifier - Classifier
* lane_detector - Lane Detector
* restoration - Restoration
* trt_vehicle_feature_encoder - TensorRT Vehicle Feature Encoder (requires CVEDIX_WITH_TRT)
* trt_vehicle_color_classifier - TensorRT Vehicle Color Classifier (requires CVEDIX_WITH_TRT)
* trt_vehicle_type_classifier - TensorRT Vehicle Type Classifier (requires CVEDIX_WITH_TRT)
* trt_vehicle_scanner - TensorRT Vehicle Scanner (requires CVEDIX_WITH_TRT)

Destination Nodes:
* file_des - File Destination
* rtmp_des - RTMP Destination
* screen_des - Screen Destination

Broker Nodes:
* json_console_broker - JSON Console Broker
* json_enhanced_console_broker - JSON Enhanced Console Broker
* json_mqtt_broker - JSON MQTT Broker (requires CVEDIX_WITH_MQTT)
* json_kafka_broker - JSON Kafka Broker (requires CVEDIX_WITH_KAFKA)
* xml_file_broker - XML File Broker
* xml_socket_broker - XML Socket Broker
* msg_broker - Message Broker
* ba_socket_broker - BA Socket Broker
* embeddings_socket_broker - Embeddings Socket Broker
* embeddings_properties_socket_broker - Embeddings Properties Socket Broker
* plate_socket_broker - Plate Socket Broker
* expr_socket_broker - Expression Socket Broker
All node templates are automatically imported from CVEDIX SDK nodes available in /opt/cvedix/include/cvedix/nodes/infers. \
API path: /v1/core/node/template

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/node/template' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - List of templates retrieved successfully
  ```
  {
    "templates": [
      {
        "templateId": "rtsp_src_template",
        "nodeType": "rtsp_src",
        "displayName": "RTSP Source",
        "description": "Receive video stream from RTSP URL",
        "category": "source",
        "isPreConfigured": false,
        "defaultParameters": {
          "channel": "0",
          "resize_ratio": "1.0"
        },
        "requiredParameters": [
          "rtsp_url"
        ],
        "optionalParameters": [
          "channel",
          "resize_ratio"
        ]
      }
    ],
    "total": 10
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get template details
Returns detailed information about a specific node template. \
API path: /v1/core/node/template/{templateId}

**Parameter**
* *templateId* (string): Unique template identifier.

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/node/template/rtsp_src_template' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Template details retrieved successfully
  ```
  {
    "templateId": "rtsp_src_template",
    "nodeType": "rtsp_src",
    "displayName": "RTSP Source",
    "description": "Receive video stream from RTSP URL",
    "category": "source",
    "isPreConfigured": false,
    "defaultParameters": {
      "channel": "0",
      "resize_ratio": "1.0"
    },
    "requiredParameters": [
      "rtsp_url"
    ],
    "optionalParameters": [
      "channel",
      "resize_ratio"
    ]
  }
  ```
* 404 - Template not found
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get node pool statistics
Returns statistics about the node pool including total templates, total nodes, available nodes, and nodes by category. \
API path: /v1/core/node/stats

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/core/node/stats' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Statistics retrieved successfully
  ```
  {
    "totalTemplates": 10,
    "totalPreConfiguredNodes": 20,
    "availableNodes": 15,
    "inUseNodes": 5,
    "nodesByCategory": {
      "source": 5,
      "detector": 8,
      "processor": 4,
      "destination": 3,
      "broker": 0
    }
  }
  ```
* 500 - Internal server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```

## Recognition API
### Recognize faces from uploaded image
Recognizes faces from an uploaded image. The image can be provided as base64-encoded data in a multipart/form-data request. Returns face detection results including bounding boxes, landmarks, recognized subjects, and execution times. \
API path: /v1/recognition/recognize 

**Parameters**
* *limit* (integer): Maximum number of faces to recognize (0 = no limit). Recognizes the largest faces first. Default value : 0
* *prediction_count* (integer): Number of predictions to return per face. Default value : 1
* *det_prob_threshold* (float): Detection probability threshold (0.0 to 1.0). Default value : 0.5
* *face_plugins* (string): Face plugins to enable (comma-separated).
* *status* (string): Status filter.
* *detect_faces* (boolean): Whether to detect faces (true) or only recognize existing faces (false). Default value : true

**Request body**
* *file*: Image file as base64-encoded data or binary data.

**Request schema**

**Responses schema**
* 200 - Face recognition results
  ```
  {
    "result": [
      {
        "box": {
          "probability": 1,
          "x_max": 1420,
          "y_max": 1368,
          "x_min": 548,
          "y_min": 295
        },
        "landmarks": [
          [
            814,
            713
          ],
          [
            1104,
            829
          ],
          [
            832,
            937
          ],
          [
            704,
            1030
          ],
          [
            1017,
            1133
          ]
        ],
        "subjects": [
          {
            "similarity": 0.97858,
            "subject": "subject1"
          }
        ],
        "execution_time": {
          "age": 28,
          "gender": 26,
          "detector": 117,
          "calculator": 45,
          "mask": 36
        }
      }
    ]
  }
  ```
* 400 - Invalid request (missing file, invalid image format, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 401 - Unauthorized (missing or invalid API key)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### List face subjects
Retrieves a list of all saved face subjects with pagination support. You can filter by subject name or retrieve all subjects. \
This endpoint returns paginated results with face information including image_id and subject name. \
API path: /v1/recognition/faces 

**Parameters**
* *page* (integer): Page number of examples to return. Used for pagination. Default value is 0
* *size* (integer): Number of faces per page (page size). Used for pagination. Default value is 20
* *subject* (string): Specifies the subject examples to return. If empty, returns examples for all subjects.

**No request body**

**Request schema**

**Responses schema**
* 200 - List of face subjects retrieved successfully
  ```
  {
    "faces": [
      {
        "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
        "subject": "subject1"
      },
      {
        "image_id": "7c246g6c-b476-5633-c2g2-5d0bd3ee1839",
        "subject": "subject1"
      }
    ],
    "page_number": 0,
    "page_size": 20,
    "total_pages": 2,
    "total_elements": 12
  }
  ```
* 401 - Unauthorized (missing or invalid API key)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Register face subjects
Registers a face subject by storing the image. You can add as many images as you want to train the system. Images should contain only a single face. \
The system will detect the face in the image and store the face features for recognition. \
API path: /v1/recognition/faces 

**Parameeters**
* *subject* (string): Subject name to register (e.g., "subject1")
* *deb_prob_threshold* (float): Detection probability threshold (0.0 to 1.0). Default value : 0.5

**Request body**
* *file*: Image file to upload.
Image data can be sent in two formats:
  1. Multipart/form-data: Upload file directly (recommended for file uploads)
  2. Application/json: Send base64-encoded image string

Supported image formats: JPEG, JPG, PNG, BMP, GIF, ICO, TIFF, WebP Maximum file size: 5MB 

**Request schema**

**Responses schema**
* 200 - Face subject registered successfully
  ```
  {
    "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
    "subject": "subject1"
  }
  ```
* 400 - Invalid request (missing subject, invalid image format, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 401 - Unauthorized (missing or invalid API key)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete face subject by ID or subject name
Deletes a face subject by its image ID or subject name.
* If the identifier is an image_id, only that specific face will be deleted.
* If the identifier is a subject name, all faces associated with that subject will be deleted.

The endpoint automatically detects whether the identifier is an image_id or subject name. If no face is found with the given identifier, a 404 error is returned.
API path: /v1/core/recognition/faces/{image_id} \

**Parameters**
* *image_id* (string): Either the UUID of the face subject to be deleted, or the subject name. The endpoint will automatically detect the type of identifier. Example : 6b135f5b-a365-4522-b1f1-4c9ac2dd0728

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/recognition/faces/6b135f5b-a365-4522-b1f1-4c9ac2dd0728' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Face subject(s) deleted successfully
  * Single face deleted
    ```
    {
    "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
    "subject": "subject1"
    }
    ```
  * All faces for a subject deleted
    ```
    {
      "subject": "subject1",
      "deleted_count": 3,
      "image_ids": [
        "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
        "7c246g6c-b476-5633-c2g2-5d0bd3ee1839",
        "8d357h7d-c587-6744-d3h3-6e1ce4ff2950"
      ]
    }
    ```
* 400 - Bad request (missing image_id, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 401 - Unauthorized (missing or invalid API key)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 404 - Face subject not found
  ```
  {
    "error": "Not Found",
    "message": "Face subject with identifier '6b135f5b-a365-4522-b1f1-4c9ac2dd0728' not found"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete multiple face subjects
Deletes multiple face subjects by their image IDs. If some IDs do not exist, they will be ignored. This endpoint is available since version 1.0. \
API path: /v1/recognition/faces/delete \

**No parameter**
**Request body**
```
[
  "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
  "7c246g6c-b476-5633-c2g2-5d0bd3ee1839"
]
```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/recognition/faces/delete' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '[
  "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
  "7c246g6c-b476-5633-c2g2-5d0bd3ee1839"
]'
```
**Responses schema**
* 200 - Face subjects deleted successfully
  ```
  {
    "deleted": [
      {
        "image_id": "6b135f5b-a365-4522-b1f1-4c9ac2dd0728",
        "subject": "subject1"
      },
      {
        "image_id": "7c246g6c-b476-5633-c2g2-5d0bd3ee1839",
        "subject": "subject2"
      }
    ]
  }
  ```
* 400 - Bad request (invalid JSON, not an array, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 401 - Unauthorized (missing or invalid API key)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete all face subjects
Deletes all face subjects from the database. This operation is irreversible. All image IDs and subject mappings will be removed. \
Warning: This endpoint will delete all registered faces. Use with caution. \
API path: /v1/recognition/faces/all

**No parameter** 

**No request body** 

**Request schema** 
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/recognition/faces/all' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - All face subjects deleted successfully
  ```
  {
    "deleted_count": 15,
    "message": "All face subjects deleted successfully"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Search appearance subject 
Search for similar faces in the database given an input face image. Returns a list of matching faces sorted by similarity (highest first) above the specified threshold.
How it works:
  1. Detects faces in the uploaded image
  2. Extracts face embeddings from the first detected face
  3. Compares with all faces in the database
  4. Returns matches with similarity >= threshold, sorted from highest to lowest
Response fields:
* *image_id*: Unique identifier of the matched face
* *subject*: Name of the matched subject
* *similarity*: Cosine similarity score (0.0 - 1.0)
* *face_image*: Base64 encoded face image (reserved for future use)

API path: /v1/recognition/search

**Parameters**
* *threshold* (float): Minimum similarity threshold for matches (0.0 - 1.0). Default value : 0.5
* *limit* (integer): Maximum number of results to return (0 = no limit). Default value : 0
* *det_prob_threshold* (float): Face detection probability threshold. Default value : 0.5

**Request body**
* *file*: Image file containing a face to search

**Request schema**

**Responses schema**
* 200 - Search result
  ```
  {
    "result": [
      {
        "image_id": "abc123-def456-ghi789",
        "subject": "John Doe",
        "similarity": 0.95,
        "face_image": ""
      },
      {
        "image_id": "xyz789-uvw456-rst123",
        "subject": "Jane Smith",
        "similarity": 0.82,
        "face_image": ""
      }
    ],
    "faces_found": 2,
    "threshold": 0.5
  }
  ```
* 400 - Invalid request
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Rename face subject
Renames an existing face subject. This endpoint searches for the face subject name in the face database and allows you to select and rename that subject. \
How it works:
  1. The endpoint searches for the subject name specified in the URL path ({subject}) in the face database.
  2. If found, it renames the subject to the new name provided in the request body.
  3. All face registrations associated with the old subject name are updated to use the new name.
Behavior:
  * If the new subject name does not exist, all faces from the old subject are moved to the new name.
  * If the new subject name already exists, the subjects are merged. The embeddings are averaged and all faces from the old subject are reassigned to the subject with the new name, and the old subject is removed.
  * All image_id mappings are automatically updated to reflect the new subject name.
  * The face database file is updated to persist the changes.
Note: The subject name in the URL path must match exactly with a subject name in the database. Use GET /v1/recognition/faces to list all available subjects. \
This endpoint is available since version 0.6. \
API path: /v1/recognition/subject/{subject}

**Parameter**
* *subject* (string): The current subject name to rename. This must match exactly with a subject name in the face database. Use GET /v1/recognition/faces to see all available subjects.

**Request body**
```
{
  "subject": "testing_face"
}
```
**Request schema**
```
curl -X 'PUT' \
  'http://localhost:8080/v1/recognition/subjects/subject1' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "subject": "testing_face"
}'
```
**Responses schema**
* 200 - Subject renamed successfully
  ```
  {
    "updated": "true",
    "old_subject": "subject1",
    "new_subject": "testing_face",
    "message": "Subject renamed successfully"
  }
  ```
* 400 - Bad request (subject not found, missing subject, invalid JSON, etc.)
  ```
  {
    "updated": "false",
    "error": "Subject 'subject1' not found in face database",
    "old_subject": "subject1"
  }
  ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Configure face database connection
Configures the face database connection for MySQL or PostgreSQL. This endpoint allows you to configure a database connection to store face recognition data instead of using the default face_database.txt file. \
Database Support:
* MySQL (default port: 3306)
* PostgreSQL (default port: 5432)

Database Schema: The system expects two tables in the database:
  1. `face_libraries` table:
    * id (primary key, auto-increment)
    * image_id (varchar 36) - Unique identifier for each face image
    * subject (varchar 255) - Subject/name of the person
    * base64_image (longtext) - Base64-encoded face image
    * embedding (text) - Face embedding vector (comma-separated floats)
    * created_at (timestamp) - Creation timestamp
    * machine_id (varchar 255) - Machine identifier
    * mac_address (varchar 255) - MAC address
  2. `face_log` table: 
    * id (primary key, auto-increment)
    * request_type (varchar 50) - Type of request (recognize, register, etc.)
    * timestamp (datetime) - Request timestamp
    * client_ip (varchar 45) - Client IP address
    * request_body (longtext) - Request body JSON
    * response_body (longtext) - Response body JSON
    * response_code (int) - HTTP response code
    * notes (text) - Additional notes
    * mac_address (varchar 255) - MAC address
    * machine_id (varchar 255) - Machine identifier
Behavior:
* If database connection is configured and enabled, the system will use the database instead of face_database.txt
* If enabled: false is sent, the database connection is disabled and the system falls back to face_database.txt
* Configuration is persisted in config.json under the face_database section
* The configuration takes effect immediately after being saved
Note: The database tables must be created manually before using this endpoint. The endpoint only configures the connection parameters, it does not create the database schema.\
API path: /v1/recognition/face-database/connection

**No parameter** 

**Request body**
* MySQL configuration example
  ```
  {
    "type": "mysql",
    "host": "localhost",
    "port": 3306,
    "database": "face_db",
    "username": "face_user",
    "password": "secure_password",
    "charset": "utf8mb4"
  }
  ```
* PostgreSQL configuration example
  ```
  {
    "type": "postgresql",
    "host": "localhost",
    "port": 5432,
    "database": "face_db",
    "username": "face_user",
    "password": "secure_password"
  }
  ```
* Disable database connection
  ```
  {
    "enabled": false
  }
  ```
**Request schema**
```
curl -X 'POST' \
  'http://localhost:8080/v1/recognition/face-database/connection' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "enabled": false
}'
```
**Responses schema**
* 200 - Database connection configured successfully
  * Configure successfully
    ```
    {
      "message": "Face database connection configured successfully",
      "config": {
        "type": "mysql",
        "host": "localhost",
        "port": 3306,
        "database": "face_db",
        "username": "face_user",
        "charset": "utf8mb4"
      },
      "note": "Database connection will be used instead of face_database.txt file"
    }
    ```
  * Database disabled
    ```
    {
      "message": "Database connection disabled. Using default face_database.txt file",
      "enabled": false,
      "default_file": "/opt/edgeos-api/data/face_database.txt"
    }
    ```
* 400 - Bad request (missing required fields, invalid database type, etc.)
  ```
  {
    "error": "Bad request",
    "message": "Field 'type' (mysql/postgresql) is required"
  }
  ```
* 500 - Server error (failed to save configuration, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Get face database connection configuration
Retrieves the current face database connection configuration. If no database is configured or the database connection is disabled, returns information about the default `face_database.txt` file. \
Response:
* If database is configured: Returns the connection configuration details
* If database is not configured: Returns enabled: false and the path to the default file

API path: /v1/recognition/face-database/connection

**No parameter**

**No request body**

**Request schema**
```
curl -X 'GET' \
  'http://localhost:8080/v1/recognition/face-database/connection' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Configuration retrieved successfully
  * Database enabled
    ```
    {
      "enabled": true,
      "config": {
        "type": "mysql",
        "host": "localhost",
        "port": 3306,
        "database": "face_db",
        "username": "face_user",
        "charset": "utf8mb4"
      },
      "message": "Database connection is configured and enabled"
    }
    ```
  * Database disabled
    ```
    {
      "enabled": false,
      "message": "No database connection configured. Using default face_database.txt file",
      "default_file": "/opt/edgeos-api/data/face_database.txt"
    }
    ```
* 500 - Server error
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
### Delete face database connection configuration
Deletes the face database connection configuration. After deletion, the system will fall back to using the default face_database.txt file for storing face recognition data. \
Behavior:
* Removes the database connection configuration from config.json
* Sets enabled to false in the configuration
* System immediately switches to using face_database.txt file
* If no database configuration exists, returns a success response indicating no configuration was found

Note: This operation does not delete any data from the database itself. It only removes the connection configuration. To delete data from the database, use the face deletion endpoints. \
API path: /v1/recognition/face-database/connection

**No parameter**

**No request body**

**Request schema**
```
curl -X 'DELETE' \
  'http://localhost:8080/v1/recognition/face-database/connection' \
  -H 'accept: application/json'
```
**Responses schema**
* 200 - Database connection configuration deleted successfully
  * Configuration deleted successfully
    ```
    {
      "message": "Database connection configuration deleted successfully. System will now use default face_database.txt file",
      "enabled": false,
      "default_file": "/opt/edgeos-api/data/face_database.txt"
    }
    ```
  * No configuration to delete
    ```
    {
      "message": "No database connection configured to delete",
      "enabled": false,
      "default_file": "/opt/edgeos-api/data/face_database.txt"
    }
    ```
* 500 - Server error (failed to update configuration, etc.)
  ```
  {
    "error": "string",
    "code": 0,
    "message": "string"
  }
  ```
