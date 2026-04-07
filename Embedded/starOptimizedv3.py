import time
import serial
import struct
import cv2
from picamera2 import Picamera2
from ultralytics import YOLO

# =========================
# CONFIG
# =========================
CAM_W, CAM_H = 640, 640

# YOLO cadence (run once every N frames)
YOLO_EVERY_N_FRAMES = 4  # try 3 or 4 to start

# ROI cropping around the target when locked
USE_ROI_CROP = True
ROI_PAD_SCALE = 1.8      # 1.5–2.2 typical; larger = safer but slower

# Tracker choice
TRACKER_TYPE = "MOSSE"   # "MOSSE", "KCF", or "CSRT"

# UART
UART_PORT = "/dev/ttyAMA2"
UART_BAUD = 460800
UART_TIMEOUT = 0

# Send rate (decoupled from YOLO/tracking)
SEND_HZ = 30.0
SEND_PERIOD = 1.0 / SEND_HZ

# Detection settings
CONF_THRESH = 0.5
PERSON_CLASS = [0]

# Target handling
MAX_LOST_FRAMES = 12
IOU_KEEP_THRESH = 0.15

# Display
SHOW_WINDOW = True
DRAW_OVERLAY = True

# FPS display
SHOW_FPS = True
FPS_ALPHA = 0.10   # smoothing factor

START_BYTE = 0xAA
END_BYTE = 0xFF


# =========================
# PACKETS
# =========================
def build_track_packet(norm_x: float, norm_y: float) -> bytes:
    cmd = b"\x01"
    payload = struct.pack("<ff", float(norm_x), float(norm_y))
    checksum = 0
    for b in cmd + payload:
        checksum ^= b
    return bytes([START_BYTE]) + cmd + payload + bytes([checksum]) + bytes([END_BYTE])

def build_idle_packet() -> bytes:
    cmd = b"\x00"
    payload = b"\x00" * 8
    checksum = 0
    for b in cmd + payload:
        checksum ^= b
    return bytes([START_BYTE]) + cmd + payload + bytes([checksum]) + bytes([END_BYTE])


# =========================
# UTILS
# =========================
def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v

def iou(a, b):
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    inter_x1 = max(ax1, bx1)
    inter_y1 = max(ay1, by1)
    inter_x2 = min(ax2, bx2)
    inter_y2 = min(ay2, by2)
    iw = max(0.0, inter_x2 - inter_x1)
    ih = max(0.0, inter_y2 - inter_y1)
    inter = iw * ih
    area_a = max(0.0, ax2 - ax1) * max(0.0, ay2 - ay1)
    area_b = max(0.0, bx2 - bx1) * max(0.0, by2 - by1)
    union = area_a + area_b - inter
    return (inter / union) if union > 0 else 0.0

def pick_initial_target(person_boxes, frame_w, frame_h):
    cx0, cy0 = frame_w / 2.0, frame_h / 2.0
    best = None
    best_dist2 = 1e18
    for conf, (x1, y1, x2, y2) in person_boxes:
        cx = (x1 + x2) / 2.0
        cy = (y1 + y2) / 2.0
        dist2 = (cx - cx0) ** 2 + (cy - cy0) ** 2
        if dist2 < best_dist2:
            best_dist2 = dist2
            best = (x1, y1, x2, y2)
    return best

def torso_center_from_box(box):
    x1, y1, x2, y2 = box
    w = x2 - x1
    h = y2 - y1

    torso_y1 = y1 + 0.15 * h
    torso_y2 = y1 + 0.65 * h
    torso_x1 = x1 + 0.20 * w
    torso_x2 = x1 + 0.80 * w

    cx = (torso_x1 + torso_x2) / 2.0
    cy = (torso_y1 + torso_y2) / 2.0
    return cx, cy

