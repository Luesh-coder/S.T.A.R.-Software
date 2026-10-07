# S.T.A.R. — Software

Mobile app and embedded firmware for the **S.T.A.R.** (Subject Tracking & Autonomous Response) gimbal system.

## Overview

S.T.A.R. is a two-axis motorized gimbal that autonomously tracks a person in frame using computer vision, and can also be controlled manually from a mobile app. The system is built around three hardware components that work together:

| Component            | Role                                                                                                    |
| -------------------- | ------------------------------------------------------------------------------------------------------- |
| **Raspberry Pi CM5** | Runs YOLO-based person detection and sends servo commands over UART                                     |
| **ESP32-S3**         | Drives the gimbal servos via a PCA9685 PWM driver, hosts a Wi-Fi AP, REST API, and WebSocket server     |
| **React Native App** | Connects to the ESP32 over Wi-Fi to control tracking, switch modes, and manually pan/tilt               |

## Repository Structure

```text
S.T.A.R.-Software/
├── app/                    # Expo Router screens
│   ├── index.tsx           # Main screen (auto tracking controls)
│   ├── manual.tsx          # Manual D-pad control screen
│   └── calibrate.tsx       # Gain calibration screen (sliders)
├── src/
│   ├── api/                # ESP32 communication layer (HTTP + WebSocket)
│   └── components/         # Shared UI components
└── Embedded/
    ├── STAR_ESP32_V3/      # Current ESP32-S3 firmware (Arduino)
    ├── starOptimizedv3.py  # Current Raspberry Pi tracking script
    └── ...                 # Previous firmware/script versions
```

## How It Works

### Auto Mode

The Raspberry Pi runs `starOptimizedv3.py`, which uses a YOLO model to detect people and an OpenCV tracker (MOSSE/KCF/CSRT) to follow a locked target. It sends binary UART packets (`0xAA ... 0xFF`) to the ESP32 at up to 30 Hz. The ESP32 translates normalized `(x, y)` offsets into servo angles with deadband filtering and exponential smoothing.

### Manual Mode

The mobile app connects to the ESP32's WebSocket server (port 81). Holding a D-pad button streams directional commands to the ESP32, which moves the pan/tilt servos in small increments in real time.

### Calibration Mode

The Calibrate screen (accessible from the home screen when connected) exposes four directional tracking gain sliders:

| Slider        | Firmware variable   | Range   |
| ------------- | ------------------- | ------- |
| Tilt Up       | `TILT_UP_GAIN`      | 1.0–3.0 |
| Tilt Down     | `TILT_DOWN_GAIN`    | 1.0–3.0 |
| Pan Left      | `PAN_LEFT_GAIN`     | 1.0–3.0 |
| Pan Right     | `PAN_RIGHT_GAIN`    | 1.0–3.0 |

Adjusting a slider POSTs all four gains to `/api/calibration`. Gains only take effect while the system is in auto tracking mode. They allow fine-tuning of servo response speed per direction to compensate for gimbal mounting position.

### REST API (ESP32 — port 80)

| Endpoint            | Method | Description                                                        |
| ------------------- | ------ | ------------------------------------------------------------------ |
| `/api/status`       | GET    | Returns current mode, tracking state, light state, and gain values |
| `/api/mode`         | POST   | Switch between `"auto"` and `"manual"`                             |
| `/api/tracking`     | POST   | Enable or disable the tracking algorithm                           |
| `/api/target/new`   | POST   | Lock onto a new target in frame                                    |
| `/api/light`        | POST   | Toggle the spotlight                                               |
| `/api/calibration`  | POST   | Set directional tracking gains (range 1.0-3.0)                    |

## Hardware

- **ESP32-S3** dev board
- **PCA9685** 16-channel PWM driver over I2C (addr `0x40`)
  - Ch 0: Pan servo
  - Ch 1: Tilt-Left servo (differential pair)
  - Ch 2: Tilt-Right servo (mirrored)
- **Raspberry Pi CM5** connected via UART1 (RX=GPIO44, TX=GPIO43)
- Spotlight relay / LED on GPIO 2
- Wi-Fi AP: `STAR-ESP32` / `star12345`

## Mobile App Setup

Built with [Expo](https://expo.dev) and React Native.

```bash
npm install
npx expo start
```

Connect your phone to the `STAR-ESP32` Wi-Fi network before launching the app.
