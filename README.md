# S.T.A.R. — Software

Mobile app and embedded firmware for **S.T.A.R.** (Spotlight Tracking with Automated Recognition), a low-cost theatrical follow-spot that uses a camera and human detection to keep a performer lit automatically.

## Demo

[![S.T.A.R. Final Demo](https://img.youtube.com/vi/4TAkUadxbjE/hqdefault.jpg)](https://youtu.be/4TAkUadxbjE)

▶️ [Watch the final demo on YouTube](https://youtu.be/4TAkUadxbjE)

## Overview

S.T.A.R. is a two-axis motorized gimbal that autonomously tracks a person in frame using computer vision, and can also be controlled manually from a mobile app. The system is built around three hardware components that work together:

| Component            | Role                                                                                                |
| -------------------- | --------------------------------------------------------------------------------------------------- |
| **Raspberry Pi CM5** | Runs YOLO-based person detection and sends servo commands over UART                                 |
| **ESP32-S3**         | Drives the gimbal servos via a PCA9685 PWM driver, hosts a Wi-Fi AP, REST API, and WebSocket server |
| **React Native App** | Connects to the ESP32 over Wi-Fi to control tracking, switch modes, and manually pan/tilt           |

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
├── results/                # Test data, charts, and final presentation slides
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

| Slider    | Firmware variable | Range   |
| --------- | ----------------- | ------- |
| Tilt Up   | `TILT_UP_GAIN`    | 1.0–3.0 |
| Tilt Down | `TILT_DOWN_GAIN`  | 1.0–3.0 |
| Pan Left  | `PAN_LEFT_GAIN`   | 1.0–3.0 |
| Pan Right | `PAN_RIGHT_GAIN`  | 1.0–3.0 |

Adjusting a slider POSTs all four gains to `/api/calibration`. Gains only take effect while the system is in auto tracking mode. They allow fine-tuning of servo response speed per direction to compensate for gimbal mounting position.

### REST API (ESP32 — port 80)

| Endpoint           | Method | Description                                                        |
| ------------------ | ------ | ------------------------------------------------------------------ |
| `/api/status`      | GET    | Returns current mode, tracking state, light state, and gain values |
| `/api/mode`        | POST   | Switch between `"auto"` and `"manual"`                             |
| `/api/tracking`    | POST   | Enable or disable the tracking algorithm                           |
| `/api/target/new`  | POST   | Lock onto a new target in frame                                    |
| `/api/light`       | POST   | Toggle the spotlight                                               |
| `/api/calibration` | POST   | Set directional tracking gains (range 1.0-3.0)                     |

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

## Results

Two system-level tests were run on the finished prototype: detection confidence and end-to-end tracking latency. Raw data and charts are in [results/](results/).

### Summary

| Specification        | Target                            | Measured (avg of 10 trials)   | Status                         |
| -------------------- | --------------------------------- | ----------------------------- | ------------------------------ |
| Detection Confidence | > 70% on first detection          | **76.2%** (min 71%, max 83%)  | ✅ Met in all 10 trials        |
| Tracking Latency     | 500 ms (basic), 300 ms (advanced) | **396.2 ms** (best 275.22 ms) | ✅ Basic target met on average |

### Detection Confidence

The YOLO person detector was run on live camera input under representative lighting, and the confidence score was recorded on the first detection of a subject entering the frame. The subject was detected in all 10 trials, and every trial was above the 70% target.

| Trial   | Detected | Confidence |
| ------- | -------- | ---------- |
| 1       | ✅       | 0.82       |
| 2       | ✅       | 0.71       |
| 3       | ✅       | 0.80       |
| 4       | ✅       | 0.73       |
| 5       | ✅       | 0.79       |
| 6       | ✅       | 0.73       |
| 7       | ✅       | 0.73       |
| 8       | ✅       | 0.83       |
| 9       | ✅       | 0.72       |
| 10      | ✅       | 0.76       |
| **Avg** |          | **0.762**  |

![Confidence score on first instance](results/Confidence%20Score%20Recorded%20Results.png)

### Tracking Latency

Latency was measured by recording the subject's motion and the spotlight's response in the same high-frame-rate video, then counting frames between the start of the subject's movement and the start of the gimbal's movement. Each frame is 4.17 ms (240 fps), so `latency = frames × 4.17 ms`.

| Trial   | Change in Frames | Latency (ms) |
| ------- | ---------------- | ------------ |
| 1       | 92               | 383.64       |
| 2       | 120              | 500.40       |
| 3       | 73               | 304.41       |
| 4       | 83               | 346.11       |
| 5       | 99               | 412.83       |
| 6       | 86               | 358.62       |
| 7       | 66               | 275.22       |
| 8       | 111              | 462.87       |
| 9       | 123              | 512.91       |
| 10      | 97               | 404.49       |
| **Avg** |                  | **396.2**    |

The average latency of 396.2 ms meets the 500 ms basic target. Trial 7 (66 frames, 275.22 ms) beat the 300 ms advanced target. Trials 2 and 9 were slightly above 500 ms.

### Illumination Uniformity

Lux was measured at the center of the beam and on three rings (radius 280 mm, 560 mm and 840 mm) at 45° intervals. Dividing the average outer-ring (840 mm) lux by the average center lux gives the beam's edge uniformity. The edge intensity met the 50%-of-center benchmark. The beam is brightest in the center, which keeps the light on the performer's face and upper body.

### Problems and Solutions

- **Tracking overshoot:** The gimbal overshot the performer's position even when detection was correct. The backend tracking and control code was tuned to fix this.
- **Heat:** The illumination source runs hot, so a fan was added for active cooling.
- **Housing stability:** The spotlight housing wobbled in motion, so it was rebuilt as a sturdier wooden enclosure.
- **Center of mass:** The illumination source housing was redesigned so the gimbal could support the system's center of mass.
- **Camera IR filter:** The HQ camera's built-in IR filter was too thick for the required lens-to-sensor spacing. It was removed, and a separate filter was mounted on a 3D-printed plate.
- **PCBs:** The original motor regulator IC failed because of a PCB layout error, so a new regulator board was designed around an IC with simpler layout requirements. Misplaced terminal board traces were fixed by cutting the copper and soldering to the correct planes.

### Budget

| Part                           | Cost          |
| ------------------------------ | ------------- |
| ESP32 & Raspberry Pi 5         | $152.47       |
| Raspberry Pi Camera and Lenses | $166.48       |
| Servo Motors & Peripherals     | $303.44       |
| Filament                       | $96.83        |
| Housing Hardware               | $81.33        |
| Lens System                    | $268.37       |
| PCB Components & Boards        | $633.31       |
| **Total**                      | **$1,702.23** |

### Presentations

- [Final Presentation (PDF)](results/Final%20Presentation.pdf): full design review covering the optical design, hardware, PCBs, software and testing
- [Final Demo Slides (PDF)](results/Final%20demo.pdf): Senior Design Showcase slides

## Team (Group 7)

| Name                | Discipline          | Responsibilities                                                                                    |
| ------------------- | ------------------- | --------------------------------------------------------------------------------------------------- |
| Lucio Ruben Villena | Computer Engineer   | Computer vision algorithms, system UI, system logic, motor command code, Senior Design website      |
| Jerison Lau         | Electrical Engineer | PCB design, subsystem integration, 3D design and printing, circuitry and electrical components      |
| Gage Anderson       | Optical Engineer    | Illumination lens design, source selection, illumination gate system, gimbal design                 |
| Travis Nguyen       | Optical Engineer    | Imaging camera selection, imaging lens design, imaging housing, magnification and lens calculations |

**Faculty Advisors:** Dr. Chung Yong Chan, Dr. Aravinda Kar

**Review Committee:** Dr. Jaesung Lee, Dr. Wei Sun, Dr. Stephen Eikenberry, Dr. Andrew Klein
