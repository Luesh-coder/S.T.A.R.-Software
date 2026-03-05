import serial
import struct
import cv2
from picamera2 import Picamera2
from ultralytics import YOLO

CAM_W, CAM_H = 640, 640

picam2 = Picamera2()
picam2.preview_configuration.main.size = (CAM_W, CAM_H)
picam2.preview_configuration.main.format = "RGB888"
picam2.preview_configuration.align()
picam2.configure("preview")
picam2.start()

model = YOLO("yolo11n_ncnn_model")
uart = serial.Serial('/dev/ttyAMA2', baudrate=115200, timeout=1)

START_BYTE = 0xAA
END_BYTE   = 0xFF

# ===== Single-target tracking state =====
target_box = None          # (x1,y1,x2,y2) of the locked person
lost_frames = 0
MAX_LOST_FRAMES = 10       # how long to tolerate losing target before reacquiring
IOU_KEEP_THRESH = 0.15     # minimum overlap to consider "same person" (tune 0.1–0.3)

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
    """
    Pick ONE person to lock onto.
    Strategy: closest to center (more stable than "highest conf" when multiple people exist).
    Each item is (conf, (x1,y1,x2,y2)).
    """
    cx0, cy0 = frame_w / 2.0, frame_h / 2.0
    best = None
    best_score = 1e18
    for conf, (x1,y1,x2,y2) in person_boxes:
        cx = (x1 + x2) / 2.0
        cy = (y1 + y2) / 2.0
        dist2 = (cx - cx0)**2 + (cy - cy0)**2
        # optionally weight by confidence: dist2 / max(conf,1e-6)
        if dist2 < best_score:
            best_score = dist2
            best = (x1,y1,x2,y2)
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

def build_packet(norm_x, norm_y):
    cmd = b'\x01'
    payload = struct.pack('<ff', norm_x, norm_y)
    checksum = 0
    for b in cmd + payload:
        checksum ^= b
    return bytes([START_BYTE]) + cmd + payload + bytes([checksum]) + bytes([END_BYTE])

while True:
    frame = picam2.capture_array()
    frame_h, frame_w = frame.shape[:2]

    results = model(frame, conf=0.5, classes=[0])
    annotated = results[0].plot()

    # Collect person boxes (conf, box)
    persons = []
    for b in results[0].boxes:
        if int(b.cls[0]) != 0:
            continue
        conf = float(b.conf[0])
        x1, y1, x2, y2 = b.xyxy[0].tolist()
        persons.append((conf, (float(x1), float(y1), float(x2), float(y2))))

    # ---- Target logic ----
    if target_box is None:
        if persons:
            target_box = pick_initial_target(persons, frame_w, frame_h)
            lost_frames = 0
            print("Locked onto a target.")
    else:
        # Find detection that best matches current target by IoU
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
        else:
            lost_frames += 1
            if lost_frames >= MAX_LOST_FRAMES:
                print("Target lost. Reacquiring...")
                target_box = None
                lost_frames = 0

    # ---- Send torso aim if we have a target ----
    if target_box is not None:
        cx, cy = torso_center_from_box(target_box)
        norm_x = (cx - frame_w / 2.0) / (frame_w / 2.0)
        norm_y = (cy - frame_h / 2.0) / (frame_h / 2.0)

        packet = build_packet(norm_x, norm_y)
        uart.write(packet)

        # Visualize lock point + target box
        cx_px = int(cx); cy_px = int(cy)
        cv2.drawMarker(annotated, (cx_px, cy_px), (255, 255, 255),
                       markerType=cv2.MARKER_CROSS, markerSize=20, thickness=2)

        x1, y1, x2, y2 = map(int, target_box)
        cv2.rectangle(annotated, (x1, y1), (x2, y2), (255, 255, 255), 2)

        print(f"Tracking target | lost={lost_frames} | norm_x={norm_x:.3f}, norm_y={norm_y:.3f}")
    else:
        print("No target locked.")

    cv2.imshow("Camera", annotated)
    if cv2.waitKey(1) == ord("q"):
        break

uart.close()
cv2.destroyAllWindows()