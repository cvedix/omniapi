# OmniAPI

[English](README.md) | [Tiếng Việt](README_vi.md)

**OmniAPI** is a high-performance **REST API Server** designed to make Edge AI **effortless for developers**. Whether you're building a Mobile App, an intelligent AI PC, or seamlessly integrating advanced analytics into a customer's Video Management System (VMS) or Enterprise Server – OmniAPI lets you execute state-of-the-art AI workloads in seconds using simple HTTP & JSON.

**Zero AI Knowledge Required.** No need to learn about tensor shapes, ONNX model conversions, PyTorch configurations, or hardware dependencies. If you know how to make an API call, you know how to deploy Vision AI.

![OmniAPI Architecture Concept](asset/architecture.png)

---

## ⚡ What is OmniAPI?

Built on top of the robust CVEDIX EdgeOS engine, OmniAPI abstracts complex GStreamer media pipelines and lower-level inference engines into straightforward, universal **REST API calls**.

By acting as a universal translation layer, OmniAPI empowers application teams to integrate complex Vision AI features (Face Recognition, License Plate Reading, Behavior Analysis) directly into their applications **in a flash**, drastically reducing integration time from weeks to hours. 

### 🌟 Key Developer Benefits
- **Effortless Integration:** Call our REST API from any platform: iOS, Android, Web UI, Desktop software, or Backend Servers.
- **Zero AI Knowledge Required:** You send the video stream URL (RTSP/RTMP/File) and tell OmniAPI what you want to detect. OmniAPI handles the AI pipelines, object tracking, scaling, and bounding boxes internally.
- **Production-Ready Accuracy:** Ships with pre-trained, high-accuracy models that are robust and ready-to-use out-of-the-box in real-world environments.
- **VMS-Ready:** Seamless integration with existing Video Management Systems (Milestone, Exacq, Nx Witness) via standard streams and HTTP Webhooks.
- **Hardware Agnostic (Write once, run anywhere):** OmniAPI automatically optimizes processing for the underlying NPUs/GPUs (NVIDIA, Rockchip, Intel, Hailo, Qualcomm). No custom code for different hardware!

---

## 🔌 Integrating in a Flash

Everything in OmniAPI is designed with an API-first mindset. Start an advanced Vision pipeline instantly using simple JSON payloads.

### 🟢 Tier 1 — Zero Knowledge (Just pick a category)
*Turn any camera into a smart AI node — no model configuration needed.*

```bash
curl -X POST http://localhost:8080/v1/securt/instance \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Lobby Camera",
    "category": "security",
    "input": { "url": "rtsp://admin:pass@192.168.1.100/stream", "type": "rtsp" },
    "output": { "url": "rtmp://localhost:1935/live/lobby", "type": "rtmp" },
    "autoStart": true
  }'
```

### 🔵 Tier 2 — Choose a Specific Feature
*Select a targeted AI feature within a category for precise analytics.*

```bash
# Traffic jam detection — just pick category + feature
curl -X POST http://localhost:8080/v1/securt/instance \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Intersection Monitor",
    "category": "its",
    "feature": "jam",
    "input": { "url": "rtsp://10.0.0.50/stream", "type": "rtsp" },
    "autoStart": true
  }'
```

### 🟣 Tier 3 — Custom Model (Expert)
*Bring your own trained model — full control for AI researchers and engineers.*

```bash
curl -X POST http://localhost:8080/v1/securt/instance \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Custom Fire Detection v2",
    "category": "custom",
    "solution": "fire_smoke_detection",
    "input": { "url": "rtsp://10.0.0.100/stream", "type": "rtsp" },
    "additionalParams": {
      "MODEL_PATH": "/opt/models/fire_v2.weights",
      "CONFIG_PATH": "/opt/models/fire_v2.cfg",
      "LABELS_PATH": "/opt/models/fire_labels.txt"
    },
    "detectionSensitivity": 0.35
  }'
```

