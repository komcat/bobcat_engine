# Beam Server Protocol Reference

> Protocol documentation for the TCP beam detection server used by bobcat_engine.
> Use this as a reference when building a new server UI implementation.

## Overview

The beam server uses a simple **binary-framed TCP protocol** for laser beam characterization (ISO 11146 D4Sigma). The bobcat_engine C++ application is the **client**; your server UI is the **server** that listens for connections and processes commands.

```
bobcat_engine (Client)                    Beam Server UI (Server)
┌──────────────────────┐     TCP/IP      ┌──────────────────────┐
│ BeamServerClient     │ ──────────────> │ Listens on port 5555 │
│                      │ <────────────── │                      │
│  Connect()           │                 │  detect_beams()      │
│  SendDetect()        │                 │  curve_fit()         │
│  SendDetectFile()    │                 │  save_results()      │
│  SendFit()           │                 │  reset()             │
│  SendSave()          │                 │                      │
│  SendReset()         │                 │                      │
└──────────────────────┘                 └──────────────────────┘
```

**Default endpoint:** `127.0.0.1:5555`

---

## Wire Format

### Request Frame (Client → Server)

```
┌──────────────────────┬──────────────────────┬──────────────────────┐
│  Command ID (4 bytes)│ Payload Len (4 bytes)│ Payload (N bytes)    │
│  uint32 LE           │ uint32 LE            │ variable             │
└──────────────────────┴──────────────────────┴──────────────────────┘
```

| Field | Type | Description |
|-------|------|-------------|
| Command ID | `uint32_t` little-endian | Which command (see table below) |
| Payload Length | `uint32_t` little-endian | Byte length of payload that follows |
| Payload | `N` bytes | Command-specific data (may be 0 bytes) |

### Response Frame (Server → Client)

```
┌──────────────────────┬──────────────────────┐
│  JSON Len (4 bytes)  │  JSON Data (N bytes) │
│  uint32 LE           │  UTF-8 string        │
└──────────────────────┴──────────────────────┘
```

| Field | Type | Description |
|-------|------|-------------|
| JSON Length | `uint32_t` little-endian | Byte length of JSON response (max 10 MB) |
| JSON Data | UTF-8 JSON string | Response object |

**All responses are JSON objects.** On error, include an `"error"` key:

```json
{ "error": "description of what went wrong" }
```

---

## Command IDs

| ID | Hex | Name | Description |
|----|-----|------|-------------|
| 1 | `0x01` | `CMD_DETECT` | Send raw image for D4Sigma beam detection |
| 2 | `0x02` | `CMD_FIT` | Compute M², beam waist, Poynting angle from accumulated detections |
| 3 | `0x03` | `CMD_RESET` | Clear all accumulated data |
| 4 | `0x04` | `CMD_DETECT_FILE` | Detect beam from an image file path on disk |
| 5 | `0x05` | `CMD_SAVE` | Save accumulated data to disk |

---

## Command Details

### CMD_DETECT (0x01) — Detect beam from raw image

The client captures a camera frame and sends the raw pixel data to the server for D4Sigma analysis.

#### Request Payload

The payload has **two parts**: a JSON header followed by raw pixel data.

```
┌───────────────────────────┬────────────────────────┬────────────────────────┐
│ JSON Header Len (4 bytes) │ JSON Header (N bytes)  │ Raw Pixel Data (M bytes│
│ uint32 LE                 │ UTF-8 JSON             │ uint16[] LE            │
└───────────────────────────┴────────────────────────┴────────────────────────┘
```

**JSON Header:**

