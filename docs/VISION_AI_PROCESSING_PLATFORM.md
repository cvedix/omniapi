# Vision: Nền tảng Edge AI (API + xử lý AI)

## Định vị

**Edge AI API** là **nền tảng Edge AI**: cung cấp REST API và xử lý AI trực tiếp trên thiết bị biên. CVEDIX SDK (EdgeOS SDK) đóng vai trò **tầng hỗ trợ** — không phải là điểm tích hợp duy nhất; API và AI Runtime mới là lớp thống nhất.

## Luồng kiến trúc

```
Client → REST API → AI Runtime (decode / infer / cache) → CVEDIX SDK (pipeline)
```

- **REST API**: Instance, Solution, Recognition, Lines/Jams/Stops, Push frame, Metrics.
- **AI Runtime**: InferenceSession, AIRuntimeFacade, PipelineHelper — decode, inference, cache tùy chọn.
- **CVEDIX SDK**: Pipeline (source → detector → tracker → broker → destination).

## Kết quả mong muốn

- Một điểm tích hợp SDK qua AI Runtime.
- Recognition, push frame, batch dùng chung decode + inference + cache.
- Người đọc hiểu: Edge AI API = nền tảng; SDK = tay hỗ trợ.

## Tài liệu liên quan

- [ARCHITECTURE.md](ARCHITECTURE.md) — Diagram API → AI Runtime → SDK.
- [AI_RUNTIME_DESIGN.md](AI_RUNTIME_DESIGN.md) — Thiết kế InferenceSession, AIRuntimeFacade.
- [task/edgeos-api/00_MASTER_PLAN.md](../task/edgeos-api/00_MASTER_PLAN.md) — Master plan & phases.
