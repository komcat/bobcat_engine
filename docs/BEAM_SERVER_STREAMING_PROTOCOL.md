# Beam Server Streaming Protocol Extension

> Protocol for continuous 10Hz image streaming from the GDI camera window to the Beam Analyser server.
> The client (bobcat_engine) pushes camera frames; the server detects beams and returns results per frame.

## Context

This extends the existing [Beam Server Protocol](BEAM_SERVER_PROTOCOL.md) with three new commands for live streaming. The existing commands (0x01–0x05) remain unchanged.

### Why New Commands?

The existing `CMD_DETECT (0x01)` repeats full JSON metadata + pixel format negotiation on every call. For 10Hz streaming:

- Static metadata (image size, pixel scale, rotation) is sent **once** in `STREAM_START`
- Each frame only carries a sequence number + raw pixels — **no JSON header per frame**
- The server knows it's in streaming mode and can optimize (pre-allocate buffers, update live UI)
- Streaming frames are **not accumulated** for Fit — they are detection-only for live display

---

## New Command IDs

| ID | Hex | Name | Description |
|----|-----|------|-------------|
| 6 | `0x06` | `CMD_STREAM_START` | Enter streaming mode with camera parameters |
| 7 | `0x07` | `CMD_STREAM_FRAME` | Send one frame for live detection |
| 8 | `0x08` | `CMD_STREAM_STOP` | Exit streaming mode |

These use the **same wire format** as existing commands:

```
Request:  [4B cmd_id LE][4B payload_len LE][payload]
Response: [4B json_len LE][json_utf8]
```

---

## CMD_STREAM_START (0x06)

Tells the server to enter streaming mode. Sends all static parameters once so they don't need to be repeated per frame.

### Request Payload (JSON)

