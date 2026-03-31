# Zero-Downtime Atomic Pipeline Swap Architecture

**Design for real-time video analytics with hot updates (PATCH/PUT) without RTMP reconnects.**

---

## 1. Executive Summary

This document describes an architecture where:

- **RTMP output and encoder stay alive** during pipeline configuration updates (PATCH/PUT).
- **New AI pipelines** are built and warmed up **before** activation.
- An **atomic pointer swap** switches the active pipeline in O(1).
- **Old pipelines drain** remaining frames before destruction.
- **No RTMP reconnects** occur (single publisher per stream key, ZLMediaKit).
- **Frame drops** are minimized (target: 0–2 frames).

The design extends the existing `WorkerHandler` + `PipelineSnapshot` + `RtmpLastFrameFallbackProxyNode` with a **persistent output leg** (proxy + rtmp_des) and a **frame router** that feeds either the active AI pipeline or a last-frame pump into that leg.

---

## 2. System Architecture

### 2.1 High-Level Data Flow

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                  Worker Process                         │
                    │                                                         │
  RTSP/RTMP ──────► │  Source Node  ──►  Decoder  ──►  [AI Leg]  ──►  OSD     │
  (input)           │       │                │             │            │     │
                    │       │                │             │            │     │
                    │       │                │     ┌───────┴────────────┼─────┼──► Frame Router
                    │       │                │     │                   │     │    (atomic read
                    │       │                │     │  active_pipeline_  │     │     of pipeline ptr)
                    │       │                │     │  (shared_ptr)      │     │
                    │       │                │     └───────────────────┘     │
                    │       │                │                               ▼
                    │       │                │                    ┌──────────────────────┐
                    │       │                │                    │ RtmpLastFrame        │
                    │       │                └───────────────────►│ FallbackProxyNode    │──► rtmp_des ──► ZLMediaKit
                    │       │                     (last-frame     │ (PERSISTENT)         │     (single
                    │       │                      pump during     └──────────────────────┘     connection)
                    │       │                      swap)                    ▲
                    │       │                                              │
                    │       └──────────────────────────────────────────────┘
                    │              (optional: inject during swap)
                    └─────────────────────────────────────────────────────────┘
```

### 2.2 Core Idea: Persistent Output Leg

- **One** `RtmpLastFrameFallbackProxyNode` + **one** `cvedix_rtmp_des_node` per instance (stream key).
- They are **not** part of the swappable pipeline snapshot; they live in a **persistent output context**.
- The **AI leg** (source → decoder → detector → … → OSD) is the **swappable** part; its **output** is connected to the **frame router**, which forwards frames into the proxy.
- During swap: new pipeline is built (with a **virtual** or **stub** output that feeds the same router); once ready, **atomic swap** of the “active pipeline” pointer; old pipeline **drains** then is destroyed; **no reconnection** of RTMP.

### 2.3 Component Roles

| Component | Role |
|-----------|------|
| **PipelineSnapshot** | Immutable graph of nodes (source → … → OSD). No rtmp_des inside; output goes to router. |
| **FrameRouter** | Holds `std::atomic<std::shared_ptr<PipelineSnapshot>>` (or equivalent); forwards frames from the active pipeline’s OSD output to the persistent proxy. During swap, can accept last-frame from a pump. |
| **RtmpLastFrameFallbackProxyNode** | Persistent. Receives frames from FrameRouter (or last-frame pump). Forwards to rtmp_des. Keeps RTMP connection alive. |
| **rtmp_des** | Persistent. Single connection to ZLMediaKit for the stream key. |
| **Last-frame pump** | Optional. During swap, pushes last good frame into the proxy so the server never sees a gap. |
| **WorkerHandler** | Owns persistent output leg, frame router, and active pipeline pointer; drives build → warm-up → atomic swap → drain old. |

---

## 3. Threading Model

### 3.1 Threads Involved

| Thread | Responsibility |
|--------|----------------|
| **IPC server thread** | Handles PATCH/PUT, START/STOP; triggers hot-swap logic (build new pipeline, then swap). Does **not** push frames. |
| **Source node thread(s)** | CVEDIX SDK: pull from RTSP/RTMP, decode, push into pipeline graph. |
| **Pipeline graph threads** | SDK internal: detector, tracker, OSD, etc. Process frames and eventually call into the node that feeds the FrameRouter. |
| **Frame router** | Called from pipeline thread(s) when the OSD (or terminal node) produces a frame. No dedicated thread; runs in pipeline context. |
| **Last-frame pump thread** (optional) | Only active during swap: periodically calls `proxy->inject_frame(last_frame_)` so RTMP keeps receiving data. |
| **Drain / teardown** | After swap, old pipeline’s source is stopped; remaining frames in the old graph drain; then `PipelineSnapshot` destructor runs (e.g. in worker thread or a small cleanup context). |

### 3.2 Lock Discipline

- **Active pipeline**: `std::shared_ptr<PipelineSnapshot>` (or `std::atomic<std::shared_ptr<...>>` in C++20) so that:
  - **Readers** (frame path): load the current pointer, use the snapshot, no mutex.
  - **Writer** (swap): exchange in one step so there is no “half-updated” state.
- **Persistent output leg**: created once per instance; no swap. Only the **input** to the proxy (who feeds it) changes (router vs last-frame pump).
- **Config / state**: existing `state_mutex_` / `active_pipeline_mutex_` for “running”, “building”, etc., as today; swap itself should be a single atomic pointer write to minimize contention.

---

## 4. Key C++ Classes

### 4.1 PersistentOutputLeg (new)

Owns the RTMP output side that never tears down during swap.

```cpp
class PersistentOutputLeg {
public:
  using ProxyPtr = std::shared_ptr<edgeos::RtmpLastFrameFallbackProxyNode>;
  using RtmpDesPtr = std::shared_ptr<cvedix_nodes::cvedix_rtmp_des_node>;

