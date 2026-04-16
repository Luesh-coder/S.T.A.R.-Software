import time
import serial
import struct
import math
import cv2
from picamera2 import Picamera2
from ultralytics import YOLO

# =========================
# CONFIG
# =========================
CAM_W, CAM_H = 640, 640

# YOLO cadence (run once every N frames)
YOLO_EVERY_N_FRAMES = 4

# ROI cropping around the target when locked
USE_ROI_CROP = True
ROI_PAD_SCALE = 1.8

# Tracker choice
TRACKER_TYPE = "MOSSE"   # "MOSSE", "KCF", or "CSRT"

# UART
UART_PORT = "/dev/ttyAMA2"
UART_BAUD = 460800
UART_TIMEOUT = 0

# Send rate
SEND_HZ = 30.0
SEND_PERIOD = 1.0 / SEND_HZ

# Detection settings
CONF_THRESH = 0.5
PERSON_CLASS = [0]

# Target handling
MAX_LOST_FRAMES = 12
IOU_KEEP_THRESH = 0.15

# --- Anti-jitter / smoothing (v7) ---
# Box smoothing — much heavier than before
BOX_EMA_ALPHA_FAST = 0.35   # used when target is clearly moving
BOX_EMA_ALPHA_SLOW = 0.08   # used when target is nearly still
REINIT_IOU_THRESH = 0.70    # only re-init tracker if YOLO disagrees a lot

# Pixel-level box deadzone: ignore tiny wobble from the tracker/YOLO.
# If the new box corners move less than this many pixels, snap them back
# to the previous value (prevents 1–2 px shimmering).
BOX_CORNER_DEADZONE_PX = 2.0

# Center stillness detector (in pixels). If the torso center has been
# moving less than this for STILL_FRAMES_REQUIRED frames, we consider
# the target "still" and freeze the output completely.
STILL_CENTER_DEADZONE_PX = 3.0
STILL_FRAMES_REQUIRED = 6

# Output smoothing (normalized coords)
OUTPUT_EMA_ALPHA_FAST = 0.40
OUTPUT_EMA_ALPHA_SLOW = 0.10

# Output deadzone with hysteresis. Once inside the "inner" zone, the
# output is held at zero until it exceeds the "outer" zone.
DEADZONE_INNER = 0.020
DEADZONE_OUTER = 0.035

# Minimum change required to actually transmit a new value (prevents
# sending tiny oscillations to the gimbal).
MIN_SEND_DELTA = 0.004

# Display
SHOW_WINDOW = True
DRAW_OVERLAY = True

# FPS display
SHOW_FPS = True
FPS_ALPHA = 0.10

START_BYTE = 0xAA
END_BYTE = 0xFF

CMD_IDLE          = 0x00
CMD_TRACK         = 0x01
CMD_SWITCH_TARGET = 0x02
IN_PACKET_LEN     = 12


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


_rx_buf = bytearray()

def poll_inbound_command():
    """Non-blocking: return a command byte if a full valid packet was parsed, else None."""
    try:
        chunk = uart.read(64)
    except Exception:
        return None
    if chunk:
        _rx_buf.extend(chunk)

    while _rx_buf and _rx_buf[0] != START_BYTE:
        _rx_buf.pop(0)

    if len(_rx_buf) < IN_PACKET_LEN:
        return None

    pkt = bytes(_rx_buf[:IN_PACKET_LEN])
    del _rx_buf[:IN_PACKET_LEN]

    if pkt[-1] != END_BYTE:
        return None
    csum = 0
    for b in pkt[1:10]:
        csum ^= b
    if csum != pkt[10]:
        return None
    return pkt[1]


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

def blend_box(old_box, new_box, alpha):
    return (
        (1 - alpha) * old_box[0] + alpha * new_box[0],
        (1 - alpha) * old_box[1] + alpha * new_box[1],
        (1 - alpha) * old_box[2] + alpha * new_box[2],
        (1 - alpha) * old_box[3] + alpha * new_box[3],
    )