```json
{
  "width": 1936,
  "height": 1216,
  "pixel_format": "mono8",
  "rotate_180": false,
  "um_per_px": [3.45, 3.45],
  "interval_ms": 100
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `width` | int | Yes | Image width in pixels (fixed for entire stream session) |
| `height` | int | Yes | Image height in pixels |
| `pixel_format` | string | Yes | `"mono8"` (uint8 grayscale) or `"mono16"` (uint16 grayscale) |
| `rotate_180` | bool | Yes | Whether to rotate each frame 180° before processing |
| `um_per_px` | [float, float] | Yes | Pixel scale [X, Y] in micrometers per pixel |
| `interval_ms` | int | No | Hint to server: expected interval between frames in ms (default 100) |

**Pixel format notes:**
- `"mono8"` — each pixel is 1 byte (uint8). Raw data size = `width × height` bytes. This is the native format from the GDI camera window (RGB converted to grayscale).
- `"mono16"` — each pixel is 2 bytes (uint16 little-endian). Raw data size = `width × height × 2` bytes. Same as existing CMD_DETECT format.

### Response

```json
{ "status": "ok" }
```

Or on error (e.g., already streaming, invalid parameters):

```json
{ "error": "already in streaming mode" }
```

### Server Behavior

On receiving STREAM_START, the server should:
1. Store the stream parameters (width, height, pixel_format, rotate_180, um_per_px)
2. Pre-allocate image buffer: `width × height × bytes_per_pixel`
3. Enter streaming state — ready to receive STREAM_FRAME commands
4. Update the server UI to show "Streaming..." status

---

## CMD_STREAM_FRAME (0x07)

Sends a single camera frame for live beam detection. This is the hot path — sent every ~100ms.

### Request Payload (Binary)

```
┌────────────────────────┬───────────────────────────────┐
│ Sequence Number (4B)   │ Raw Pixel Data (N bytes)      │
│ uint32 LE              │ mono8 or mono16 LE            │
└────────────────────────┴───────────────────────────────┘
```

| Field | Type | Size | Description |
|-------|------|------|-------------|
| Sequence Number | uint32 LE | 4 bytes | Monotonically increasing frame counter (0, 1, 2, ...) |
| Raw Pixel Data | uint8[] or uint16[] LE | `W×H×bpp` bytes | Grayscale pixel data, row-major, top-to-bottom |

**Payload size** = 4 + (width × height × bytes_per_pixel)

- For `mono8` with 1936×1216: 4 + 2,354,176 = **~2.25 MB per frame**
- For `mono16` with 1936×1216: 4 + 4,708,352 = **~4.49 MB per frame**

**No JSON header** — all metadata was established in STREAM_START.

### Response

```json
{
  "seq": 42,
  "num_beams": 2,
  "beams": [
    {
      "side": "TX",
      "centroid_x_px": 1141.21,
      "centroid_y_px": 506.70,
      "centroid_x_um": 285.30,
      "centroid_y_um": 126.68,
      "d4sigma_x_um": 13.51,
      "d4sigma_y_um": 17.73
    },
    {
      "side": "RX",
      "centroid_x_px": 1135.45,
      "centroid_y_px": 596.01,
      "centroid_x_um": 283.86,
      "centroid_y_um": 149.00,
      "d4sigma_x_um": 36.87,
      "d4sigma_y_um": 6.87
    }
  ],
  "process_ms": 23.5
}
```

| Field | Type | Description |
|-------|------|-------------|
| `seq` | int | Echo of the sequence number from the request (for client to match responses) |
| `num_beams` | int | Number of beams detected (0 if none found) |
| `beams` | array | Same beam result format as CMD_DETECT |
| `beams[].side` | string | `"TX"` or `"RX"` |
| `beams[].centroid_x_px` | float | Centroid X in pixels |
| `beams[].centroid_y_px` | float | Centroid Y in pixels |
| `beams[].centroid_x_um` | float | Centroid X in micrometers |
| `beams[].centroid_y_um` | float | Centroid Y in micrometers |
| `beams[].d4sigma_x_um` | float | D4Sigma beam width X in µm |
| `beams[].d4sigma_y_um` | float | D4Sigma beam width Y in µm |
| `process_ms` | float | Server-side processing time in milliseconds (for performance monitoring) |

**When no beams detected:**

```json
{
  "seq": 42,
  "num_beams": 0,
  "beams": [],
  "process_ms": 8.1
}
```

**On error:**

```json
{
  "seq": 42,
  "error": "image dimensions don't match stream config"
}
```

### Important: This is Still Request-Response

Each STREAM_FRAME is a synchronous request-response. The client sends a frame, then **waits for the response** before sending the next frame. This keeps the protocol simple and prevents buffer overrun.

At 10Hz with ~20-50ms server processing time, there's plenty of headroom:
```
100ms interval - 50ms processing = 50ms idle (margin)
```

If the server takes longer than the interval, the client simply skips that tick and sends the next frame — the effective rate drops gracefully.

---

## CMD_STREAM_STOP (0x08)

Exit streaming mode.

### Request Payload

**Empty** — Payload Length = 0.

### Response

```json
{
  "status": "ok",
  "total_frames": 523,
  "avg_process_ms": 31.2
}
```

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | `"ok"` |
| `total_frames` | int | Total frames processed during this stream session |
| `avg_process_ms` | float | Average processing time per frame in ms |

### Server Behavior

On receiving STREAM_STOP, the server should:
1. Exit streaming state
2. Free pre-allocated streaming buffers
3. Update server UI (show summary, return to idle state)
4. Be ready to accept any command again (including a new STREAM_START)

---

## Sequence Diagram

```
GDI Camera Window (Client)                    Beam Analyser Server
       │                                              │
       │──── Connect (TCP to port 5555) ─────────────>│
       │                                              │
       │──── CMD_STREAM_START (0x06) ────────────────>│  width=1936, height=1216
       │     {"width":1936,"height":1216,             │  pixel_format=mono8
       │      "pixel_format":"mono8",                 │  um_per_px=[3.45,3.45]
       │      "rotate_180":false,                     │
       │      "um_per_px":[3.45,3.45]}                │
       │<─── {"status":"ok"} ────────────────────────-│
       │                                              │
       │  ┌─── Streaming Loop (every ~100ms) ───┐    │
       │  │                                      │    │
       │──┤─ CMD_STREAM_FRAME (0x07) ───────────>│    │  seq=0, raw pixels
       │  │  [seq=0][2.25 MB pixel data]         │    │
       │<─┤─ {"seq":0,"num_beams":2,...} ────────│    │  detect result
       │  │                                      │    │
       │  │  [100ms later]                       │    │
       │  │                                      │    │
       │──┤─ CMD_STREAM_FRAME (0x07) ───────────>│    │  seq=1, raw pixels
       │  │  [seq=1][2.25 MB pixel data]         │    │
       │<─┤─ {"seq":1,"num_beams":2,...} ────────│    │  detect result
       │  │                                      │    │
       │  │  ... continues at 10Hz ...           │    │
       │  │                                      │    │
       │  └──────────────────────────────────────┘    │
       │                                              │
       │──── CMD_STREAM_STOP (0x08) ─────────────────>│
       │<─── {"status":"ok","total_frames":523} ─────-│
       │                                              │
       │──── Disconnect ─────────────────────────────>│
