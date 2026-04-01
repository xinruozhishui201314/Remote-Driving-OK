# Project Plan: High-Performance Teleoperation System

> **Context for Cursor**: 
> You are an expert Full-Stack Engineer specializing in WebRTC, Real-time Systems, and IoT. 
> We are building a Teleoperation System with strict latency requirements (<200ms G2G).
> **Tech Stack**: ZLMediaKit (Media Server), Keycloak (Auth), Go/Rust (Control Gateway), React/Electron (Client), C++/Python (Vehicle Agent).
> **Protocol**: WebRTC (WHIP/WHEP), DataChannel (SCTP), OIDC.

---

## 1. System Goals & Acceptance Criteria

### 1.1 Core Experience
- **Video Latency**: Glass-to-Glass (G2G) P50 ≤ 200ms.
- **Control Latency**: Round-Trip-Time (RTT) P95 ≤ 100ms.
- **Input**: Support Keyboard (fallback) and Logitech Steering Wheel (G29/G923) with Force Feedback (FFB).
- **Safety**: Vehicle must autonomously brake if control stream is lost > 300ms.

### 1.2 Architecture Overview
1.  **Vehicle Agent**: Hard-encodes video (H.264, No-B-frames), pushes WHIP to ZLM. Listens to Control Gateway.
2.  **Media Server (ZLM)**: ZLMediaKit configured for low latency (TWCC enabled).
3.  **Control Gateway**: A lightweight relay service (Go/Rust) forwarding DataChannel packets between Client and Vehicle.
4.  **Client**:
    - **Web (Lite)**: React, Gamepad API (Input only), WHEP Player.
    - **Electron (Pro)**: Node-addon for Logitech SDK (Input + Force Feedback).
5.  **Auth**: Keycloak for identity and session tokens.

---

## 2. Technical Standards & Constraints (Strict)

### 2.1 Video Pipeline (Low Latency)
- **Encoder**: H.264 Baseline or Main (MUST set `-bf 0` to disable B-frames).
- **GOP**: 1 second (Frame-loss recovery vs bandwidth trade-off).
- **WebRTC Params**: 
    - ZLM Config: `[rtc] rembBitRate=0` (Forces TWCC).
    - Client Jitter Buffer: Set `RTCRtpReceiver.jitterBufferTarget = 0` (or <50ms) where supported.
- **Rendering**: Use `requestVideoFrameCallback` for accurate latency metrics.

### 2.2 Control Pipeline (Reliability vs Latency)
- **Transport**: WebRTC DataChannel.
- **Config**: `ordered: false`, `maxRetransmits: 0` (UDP-like behavior).
- **Priority**: 
    - Steering/Gas/Brake: High frequency, drop old packets.
    - E-STOP: High priority, sent redundantly (3x).

### 2.3 Input System
- **Web**: Use standard W3C Gamepad API (Polling rate > 60Hz).
- **Desktop (Electron)**: Use Logitech Steering Wheel SDK (via `node-ffi` or C++ addon) for Force Feedback (Spring, Damper, Collision effects).

---

## 3. Implementation Roadmap (Task Breakdown)

### Phase 1: Infrastructure & Auth
- [ ] **Infra**: Deploy ZLMediaKit, Keycloak, Postgres, Redis via Docker Compose.
- [ ] **Auth**: Configure Keycloak Realm `teleop`. Create Clients for `web-app` and `vehicle-agent`.
- [ ] **Backend**: Implement `POST /session` to lock a VIN and return Session Tokens.
- [ ] **Time Sync**: Implement NTP-like logic in Backend/Gateway to sync server time with clients.

### Phase 2: Media Pipeline (The "Eye")
- [ ] **Vehicle**: Implement FFmpeg/GStreamer pipeline pushing to ZLM via WHIP (`http://zlm/index/api/whip`).
    - *Constraint*: Ensure hardware acceleration (NVENC/VAAPI/OMEX) is active.
- [ ] **Client**: Implement WHEP Player using standard `RTCPeerConnection`.
    - *Metric*: Visualize `inbound-rtp` stats (rtt, jitterBufferDelay).
- [ ] **Optimization**: Add specific logic to handle browser `jitterBufferTarget`.

### Phase 3: Control Pipeline (The "Hand")
- [ ] **Gateway**: Build a Signaling Server that bridges DataChannels.
    - *Logic*: Authenticate Client & Vehicle, then route packets based on Session ID.
- [ ] **Vehicle**: Implement `SafetySupervisor` (Watchdog). 
    - *Rule*: If `last_packet_time` > 300ms, trigger `E-STOP`.
- [ ] **Protocol**: Define Protocol Buffers for Control Commands (`{ seq, ts, steer, gas, brake, checksum }`).

### Phase 4: Input Integration (The "Touch")
- [ ] **Web Input**: Implement `GamepadProvider` abstraction.
    - *Task*: Map axes (-1 to 1) to vehicle values (steer angle, throttle %). Handle dead-zones.
- [ ] **Electron Input**: Integrate Logitech SDK.
    - *Task*: Read wheel angle (DirectInput).
    - *Task*: Implement `setConstantForce(magnitude)` based on vehicle speed/resistance data.
- [ ] **Keyboard**: Implement "Virtual Joystick" with decay/ramp-up smoothing (don't send raw keypresses).

### Phase 5: Latency Governance & UI
- [ ] **Clock Sync**: Implement P2P time-sync (Client <-> Vehicle) over DataChannel to calculate accurate `G2G` latency.
- [ ] **UI**: Build "Engineering Dashboard" (Overlay).
    - Graphs: RTT, Jitter Buffer, Frame Loss, Control Frequency.
- [ ] **Long-tail**: Implement "Simulcast Switching".
    - *Logic*: If `packetLoss` > 5% or `rtt` > 300ms, switch WHEP URL to Low-Res stream automatically.

---

## 4. Troubleshooting & Fault Codes

### 4.1 Platform Fault Codes
- `MEDIA_ICE_FAIL`: WebRTC connection failed (Check STUN/TURN).
- `CTRL_LAG_HIGH`: Control RTT > 150ms (Show Yellow Alert).
- `INPUT_DISCONNECT`: Gamepad/Wheel USB lost.

### 4.2 Vehicle DTC (Diagnostic Trouble Codes)
- Standardize on `SAE J2012` format (e.g., `P0xxx`).
- Vehicle Agent must push DTCs via DataChannel immediately when active.

---

## 5. Development Tips for Cursor

- **When coding Video**: Always check for `rembBitRate` config and H.264 Profile constraints.
- **When coding Input**: Abstract the input source. The vehicle shouldn't care if commands come from a Keyboard or a G29 Wheel.
- **When coding Safety**: The Vehicle Agent's safety loop must run independently of the network thread.