def make_tracker(tracker_type: str):
    t = tracker_type.upper()

    def _try(*names):
        for name in names:
            if hasattr(cv2, "legacy") and hasattr(cv2.legacy, name):
                return getattr(cv2.legacy, name)()
            if hasattr(cv2, name):
                return getattr(cv2, name)()
        return None

    if t == "MOSSE":
        trk = _try("TrackerMOSSE_create")
        if trk is None:
            raise RuntimeError(
                "MOSSE tracker not available in your OpenCV build. "
                "Install opencv-contrib or switch TRACKER_TYPE to 'CSRT' if available."
            )
        return trk

    if t == "KCF":
        trk = _try("TrackerKCF_create")
        if trk is None:
            raise RuntimeError(
                "KCF tracker not available in your OpenCV build. "
                "Install opencv-contrib or switch TRACKER_TYPE to 'CSRT' if available."
            )
        return trk

    if t == "CSRT":
        trk = _try("TrackerCSRT_create")
        if trk is None:
            raise RuntimeError("CSRT tracker not available in your OpenCV build.")
        return trk

    trk = _try("TrackerMOSSE_create", "TrackerKCF_create", "TrackerCSRT_create")
    if trk is None:
        raise RuntimeError("No supported tracker constructors found in this OpenCV build.")
    return trk

def box_to_tracker_rect(box):
    x1, y1, x2, y2 = box
    return (float(x1), float(y1), float(x2 - x1), float(y2 - y1))

def tracker_rect_to_box(rect):
    x, y, w, h = rect
    return (float(x), float(y), float(x + w), float(y + h))

def expand_and_clip_roi(box, frame_w, frame_h, scale):
    x1, y1, x2, y2 = box
    cx = (x1 + x2) * 0.5
    cy = (y1 + y2) * 0.5
    bw = (x2 - x1) * scale
    bh = (y2 - y1) * scale
    rx1 = clamp(cx - bw * 0.5, 0, frame_w - 1)
    ry1 = clamp(cy - bh * 0.5, 0, frame_h - 1)
    rx2 = clamp(cx + bw * 0.5, 0, frame_w - 1)
    ry2 = clamp(cy + bh * 0.5, 0, frame_h - 1)
    return (int(rx1), int(ry1), int(rx2), int(ry2))


# =========================
# INIT
# =========================
picam2 = Picamera2()
picam2.preview_configuration.main.size = (CAM_W, CAM_H)
picam2.preview_configuration.main.format = "RGB888"
picam2.preview_configuration.align()
picam2.configure("preview")
picam2.start()

model = YOLO("yolo11n_ncnn_model")
uart = serial.Serial(UART_PORT, baudrate=UART_BAUD, timeout=UART_TIMEOUT)
print(f"[UART] Opened {UART_PORT} at {UART_BAUD} baud — ready to send packets")

# UART diagnostic counters
tx_track_count = 0
tx_idle_count = 0

# Target state
target_box = None
lost_frames = 0

tracker = None
tracker_ok = False

frame_count = 0
last_send_t = 0.0
last_print_t = 0.0

# FPS state
fps = 0.0
last_frame_t = time.monotonic()

time.sleep(0.2)