```

---

## Frame Data Preparation (Client Side)

The GDI camera window receives frames as **RGB 3-channel uint8** via `CameraFrameData::imageData`. Before sending, the client converts to grayscale:

```
For mono8:
  gray[i] = (R[i] + G[i] + B[i]) / 3     // or weighted: 0.299R + 0.587G + 0.114B

For mono16:
  gray[i] = ((R[i] + G[i] + B[i]) / 3) * 256   // upscale 8-bit to 16-bit
```

The pixel data is sent **row-major, top-to-bottom** (standard image order, NOT the vertically-flipped BGR format used for GDI rendering).

---

## Bandwidth Estimate

| Pixel Format | Resolution | Frame Size | At 10Hz | At 30Hz |
|---|---|---|---|---|
| mono8 | 1936×1216 | 2.25 MB | 22.5 MB/s | 67.5 MB/s |
| mono8 | 1024×768 | 0.75 MB | 7.5 MB/s | 22.5 MB/s |
| mono16 | 1936×1216 | 4.49 MB | 44.9 MB/s | 134.7 MB/s |
| mono16 | 1024×768 | 1.50 MB | 15.0 MB/s | 45.0 MB/s |

Over localhost TCP, all rates are well within capacity (~1 GB/s typical loopback throughput).

---

## Server Implementation Checklist

Your beam analyser server UI needs to handle:

### 1. State Machine

```
     ┌─────────┐    STREAM_START    ┌───────────┐
     │  IDLE   │ ─────────────────> │ STREAMING  │
     │         │ <───────────────── │            │
     └─────────┘    STREAM_STOP     └───────────┘
         │                               │
         │  CMD_DETECT/FIT/RESET/SAVE    │  CMD_STREAM_FRAME only
         │  (all accepted)               │  (other cmds rejected with error)
         │                               │
```

While in STREAMING state:
- Only `CMD_STREAM_FRAME (0x07)` and `CMD_STREAM_STOP (0x08)` are accepted
- All other commands (0x01–0x05) should return `{"error": "busy: streaming in progress"}`

### 2. Buffer Pre-allocation

On STREAM_START, pre-allocate:
```python
bpp = 1 if pixel_format == "mono8" else 2
buffer = bytearray(width * height * bpp)
```

### 3. Frame Processing Pipeline

For each STREAM_FRAME:
```
1. Read 4-byte sequence number
2. Read remaining bytes into pre-allocated buffer
3. Reshape to (height, width) numpy array (uint8 or uint16)
4. If rotate_180: np.rot90(img, 2)
5. Run D4Sigma beam detection
6. Build JSON response with seq echo + beam results
7. Send response frame
```

### 4. Server UI Updates

During streaming, update the UI at a reasonable rate (e.g., every frame or throttled to ~5Hz):
- Show live beam overlay on the received image
- Display real-time beam metrics (centroid, d4sigma)
- Show frame rate and processing latency
- Show stream status ("Streaming: frame 142, 10.0 fps, 28ms avg")

### 5. Graceful Handling

- If the TCP connection drops during streaming, clean up streaming state
- If STREAM_START is received while already streaming, return error (don't silently restart)
- If a non-streaming command arrives during streaming, return error with a clear message

---

## Mixing Streaming with Existing Commands

Streaming is for **live monitoring only** — frames are NOT accumulated for Fit.

A typical workflow that combines both:

```
1. Connect
2. STREAM_START          ← live view begins, user sees beam on server UI
3. STREAM_FRAME ×N       ← 10Hz live detection for alignment
4. STREAM_STOP           ← user satisfied with alignment
5. CMD_RESET             ← prepare for z-scan
6. CMD_DETECT ×9         ← normal z-scan with accumulation
7. CMD_FIT               ← compute M², waist, poynting
8. CMD_SAVE              ← persist results
9. Disconnect
```

---

## Error Codes (Response)

| Situation | Response |
|-----------|----------|
| STREAM_FRAME when not streaming | `{"error": "not in streaming mode — send STREAM_START first"}` |
| STREAM_START when already streaming | `{"error": "already in streaming mode — send STREAM_STOP first"}` |
| CMD_DETECT/FIT/RESET/SAVE while streaming | `{"error": "busy: streaming in progress — send STREAM_STOP first"}` |
| Frame size mismatch | `{"error": "payload size mismatch: expected 2354180, got 2354000"}` |
| No beams found | `{"seq": N, "num_beams": 0, "beams": [], "process_ms": 8.1}` (NOT an error) |
| Processing failure | `{"seq": N, "error": "detection failed: <reason>"}` |