def apply_box_corner_deadzone(old_box, new_box, thresh_px):
    """If a corner moved less than thresh_px, keep the old value."""
    out = []
    for o, n in zip(old_box, new_box):
        if abs(n - o) < thresh_px:
            out.append(o)
        else:
            out.append(n)
    return tuple(out)

def box_center(box):
    x1, y1, x2, y2 = box
    return ((x1 + x2) * 0.5, (y1 + y2) * 0.5)

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

def pick_next_nearest(current_box, person_boxes, exclude_iou=0.3):
    """Return the detected person whose bbox center is closest to the current
    target's center, excluding any box that overlaps the current target too
    much (that would just re-select the same person)."""
    if current_box is None or not person_boxes:
        return None
    cx, cy = box_center(current_box)
    best = None
    best_dist2 = 1e18
    for _conf, box in person_boxes:
        if iou(current_box, box) >= exclude_iou:
            continue
        bcx, bcy = box_center(box)
        dist2 = (bcx - cx) ** 2 + (bcy - cy) ** 2
        if dist2 < best_dist2:
            best_dist2 = dist2
            best = box
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
            raise RuntimeError("MOSSE tracker not available in your OpenCV build.")
        return trk
    if t == "KCF":
        trk = _try("TrackerKCF_create")
        if trk is None:
            raise RuntimeError("KCF tracker not available in your OpenCV build.")
        return trk
    if t == "CSRT":
        trk = _try("TrackerCSRT_create")
        if trk is None:
            raise RuntimeError("CSRT tracker not available in your OpenCV build.")
        return trk

    trk = _try("TrackerMOSSE_create", "TrackerKCF_create", "TrackerCSRT_create")
    if trk is None:
        raise RuntimeError("No supported tracker constructors found.")
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

tx_track_count = 0
tx_idle_count = 0

target_box = None
lost_frames = 0

tracker = None
tracker_ok = False

# Set by inbound SWITCH_TARGET command; consumed next time YOLO runs.
switch_target_requested = False

# Most recent YOLO person detections, cached for every-frame overlay drawing.
last_persons = []

# Smoothed output state
sent_norm_x = 0.0
sent_norm_y = 0.0
last_tx_x = 0.0
last_tx_y = 0.0

# Deadzone hysteresis state (True = currently holding at zero)
in_deadzone_x = True
in_deadzone_y = True

# Stillness detector state
prev_center = None
still_frames = 0