# =========================
# MAIN LOOP
# =========================
try:
    while True:
        frame = picam2.capture_array()
        frame_h, frame_w = frame.shape[:2]
        frame_count += 1

        # ---------- FPS UPDATE ----------
        now_frame_t = time.monotonic()
        dt = now_frame_t - last_frame_t
        last_frame_t = now_frame_t
        inst_fps = (1.0 / dt) if dt > 1e-6 else 0.0
        fps = (1.0 - FPS_ALPHA) * fps + FPS_ALPHA * inst_fps

        # ---------- 1) TRACK EVERY FRAME ----------
        if tracker is not None:
            tracker_ok, rect = tracker.update(frame)
            if tracker_ok:
                target_box = tracker_rect_to_box(rect)
            else:
                lost_frames += 1

        # ---------- 2) YOLO OCCASIONALLY ----------
        run_yolo = (frame_count % YOLO_EVERY_N_FRAMES == 0) or (target_box is None) or (lost_frames > 0)

        persons = []
        if run_yolo:
            roi_offset_x = 0
            roi_offset_y = 0

            if USE_ROI_CROP and target_box is not None:
                rx1, ry1, rx2, ry2 = expand_and_clip_roi(target_box, frame_w, frame_h, ROI_PAD_SCALE)
                infer_img = frame[ry1:ry2, rx1:rx2]
                roi_offset_x, roi_offset_y = rx1, ry1
            else:
                infer_img = frame

            results = model(infer_img, conf=CONF_THRESH, classes=PERSON_CLASS, verbose=False)

            for b in results[0].boxes:
                if int(b.cls[0]) != 0:
                    continue
                conf = float(b.conf[0])
                x1, y1, x2, y2 = b.xyxy[0].tolist()

                x1 += roi_offset_x
                x2 += roi_offset_x
                y1 += roi_offset_y
                y2 += roi_offset_y

                persons.append((conf, (float(x1), float(y1), float(x2), float(y2))))

            if target_box is None:
                if persons:
                    target_box = pick_initial_target(persons, frame_w, frame_h)
                    lost_frames = 0
                    tracker = make_tracker(TRACKER_TYPE)
                    tracker.init(frame, box_to_tracker_rect(target_box))
                    tracker_ok = True
            else:
                best_match = None
                best_iou = 0.0
                for conf, box in persons:
                    v = iou(target_box, box)
                    if v > best_iou:
                        best_iou = v
                        best_match = box

                if best_match is not None and best_iou >= IOU_KEEP_THRESH:
                    target_box = best_match
                    lost_frames = 0
                    tracker = make_tracker(TRACKER_TYPE)
                    tracker.init(frame, box_to_tracker_rect(target_box))
                    tracker_ok = True
                else:
                    lost_frames += 1
                    if lost_frames >= MAX_LOST_FRAMES:
                        target_box = None
                        tracker = None
                        tracker_ok = False
                        lost_frames = 0
                        uart.write(build_idle_packet())

        # ---------- 3) FIXED-RATE UART SEND ----------
        now = time.monotonic()
        if now - last_send_t >= SEND_PERIOD:
            last_send_t = now

            if target_box is not None:
                cx, cy = torso_center_from_box(target_box)
                norm_x = (cx - frame_w / 2.0) / (frame_w / 2.0)
                norm_y = (cy - frame_h / 2.0) / (frame_h / 2.0)
                uart.write(build_track_packet(norm_x, norm_y))
                tx_track_count += 1
            else:
                uart.write(build_idle_packet())
                tx_idle_count += 1

        # ---------- 4) OVERLAY ----------
        if SHOW_WINDOW:
            disp = frame.copy() if DRAW_OVERLAY else frame

            if DRAW_OVERLAY and target_box is not None:
                cx, cy = torso_center_from_box(target_box)
                cv2.drawMarker(
                    disp, (int(cx), int(cy)), (255, 255, 255),
                    markerType=cv2.MARKER_CROSS, markerSize=18, thickness=2
                )
                x1, y1, x2, y2 = map(int, target_box)
                cv2.rectangle(disp, (x1, y1), (x2, y2), (255, 255, 255), 2)

            if SHOW_FPS:
                cv2.putText(
                    disp,
                    f"FPS: {fps:5.1f}",
                    (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.8,
                    (255, 255, 255),
                    2,
                    cv2.LINE_AA
                )

            cv2.imshow("Camera", disp)
            if cv2.waitKey(1) == ord("q"):
                break

        # ---------- 5) THROTTLED PRINT ----------
        if now - last_print_t > 0.5:
            last_print_t = now
            if target_box is None:
                print(f"[INFO] No target | FPS={fps:.1f} | YOLO every {YOLO_EVERY_N_FRAMES} frames | baud={UART_BAUD}")
            else:
                print(f"[INFO] Target locked | FPS={fps:.1f} | lost_frames={lost_frames} | YOLOevery={YOLO_EVERY_N_FRAMES} | baud={UART_BAUD}")
            print(f"[UART] TX packets: track={tx_track_count} idle={tx_idle_count} total={tx_track_count + tx_idle_count}")

finally:
    uart.close()
    cv2.destroyAllWindows()