```json
{
  "width": 640,
  "height": 480,
  "z_um": 1500.0,
  "rotate_180": false,
  "um_per_px": [0.25, 0.25]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `width` | int | Image width in pixels |
| `height` | int | Image height in pixels |
| `z_um` | float | Z-axis position in micrometers (for z-scan accumulation) |
| `rotate_180` | bool | Whether to rotate the image 180° before processing |
| `um_per_px` | [float, float] | Pixel scale [X, Y] in micrometers per pixel |

**Raw Pixel Data:**

- `width × height` values, each `uint16_t` in little-endian byte order
- Total size: `width * height * 2` bytes
- If the source camera is 8-bit, the client upconverts by `value * 256`
- Multi-channel images are converted to grayscale (channel average) before sending

#### Response — Success

```json
{
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
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `num_beams` | int | Number of beams detected in the image |
| `beams` | array | Array of beam result objects |
| `beams[].side` | string | Beam label — `"TX"` or `"RX"` |
| `beams[].centroid_x_px` | float | Beam centroid X position in pixels |
| `beams[].centroid_y_px` | float | Beam centroid Y position in pixels |
| `beams[].centroid_x_um` | float | Beam centroid X position in micrometers |
| `beams[].centroid_y_um` | float | Beam centroid Y position in micrometers |
| `beams[].d4sigma_x_um` | float | D4Sigma beam width X in micrometers |
| `beams[].d4sigma_y_um` | float | D4Sigma beam width Y in micrometers |

#### Response — Error

```json
{ "error": "no beams detected in image" }
```

---

### CMD_FIT (0x02) — Fit M² beam propagation model

After accumulating multiple Detect calls at different Z positions, this command requests the server to fit the Gaussian M² beam propagation model to all accumulated data.

#### Request Payload

JSON bytes (no binary prefix):

```json
{
  "um_per_px": [0.25, 0.25],
  "wavelength_m": 1.55e-09,
  "name": "optional_label"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `um_per_px` | [float, float] | Yes | Pixel scale [X, Y] in micrometers per pixel |
| `wavelength_m` | float | Yes | Laser wavelength in meters (e.g., `1.55e-09` for 1550 nm) |
| `name` | string | No | Optional label for the dataset (used for save directory naming) |

#### Response — Success

```json
{
  "num_beams": 2,
  "num_z_steps": 9,
  "beams": [
    {
      "beam_loc_x_um": 285.25,
      "beam_loc_y_um": 126.30,
      "waist_x_um": 8.23,
      "waist_y_um": 6.80,
      "centroid_z_um": 1498.56,
      "poynt_x_deg": 0.217,
      "poynt_y_deg": -0.272,
      "m2_x": 2.82,
      "m2_y": 3.40,
      "fit_error_x": 0.0,
      "fit_error_y": 0.0
    },
    {
      "beam_loc_x_um": 283.58,
      "beam_loc_y_um": 176.11,
      "waist_x_um": 6.61,
      "waist_y_um": 6.53,
      "centroid_z_um": 1492.11,
      "poynt_x_deg": 0.946,
      "poynt_y_deg": -0.916,
      "m2_x": 2.38,
      "m2_y": 2.27,
      "fit_error_x": 0.0,
      "fit_error_y": 0.0
    }
  ],
  "tx_rx_vector_x_um": -1.66,
  "tx_rx_vector_y_um": 49.81
}
```

| Field | Type | Description |
|-------|------|-------------|
| `num_beams` | int | Number of beams tracked across z-scan |
| `num_z_steps` | int | Number of z-scan slices used for the fit |
| `beams` | array | Per-beam fit results |
| `beams[].beam_loc_x_um` | float | Beam X location at the waist plane (µm) |
| `beams[].beam_loc_y_um` | float | Beam Y location at the waist plane (µm) |
| `beams[].waist_x_um` | float | Beam waist radius X (µm) — the 1/e² half-width |
| `beams[].waist_y_um` | float | Beam waist radius Y (µm) |
| `beams[].centroid_z_um` | float | Z position of the beam waist (µm) |
| `beams[].poynt_x_deg` | float | Poynting angle X in degrees |
| `beams[].poynt_y_deg` | float | Poynting angle Y in degrees |
| `beams[].m2_x` | float | M² beam quality factor X (1.0 = ideal Gaussian) |
| `beams[].m2_y` | float | M² beam quality factor Y |
| `beams[].fit_error_x` | float | Fit residual error X (0.0 = perfect fit) |
| `beams[].fit_error_y` | float | Fit residual error Y |
| `tx_rx_vector_x_um` | float | TX-to-RX separation vector X component (µm) |
| `tx_rx_vector_y_um` | float | TX-to-RX separation vector Y component (µm) |

---

### CMD_RESET (0x03) — Clear accumulated data

Clears all accumulated detection data on the server, preparing for a new z-scan session.

#### Request Payload

**Empty** — Payload Length = 0, no payload bytes.

#### Response

```json
{ "status": "ok" }
```

---

### CMD_DETECT_FILE (0x04) — Detect beam from file path

Instead of sending raw pixels over the wire, the client sends a file path. The server reads the image from disk (requires shared filesystem).

#### Request Payload

JSON bytes (no binary prefix):

```json
{
  "file_path": "C:/images/scan/zheight1500um_ch0.png",
  "z_um": 1500.0,
  "um_per_px": [0.25, 0.25],
  "rotate_180": true
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `file_path` | string | Yes | Absolute path to image file on disk |
| `z_um` | float | Yes | Z-axis position in micrometers |
| `um_per_px` | [float, float] | Yes | Pixel scale [X, Y] in µm/px |
| `rotate_180` | bool | No | Rotate image 180° before processing (omit to use server default) |

#### Response

Same format as CMD_DETECT response.

---

### CMD_SAVE (0x05) — Save accumulated data to disk

Tells the server to persist all accumulated images and fit results to a folder.

#### Request Payload

JSON bytes (no binary prefix):

```json
{
  "name": "SN_7045_ch0"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | No | Optional folder label / serial number for naming the output directory |

#### Response — Success

```json
{
  "save_dir": "C:/beam_data/2026-02-14_SN_7045_ch0",
  "num_images": 9
}
```

| Field | Type | Description |
|-------|------|-------------|
| `save_dir` | string | Absolute path to saved data directory |
| `num_images` | int | Number of images saved |

---

## Typical Command Sequences

### Live Z-Scan Workflow

```
Client                              Server
  │                                    │
  │──── TCP Connect ──────────────────>│  (socket open to port 5555)
  │                                    │
  │──── CMD_RESET ────────────────────>│  clear previous data
  │<─── {"status":"ok"} ─────────────-│
  │                                    │
  │  [move Z to position 1]           │
  │──── CMD_DETECT (image + z=1490) ──>│  detect beams at z=1490
  │<─── {"num_beams":2, "beams":[...]} │
  │                                    │
  │  [move Z to position 2]           │
  │──── CMD_DETECT (image + z=1500) ──>│  detect beams at z=1500
  │<─── {"num_beams":2, "beams":[...]} │
  │                                    │
  │  ... repeat for N z-positions ...  │
  │                                    │
  │──── CMD_FIT ──────────────────────>│  fit M² model
  │<─── {"num_beams":2, "beams":[...], │
  │      "tx_rx_vector_x_um":...}     │
  │                                    │
  │──── CMD_SAVE ─────────────────────>│  save results
  │<─── {"save_dir":"...", "num":9} ──-│
  │                                    │
  │──── TCP Disconnect ───────────────>│  (socket close)
```

### Offline Folder Processing Workflow

```
Client                              Server
  │                                    │
  │──── TCP Connect ──────────────────>│
  │──── CMD_RESET ────────────────────>│
  │<─── {"status":"ok"} ──────────────-│
  │                                    │
  │  [for each .png in folder]        │
  │──── CMD_DETECT_FILE ──────────────>│  file_path + z_um extracted from filename
  │<─── {"num_beams":2, "beams":[...]} │
  │  [next file, 150ms delay between]  │
  │                                    │
  │──── CMD_FIT ──────────────────────>│
  │<─── {fit results} ────────────────-│
  │                                    │
  │──── CMD_SAVE (name="SN_7045") ───>│
  │<─── {"save_dir":"..."} ───────────-│
  │                                    │
  │──── TCP Disconnect ───────────────>│
```

---

## Server Implementation Checklist

Your new server UI must implement:

1. **TCP listener** on configurable port (default `5555`)
2. **Frame parser** — read 4-byte command ID + 4-byte payload length, then read payload bytes
3. **Response sender** — write 4-byte JSON length + JSON UTF-8 bytes
4. **State management** — accumulate beam detections across multiple Detect calls per session
5. **Command handlers:**

| Command | What your server must do |
|---------|--------------------------|
| `CMD_DETECT` | Parse JSON header + raw uint16 image → run D4Sigma analysis → return beam results → accumulate for later Fit |
| `CMD_DETECT_FILE` | Read image from file path → same D4Sigma analysis as Detect → accumulate |
| `CMD_FIT` | Run M² Gaussian beam propagation fit on all accumulated data → return fit results |
| `CMD_RESET` | Clear all accumulated detections → return `{"status":"ok"}` |
| `CMD_SAVE` | Write accumulated images and results to disk → return save path |

6. **Byte order** — All integers are **little-endian**
7. **Receive timeout** — client uses 30-second receive timeout; respond within this window
8. **Max response** — client rejects responses with JSON length > 10 MB
9. **Error handling** — Return `{"error": "message"}` for any failures; never send malformed frames

---

## Z-Height Filename Convention

When the client uses `CMD_DETECT_FILE` via the "Detect Folder" recipe, z-heights are extracted from filenames using this default regex:

```
zheight([\d.]+)um
```

Example filenames:
- `zheight1490um_ch0.png` → z_um = 1490.0
- `zheight1500.5um_ch1.tif` → z_um = 1500.5

Supported image extensions: `.png`, `.bmp`, `.tif`, `.tiff`, `.jpg`, `.jpeg`

---

## Beam Propagation Model (for Fit)

The server fits the ISO 11146 Gaussian beam propagation equation:

```
d(z) = 2 × w₀ × sqrt(1 + ((z - z₀) × M² × λ / (π × w₀²))²)
```

Where:
- `d(z)` = D4Sigma beam diameter at position z
- `w₀` = beam waist radius (fit parameter)
- `z₀` = waist z-position (fit parameter)
- `M²` = beam quality factor (fit parameter, 1.0 = ideal Gaussian)
- `λ` = wavelength (provided by client)

Poynting angles are computed from the linear fit of centroid position vs. z:
```
θx = atan(slope_x) × (180/π)  [degrees]
θy = atan(slope_y) × (180/π)  [degrees]
```

TX-RX vector is the spatial separation between beam 0 (TX) and beam 1 (RX) at their respective waist planes.
