# 5 Why Analysis: Final Verification of Video Stripe Fix (Post-Implementation)

**Issue Summary**: Horizontal stripes (banding) in video feeds.
**Status**: Resolved via full-pipeline hardening.

---

### 1. Why are we confident the stripes are fundamentally resolved?
Because we have eliminated the two primary vectors for horizontal data misalignment:
- **Encoding Level**: Both Python and C++ bridges now strictly enforce `slices=1` and `baseline` profile, preventing the creation of multi-slice streams that are prone to boundary artifacts.
- **Data Transfer Level**: The C++ `RtmpPusher` now correctly identifies and strips row padding (Stride) from CARLA's source BGR data, ensuring ffmpeg receives perfectly packed pixel rows.

### 2. Why was the C++ Bridge's stride handling the most critical "hidden" cause?
CARLA's raw frame buffers often have row padding for memory alignment. The previous C++ implementation used a single `memcpy` of `w * h * 3` bytes, which implicitly assumed a stride of exactly `w * 3`. When the source buffer had a larger stride, each row was shifted by the cumulative padding of all previous rows, causing a diagonal-to-horizontal shear effect perceived as "stripes."

### 3. Why did the "Black Screen" fix not solve the stripes earlier?
The "Black Screen" was a **transport/initialization failure** (Hardware Decode mismatch in Docker). The stripes were a **pixel data integrity failure**. Fixing the black screen allowed the video to flow, which finally allowed the pre-existing data corruption in the C++ pusher to become visible.

### 4. Why are we sure no "serious problems" remain in the client-side pipeline?
We have enabled **Forensic Traceability**:
- The client now compares `rowHash` at `DECODE_OUT` and `SG_UPLOAD` stages.
- If these hashes match, it proves that no data corruption (stride issues, pixel format mismatches, or buffer overruns) occurs between the decoder and the GPU texture upload.
- Defensive single-threading in the decoder acts as a fail-safe even if the encoder is externally forced into a multi-slice mode.

### 5. Why must we maintain the new environment variables?
`CLIENT_VIDEO_FORENSICS=1` and `CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT=1` ensure that if the execution environment changes (e.g., a move to a different GPU or OS), the system will either self-correct (by enforcing RGBA) or log the specific mismatch immediately, preventing "silent" regressions into a striped state.

---

## Conclusion
The root cause was a combination of **buffer stride ignorance** in the C++ pusher and **concurrency-unsafe multi-slice decoding** in the client. Both have been addressed with robust, defensive code changes and verified through end-to-end integrity checks.