  PersistentOutputLeg(const std::string& instanceId,
                     const std::string& rtmpUrl,
                     const OutputLegParams& params);

  ProxyPtr proxy() const { return proxy_; }
  RtmpDesPtr rtmpDes() const { return rtmp_des_; }

  void injectFrame(const cv::Mat& frame);  // For last-frame pump

private:
  std::string instance_id_;
  ProxyPtr proxy_;
  RtmpDesPtr rtmp_des_;
};
```

- Built once when the **first** pipeline for this instance is created (or when RTMP URL is set).
- Reused for every subsequent pipeline; only the **upstream** (frame router) changes.

### 4.2 FrameRouter (new)

Routes frames from the active pipeline (or last-frame pump) into the persistent proxy.

```cpp
class FrameRouter {
public:
  using PipelineSnapshotPtr = std::shared_ptr<PipelineSnapshot>;

  explicit FrameRouter(std::shared_ptr<PersistentOutputLeg> outputLeg);

  void setActivePipeline(PipelineSnapshotPtr snapshot);
  PipelineSnapshotPtr getActivePipeline() const;

  // Called by the terminal node of the active pipeline (or by last-frame pump)
  void submitFrame(const cv::Mat& frame, const FrameMetadata& meta);

  void setLastFramePumpActive(bool active);
  void setLastFrame(const cv::Mat& frame);

private:
  std::shared_ptr<PersistentOutputLeg> output_leg_;
  std::atomic<std::shared_ptr<PipelineSnapshot>> active_pipeline_;
  std::atomic<bool> last_frame_pump_active_{false};
  std::shared_ptr<cv::Mat> last_frame_;
  std::mutex last_frame_mutex_;
};
```

- **submitFrame**: if `last_frame_pump_active_` is false, forward to `output_leg_->proxy()` (and update last_frame_ for pump use). If true, pump owns output; submitFrame can no-op or still update last_frame_.
- **getActivePipeline**: lock-free read for the pipeline worker path.

### 4.3 PipelineSnapshot (existing, extended usage)

- Snapshot contains **only** the graph from **source** up to the **OSD** (or terminal node that would normally connect to rtmp_des).
- That terminal node is **not** connected to rtmp_des; it is connected to the **FrameRouter** (or a small “router sink” node that calls `FrameRouter::submitFrame`).
- So: **PipelineSnapshot** = source → … → OSD → [router sink]. No rtmp_des inside the snapshot.

### 4.4 WorkerHandler (existing, extended)

- Holds:
  - `std::shared_ptr<PersistentOutputLeg> output_leg_` (created when first pipeline with RTMP is built).
  - `std::unique_ptr<FrameRouter> frame_router_`.
  - Active pipeline: still `PipelineSnapshotPtr active_pipeline_` but now “AI leg only”; swap is done via `frame_router_->setActivePipeline(newSnapshot)`.
- Hot-swap sequence (see below): build new pipeline (with router sink), warm-up, atomic swap via router, stop old source, drain old snapshot, destroy.

---

## 5. Pipeline Lifecycle Management

### 5.1 States

- **Stopped**: No active pipeline; output leg may or may not exist (lazy creation on first start with RTMP).
- **Running**: One active pipeline; frame router forwards its output to the proxy.
- **Swapping**: New pipeline built and warming; optional last-frame pump active; then atomic swap, then drain old.

### 5.2 Lifecycle Transitions

1. **Start (no pipeline)**  
   Build pipeline (with router sink → same proxy), create output leg if needed, set active, start source.

2. **Start (already running)**  
   No-op or restart depending on policy.

3. **Hot update (no rebuild)**  
   Apply runtime config (e.g. `set_lines()`); no pipeline swap.

4. **Hot update (rebuild required)**  
   - Enter **Swapping**.
   - Optionally start last-frame pump (inject last frame into proxy).
   - Build new pipeline (same RTMP URL → reuse `output_leg_`; new graph’s terminal node → `frame_router_`).
   - Warm-up: run new pipeline briefly or N frames (optional).
   - **Atomic swap**: `frame_router_->setActivePipeline(newSnapshot)`.
   - Stop **old** pipeline’s source; do **not** tear down proxy/rtmp_des.
   - Drain old pipeline (wait for in-flight frames to complete or use SDK drain API).
   - Release old snapshot (destructor runs, `detach_recursively()`).
   - Stop last-frame pump if used.
   - Exit **Swapping** → **Running**.

5. **Stop**  
   Stop source, drain, clear active pipeline, optionally tear down output leg (or keep for next start).

---

## 6. Example Pseudo-Code: Atomic Pipeline Swap

```cpp
bool WorkerHandler::hotSwapPipeline(const Json::Value& newConfig) {
  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    return buildPipeline();
  }

  Json::Value mergedConfig = mergeConfig(config_, newConfig);

  // 1) Capture last frame for pump (optional)
  std::shared_ptr<cv::Mat> lastFrame = getLastFrameCopy();

  // 2) Start last-frame pump so RTMP never sees gap (optional but recommended)
  if (output_leg_ && lastFrame && !lastFrame->empty()) {
    frame_router_->setLastFrame(*lastFrame);
    frame_router_->setLastFramePumpActive(true);
    startLastFramePumpThread();
  }

  // 3) Build new pipeline (reuse output_leg_; new graph feeds frame_router_)
  std::vector<NodePtr> newNodes = pipeline_builder_->buildPipelineAIOnly(
      solution, mergedConfig, instance_id_, frame_router_);
  if (newNodes.empty()) {
    stopLastFramePumpThread();
    frame_router_->setLastFramePumpActive(false);
    return false;
  }

  PipelineSnapshotPtr newSnapshot = std::make_shared<PipelineSnapshot>(std::move(newNodes));

  // 4) Warm-up (optional): run a few frames through new pipeline to prime caches
  warmUpPipeline(newSnapshot);

  // 5) Atomic swap: readers immediately see new pipeline
  PipelineSnapshotPtr oldSnapshot = frame_router_->setActivePipeline(newSnapshot);

  // 6) Stop old pipeline source (no more new frames into old graph)
  stopSourceNodeForSnapshot(oldSnapshot);

  // 7) Drain old pipeline (wait for in-flight frames to complete)
  drainPipelineSnapshot(oldSnapshot);

  // 8) Release old snapshot (destructor detaches nodes)
  oldSnapshot.reset();

  // 9) Start new pipeline source
  if (!startSourceNodeForSnapshot(newSnapshot)) {
    // Rollback possible: setActivePipeline(oldSnapshot) if kept
    stopLastFramePumpThread();
    frame_router_->setLastFramePumpActive(false);
    return false;
  }

  // 10) Stop last-frame pump; frame_router_ now forwards from new pipeline
  stopLastFramePumpThread();
  frame_router_->setLastFramePumpActive(false);

  config_ = mergedConfig;
  setupFrameCaptureHook();
  setupQueueSizeTrackingHook();
  return true;
}
```

**Atomic swap** (inside `FrameRouter`):

```cpp
PipelineSnapshotPtr FrameRouter::setActivePipeline(PipelineSnapshotPtr newSnapshot) {
  return active_pipeline_.exchange(std::move(newSnapshot));
}
```

(C++20: `std::atomic<std::shared_ptr<T>>`; in C++17, use `std::shared_mutex` with a single pointer and brief exclusive lock for the swap, or a lock-free structure if available.)

---

## 7. Strategies to Prevent Race Conditions

### 7.1 Single Writer for Swap

- Only one context (e.g. IPC handler that processes UPDATE) performs the swap.
- Serialize hot-swap requests (e.g. queue or mutex) so two PATCH requests do not overlap.

### 7.2 Atomic Pointer Only

- The “active pipeline” is switched with one atomic exchange (or one short critical section).
- No “set active to null then set to new” in two steps visible to readers.

### 7.3 Readers Only Hold shared_ptr

- Frame path: `auto p = frame_router_->getActivePipeline(); if (p) { ... push frame from p's terminal node ... }`.
- Even if a swap happens mid-flight, the old snapshot remains valid until the last reader releases it; no use-after-free.

### 7.4 Drain Before Destroy

- After swapping, do not destroy the old snapshot until its source is stopped and drain is complete.
- Prevents: frames in flight in the old graph while nodes are being torn down.

### 7.5 Last-Frame Pump Only During Swap

- Pump is on only between “start pump” and “swap done + new source started”.
- Avoids two writers (pump and new pipeline) writing to the proxy at the same time by design (pump off when new pipeline is active).

### 7.6 No Double Start of Source

- Old source is stopped before new source is started (or immediately after swap), and only one snapshot is “active” at a time, so only one source feeds the router.

---

## 8. Frame Router Implementation

### 8.1 Responsibilities

- Hold the **active pipeline** pointer (atomic or mutex-protected).
- Expose **submitFrame(frame, meta)** to:
  - The **terminal node** of the active pipeline (sink node that receives OSD output and calls `submitFrame`).
  - Optionally the **last-frame pump** (same entry point with a “pump” flag or a separate `injectFromPump` that bypasses “active” and writes to proxy + updates last_frame_).
- Forward frames to `PersistentOutputLeg::proxy()` (which then forwards to rtmp_des).

### 8.2 Sink Node for Pipeline

The pipeline builder, when building the “AI-only” leg, must end with a node that:

- Receives the OSD output (same as today’s connection to the RTMP proxy).
- Instead of connecting to a node that owns rtmp_des, it calls:

  `frame_router_->submitFrame(meta->osd_frame, meta);`

So the **graph** ends at this “router sink” node; the router sink is the only link between the swappable pipeline and the persistent output leg.

### 8.3 Submit Logic (simplified)

```cpp
void FrameRouter::submitFrame(const cv::Mat& frame, const FrameMetadata& meta) {
  if (frame.empty()) return;

  if (last_frame_pump_active_.load()) {
    std::lock_guard<std::mutex> lock(last_frame_mutex_);
    last_frame_ = std::make_shared<cv::Mat>(frame.clone());
    return;  // Pump is driving output; do not double-write
  }

  {
    std::lock_guard<std::mutex> lock(last_frame_mutex_);
    last_frame_ = std::make_shared<cv::Mat>(frame.clone());
  }
  output_leg_->injectFrame(frame);  // Forward to proxy → rtmp_des
}
```

(Adjust as needed: e.g. pump might call `injectFrame` directly and only update `last_frame_` from the pipeline when pump is off.)

### 8.4 Minimizing Drops (0–2 frames)

- **Atomic swap** so there is no long lock on the frame path.
- **Warm-up** so the new pipeline’s caches and first-frame costs are paid before swap.
- **Drain** old pipeline so no frame is lost in the old graph.
- **Last-frame pump** so the RTMP stream never has an empty interval.
- **Single producer**: after swap, only the new pipeline’s terminal node calls `submitFrame`; no contention with the pump after pump is stopped.

---

## 9. Memory Safety Considerations

### 9.1 Ownership

- **PersistentOutputLeg**: owned by `WorkerHandler`; lives across swaps; destroyed when instance stops or is deleted.
- **FrameRouter**: owned by `WorkerHandler`; holds `shared_ptr` to `PersistentOutputLeg` and `atomic shared_ptr` to active `PipelineSnapshot`.
- **PipelineSnapshot**: refcounted; after swap, only the drain logic and any in-flight frame callbacks hold the old snapshot; when they release, destructor runs and `detach_recursively()` cleans the graph.

### 9.2 No Raw Pointers to Snapshot

- All references to the active pipeline go through `shared_ptr` (or atomic shared_ptr). No raw pointer to the snapshot that could outlive the snapshot.

### 9.3 Drain Before Reset

- `oldSnapshot.reset()` only after `stopSourceNodeForSnapshot` and `drainPipelineSnapshot` so no thread is still inside the old graph when nodes are detached.

### 9.4 Last-Frame Storage

- `last_frame_` in router and in proxy: use `shared_ptr<cv::Mat>` or mutex-protected `cv::Mat` to avoid races between pump and pipeline thread.

### 9.5 Exception Safety

- Build new pipeline in a separate vector/snapshot; only after success call `setActivePipeline`. On failure, release new snapshot and leave current pipeline active.

---

## 10. Logging and Observability

### 10.1 Recommended Log Points

- **Swap start**: instance_id, reason (config change), timestamp.
- **Build new pipeline**: start/end, duration, node count, success/failure.
- **Warm-up**: start/end, duration, frames used (if any).
- **Atomic swap**: timestamp; old snapshot refcount (if available) for debugging.
- **Old source stop**: instance_id, snapshot id (e.g. address or generation).
- **Drain start/end**: duration, any timeout.
- **Old snapshot destroyed**: after reset.
- **New source start**: success/failure.
- **Last-frame pump**: start/stop, duration, frame count injected (optional).
- **Swap end**: total duration, success/failure.

### 10.2 Metrics (if you have a metrics system)

- `pipeline_swap_total` (counter, labels: instance_id, status=ok|fail).
- `pipeline_swap_duration_seconds` (histogram).
- `pipeline_swap_build_duration_seconds` (histogram).
- `pipeline_swap_drain_duration_seconds` (histogram).
- `frames_dropped_during_swap` (counter or gauge; target 0–2).

### 10.3 Health and Alerts

- If swap fails: set `last_error_`, keep old pipeline running if possible, alert.
- If drain times out: log and optionally force-release old snapshot; consider alert (possible leak or stuck node).

### 10.4 Trace ID

- For each PATCH/PUT that triggers a swap, attach a trace_id (or request_id) to all log lines for that swap so you can reconstruct the full sequence in logs.

---

## 11. Summary Table

| Requirement | How It Is Met |
|-------------|----------------|
| RTMP output and encoder stay alive | Persistent output leg (proxy + rtmp_des) never torn down during swap. |
| New pipelines built and warmed before activation | preBuildPipeline / buildPipelineAIOnly + optional warmUpPipeline before setActivePipeline. |
| Atomic pointer swap | FrameRouter::setActivePipeline uses single atomic exchange (or short critical section). |
| Old pipelines drain before destruction | stopSourceNodeForSnapshot → drainPipelineSnapshot → oldSnapshot.reset(). |
| No RTMP reconnects | Single rtmp_des connection; only the upstream (AI leg) is swapped. |
| Minimize frame drops (0–2) | Atomic swap, warm-up, drain, last-frame pump, single producer. |

This design extends the current codebase (WorkerHandler, PipelineSnapshot, RtmpLastFrameFallbackProxyNode) with a clear separation between **swappable AI leg** and **persistent output leg**, and a **frame router** plus **atomic pipeline pointer** to achieve zero-downtime hot updates without RTMP reconnects.

---

## 12. RTMP Destination (rtmp_des) Teardown – TCP FIN

When stopping or tearing down **rtmp_des** (e.g. instance stop, pipeline swap teardown, or `PersistentOutputLeg` destruction), the RTMP TCP connection must be closed in a way that sends **TCP FIN** so the server (e.g. nginx-rtmp, ZLMediaKit) releases the stream key and does not leave the connection in half-open state.

**Requirement (SDK / cvedix_rtmp_des_node):**

- On stop or destruct, the node must:
  1. `shutdown(socket_fd, SHUT_RDWR);`
  2. `close(socket_fd);`
- Doing only `close()` can leave the connection in a state where the server does not see a clean FIN, which can delay stream key release or cause "connection in use" on reconnect.

This applies to any code path that disposes of rtmp_des: in-process instance stop (`InstanceRegistry`), worker pipeline teardown (`WorkerHandler`), and persistent output leg destruction (`PersistentOutputLeg::~PersistentOutputLeg()`). The actual socket is owned by the cvedix rtmp_des node; the SDK must implement the above in its stop/teardown (or destructor).

**Workaround when the SDK cannot be changed:** use the LD_PRELOAD wrapper in `support/rtmp_fin_wrapper/`. It overrides `close()` and, for TCP socket fds, calls `shutdown(fd, SHUT_RDWR)` before the real `close()`. Build yields `build/lib/libclose_fin.so`; run e.g. `LD_PRELOAD=/path/to/libclose_fin.so ./bin/omniapi` (or only the worker). See `support/rtmp_fin_wrapper/README.md`.

**Zero-downtime path and EDGE_AI_HOTSWAP_DELAY_SEC:** The test delay is applied **after** the new pipeline has started (not before). A delay before start caused several seconds of only last-frame pump; some RTMP servers close the connection in that situation, so output was lost. With delay after start, the new pipeline is already sending real frames and the stream stays up.

**Pump overlap:** After starting the new pipeline source, the last-frame pump is kept running for **500 ms** before it is stopped. That overlap gives the new pipeline time to produce its first frame (source connect, decode, etc.). Without it, there can be a gap where neither the pump nor the new pipeline sends, so the stream stalls or the server closes the connection. With the overlap, output stays continuous.

**Log visibility:** By default the CVEDIX SDK log level is **WARN** so worker logs (e.g. "Zero-downtime pipeline swap", "Hot-swap: preserved RTMP output") remain visible. If SDK logs still flood, set `CVEDIX_LOG_LEVEL=ERROR`. Use `EDGE_AI_VERBOSE=1` only when you need detailed PipelineBuilder/getRTMPUrl logs. See [ENVIRONMENT_VARIABLES.md](ENVIRONMENT_VARIABLES.md).

**PATCH with only CrossingLines (line-only update):** When the request body contains only `AdditionalParams.CrossingLines` (and no other structural or param changes), the worker treats it as a **line-only** update. **When the instance has RTMP output (persistent output leg / frame_router_), the worker uses hot-swap instead of `set_lines()`** so that: (1) the new pipeline is built with the new CrossingLines from config, (2) the RTMP stream stays connected via the persistent output leg, and (3) IPC timeout is avoided (set_lines() can block >10s). When there is no RTMP output, the worker applies lines via `set_lines()` at runtime and does not perform a hot-swap. The worker syncs `AdditionalParams` ↔ `additionalParams` so that a PATCH sent with PascalCase `AdditionalParams` only still updates both representations and is correctly detected as line-only. If you use set_lines() path (no RTMP), consider `IPC_START_STOP_TIMEOUT_MS=30000` so the API does not time out while set_lines() runs. Test script: `scripts/test_zero_downtime_patch_lines.sh`.

**OSD line overwrite (fixed):** When building the BA crossline OSD node, the pipeline builder first sets line config from `CrossingLines` (multi-line), then had a fallback for legacy `CROSSLINE_START_X/Y`, `CROSSLINE_END_X/Y`. If both were present (e.g. config merged from solution + PATCH), the fallback ran after CrossingLines and overwrote the OSD with a single line, so only one line was displayed. The fix: run the legacy CROSSLINE_* fallback only when no line config was set from CrossingLines (`!osdLinesSetFromCrossingLines`). See `pipeline_builder_behavior_analysis_nodes.cpp` `createBACrosslineOSDNode`.