frame_count = 0
last_send_t = 0.0
last_print_t = 0.0

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

        # ---------- INBOUND COMMANDS (from ESP32) ----------
        cmd = poll_inbound_command()
        if cmd == CMD_SWITCH_TARGET:
            switch_target_requested = True
            print("[UART] <- SWITCH_TARGET received")

        # ---------- FPS ----------
        now_frame_t = time.monotonic()
        dt = now_frame_t - last_frame_t
        last_frame_t = now_frame_t
        inst_fps = (1.0 / dt) if dt > 1e-6 else 0.0
        fps = (1.0 - FPS_ALPHA) * fps + FPS_ALPHA * inst_fps

        # ---------- 1) TRACK EVERY FRAME ----------
        if tracker is not None:
            tracker_ok, rect = tracker.update(frame)
            if tracker_ok:
                new_box = tracker_rect_to_box(rect)
                if target_box is None:
                    target_box = new_box
                else:
                    # Kill sub-pixel wobble before smoothing
                    new_box = apply_box_corner_deadzone(target_box, new_box, BOX_CORNER_DEADZONE_PX)
                    # Adaptive alpha: slow when nearly still
                    alpha = BOX_EMA_ALPHA_SLOW if still_frames >= STILL_FRAMES_REQUIRED else BOX_EMA_ALPHA_FAST
                    target_box = blend_box(target_box, new_box, alpha)
            else:
                lost_frames += 1

        # ---------- 2) YOLO OCCASIONALLY ----------
        run_yolo = (
            frame_count % YOLO_EVERY_N_FRAMES == 0
            or target_box is None
            or lost_frames > 0
            or switch_target_requested
        )

        persons = []
        if run_yolo:
            roi_offset_x = 0
            roi_offset_y = 0

            # When a switch is requested we must see ALL people in frame,
            # not just those inside the ROI around the current target.
            if USE_ROI_CROP and target_box is not None and not switch_target_requested:
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

            last_persons = persons

            if target_box is None:
                if persons:
                    target_box = pick_initial_target(persons, frame_w, frame_h)
                    lost_frames = 0
                    tracker = make_tracker(TRACKER_TYPE)
                    tracker.init(frame, box_to_tracker_rect(target_box))
                    tracker_ok = True
                    prev_center = None
                    still_frames = 0
            else:
                # If the operator tapped "New Target", switch to the person
                # whose center is nearest to the current target (excluding
                # the current target itself). Consume the flag regardless.
                if switch_target_requested:
                    new_tgt = pick_next_nearest(target_box, persons, exclude_iou=0.3)
                    # Fallback: if nothing passed the IoU exclusion but there
                    # are multiple detections, pick the one farthest from the
                    # current target center — that's definitely a different
                    # person.
                    if new_tgt is None and len(persons) >= 2:
                        cx_t, cy_t = box_center(target_box)
                        best = None
                        best_dist2 = -1.0
                        for _conf, pbox in persons:
                            if iou(target_box, pbox) >= 0.9:
                                continue
                            bcx, bcy = box_center(pbox)
                            d2 = (bcx - cx_t) ** 2 + (bcy - cy_t) ** 2
                            if d2 > best_dist2:
                                best_dist2 = d2
                                best = pbox
                        new_tgt = best

                    switch_target_requested = False
                    if new_tgt is not None:
                        target_box = new_tgt
                        tracker = make_tracker(TRACKER_TYPE)
                        tracker.init(frame, box_to_tracker_rect(target_box))
                        tracker_ok = True
                        lost_frames = 0
                        prev_center = None
                        still_frames = 0
                        print("[TARGET] switched to next-nearest person")
                        continue
                    else:
                        print("[TARGET] switch requested but no other person in frame")

                best_match = None
                best_iou = 0.0
                for conf, box in persons:
                    v = iou(target_box, box)
                    if v > best_iou:
                        best_iou = v
                        best_match = box

                if best_match is not None and best_iou >= IOU_KEEP_THRESH:
                    # Apply pixel deadzone on YOLO box too, then EMA-blend
                    best_match = apply_box_corner_deadzone(target_box, best_match, BOX_CORNER_DEADZONE_PX)
                    alpha = BOX_EMA_ALPHA_SLOW if still_frames >= STILL_FRAMES_REQUIRED else BOX_EMA_ALPHA_FAST
                    target_box = blend_box(target_box, best_match, alpha)
                    lost_frames = 0

                    # Only re-seed the tracker if it has actually drifted
                    if best_iou < REINIT_IOU_THRESH:
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
                        sent_norm_x = 0.0
                        sent_norm_y = 0.0
                        last_tx_x = 0.0
                        last_tx_y = 0.0
                        in_deadzone_x = True
                        in_deadzone_y = True
                        prev_center = None
                        still_frames = 0
                        uart.write(build_idle_packet())

        # ---------- 2.5) STILLNESS DETECTION ----------
        if target_box is not None:
            cur_center = box_center(target_box)
            if prev_center is not None:
                dx = cur_center[0] - prev_center[0]
                dy = cur_center[1] - prev_center[1]
                if math.hypot(dx, dy) < STILL_CENTER_DEADZONE_PX:
                    still_frames = min(still_frames + 1, 10_000)
                else:
                    still_frames = 0
            prev_center = cur_center
        else:
            prev_center = None
            still_frames = 0

        # ---------- 3) FIXED-RATE UART SEND ----------
        now = time.monotonic()
        if now - last_send_t >= SEND_PERIOD:
            last_send_t = now

            if target_box is not None:
                cx, cy = torso_center_from_box(target_box)
                norm_x = (cx - frame_w / 2.0) / (frame_w / 2.0)
                norm_y = (cy - frame_h / 2.0) / (frame_h / 2.0)

                # Adaptive output smoothing
                out_alpha = OUTPUT_EMA_ALPHA_SLOW if still_frames >= STILL_FRAMES_REQUIRED else OUTPUT_EMA_ALPHA_FAST
                sent_norm_x = (1 - out_alpha) * sent_norm_x + out_alpha * norm_x
                sent_norm_y = (1 - out_alpha) * sent_norm_y + out_alpha * norm_y

                # Hysteresis deadzone on X
                if in_deadzone_x:
                    out_x = 0.0
                    if abs(sent_norm_x) > DEADZONE_OUTER:
                        in_deadzone_x = False
                        out_x = sent_norm_x
                else:
                    out_x = sent_norm_x
                    if abs(sent_norm_x) < DEADZONE_INNER:
                        in_deadzone_x = True
                        out_x = 0.0

                # Hysteresis deadzone on Y
                if in_deadzone_y:
                    out_y = 0.0
                    if abs(sent_norm_y) > DEADZONE_OUTER:
                        in_deadzone_y = False
                        out_y = sent_norm_y
                else:
                    out_y = sent_norm_y
                    if abs(sent_norm_y) < DEADZONE_INNER:
                        in_deadzone_y = True
                        out_y = 0.0

                # If target is still, hard-freeze the output at the last value
                if still_frames >= STILL_FRAMES_REQUIRED:
                    out_x = last_tx_x
                    out_y = last_tx_y

                # Only transmit if value actually changed meaningfully
                if (abs(out_x - last_tx_x) >= MIN_SEND_DELTA or
                    abs(out_y - last_tx_y) >= MIN_SEND_DELTA):
                    last_tx_x = out_x
                    last_tx_y = out_y

                uart.write(build_track_packet(last_tx_x, last_tx_y))
                tx_track_count += 1
            else:
                sent_norm_x = 0.0
                sent_norm_y = 0.0
                last_tx_x = 0.0
                last_tx_y = 0.0
                in_deadzone_x = True
                in_deadzone_y = True
                uart.write(build_idle_packet())
                tx_idle_count += 1

        # ---------- 4) OVERLAY ----------
        if SHOW_WINDOW:
            disp = frame.copy() if DRAW_OVERLAY else frame

            if DRAW_OVERLAY:
                # All detected persons — thin white boxes. Skip the one
                # matching the tracked target; it gets drawn in blue below.
                for _conf, pbox in last_persons:
                    if target_box is not None and iou(pbox, target_box) >= 0.5:
                        continue
                    px1, py1, px2, py2 = map(int, pbox)
                    cv2.rectangle(disp, (px1, py1), (px2, py2), (255, 255, 255), 1)

                # Tracked target — blue box + crosshair
                if target_box is not None:
                    cx, cy = torso_center_from_box(target_box)
                    blue = (255, 0, 0)
                    cv2.drawMarker(
                        disp, (int(cx), int(cy)), blue,
                        markerType=cv2.MARKER_CROSS, markerSize=18, thickness=2
                    )
                    x1, y1, x2, y2 = map(int, target_box)
                    cv2.rectangle(disp, (x1, y1), (x2, y2), blue, 2)

            if SHOW_FPS:
                cv2.putText(
                    disp,
                    f"FPS: {fps:5.1f}  still:{still_frames}",
                    (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.7,
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
                print(f"[INFO] Target locked | FPS={fps:.1f} | still={still_frames} | lost={lost_frames} | baud={UART_BAUD}")
            print(f"[UART] TX packets: track={tx_track_count} idle={tx_idle_count} total={tx_track_count + tx_idle_count}")

finally:
    uart.close()
    cv2.destroyAllWindows()