### Python / Mobile / Backend Example
*Build security analytics natively into your Backend or App in 5 lines of code.*

```python
import requests

API_BASE = "http://localhost:8080/v1/securt"

# 1. Start a security instance (zero AI knowledge needed!)
response = requests.post(f"{API_BASE}/instance", json={
    "name": "Perimeter Camera",
    "category": "security",
    "input": {"url": "rtsp://192.168.1.100:554/live", "type": "rtsp"},
    "autoStart": True
})
instance_id = response.json().get("id")

# 2. Add an intrusion line dynamically
requests.post(f"{API_BASE}/instance/{instance_id}/lines", json={
    "name": "Fence Zone",
    "coordinates": [{"x": 100, "y": 500}, {"x": 1800, "y": 500}],
    "direction": "Up",
    "classes": ["Person", "Vehicle"]
})
print("Intrusion line armed successfully!")
```

---

## 🤝 Supported Hardware Acceleration

OmniAPI automatically leverages available hardware accelerators to maximize inference FPS while minimizing power consumption.

| Vendor | Specific SOC / Family | Acceleration Backend |
|--------|----------------------|-----------------------|
| ✅ **NVIDIA** | Jetson AGX Orin, Orin Nano, RTX GPUs | TensorRT |
| ✅ **Rockchip** | RK3588 (OPI5-Plus) | RKNN |
| ✅ **Hailo** | Hailo-8 (1200 / 3300) | HailoRT |
| ✅ **Qualcomm** | QCS6490 (DK2721) | SNPE / QNN |
| ✅ **Intel** | Core Ultra (R360) | OpenVINO |
| ✅ **AMD** | Ryzen 8000 (2210) | Vitis AI |

---

## 🚀 Quick Start

### 1. Installation

**Recommended: Debian ALL-IN-ONE Package**  
The easiest way to get started is by utilizing our pre-built standalone `.deb` package containing the API server and all required edge dependencies.
```bash
# Install the downloaded package
sudo dpkg -i omniapi-all-in-one-*.deb
sudo apt-get install -f

# Start the OmniAPI daemon
sudo systemctl start omniapi
```
*(For manual build instructions, please refer to [INSTALLATION.md](docs/INSTALLATION.md))*

### 2. Verify Server Status

```bash
# Check if the API is running correctly
curl -s http://localhost:8080/v1/securt/health | jq
```

---

## 🎯 Solution Categories

OmniAPI organizes **43+ optimized edge processing nodes** into **solution categories**. Just pick a category — no need to manage models:

| Category | Default Feature | Available Features | Use Case |
|----------|----------------|-------|----------|
| 🛡️ **security** | SecuRT (full pipeline) | `crossline`, `intrusion`, `loitering`, `crowding`, `face` | Enterprise security, perimeter defense |
| 🚗 **its** | Crossline counting | `line_counting`, `jam`, `stop`, `wrong_way`, `obstacle` | Smart traffic, intelligent transportation |
| 🔥 **firefighting** | Fire/Smoke detection | `fire`, `smoke` | Fire safety, industrial monitoring |
| 🎯 **armed** | SecuRT (full pipeline) | — | Military / defense applications |
| 🔧 **custom** | *(user-defined)* | *(requires explicit solution + model paths)* | Research, custom-trained models |

---

## 📚 Ecosystem & Documentation

- **Swagger / OpenAPI Spec:** View the interactive API sandbox at `http://localhost:8080/swagger`
- **[API Reference](docs/API_document.md):** Complete documentation on every endpoint.
- **[Architecture Guide](docs/ARCHITECTURE.md):** Deep dive into how OmniAPI communicates with the underlying AI Runtime and EdgeOS SDK.
- **[Development Guide](docs/DEVELOPMENT.md):** How to structure code, write new controllers, and build custom models.
- **[Environment Variables](docs/ENVIRONMENT_VARIABLES.md):** Configure ports, thread limits, logging, and database paths.

---

## 📝 License

Proprietary - CVEDIX
