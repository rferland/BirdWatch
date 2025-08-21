def yolo_results_to_dicts(results):
    """Convertit les résultats YOLO (ultralytics) en liste de dicts JSON-compatibles."""
    out = []
    for res in results:
        boxes = res.boxes.xywh.cpu().numpy()  # (N, 4) : x_c, y_c, w, h
        scores = res.boxes.conf.cpu().numpy()  # (N,)
        class_ids = res.boxes.cls.cpu().numpy()  # (N,)
        for i in range(len(boxes)):
            x_c, y_c, w, h = boxes[i]
            x = int(x_c - w / 2)
            y = int(y_c - h / 2)
            out.append(
                {
                    "x": x,
                    "y": y,
                    "w": int(w),
                    "h": int(h),
                    "score": float(scores[i]),
                    "class_id": int(class_ids[i]),
                    "ts": int(time.time()),
                }
            )
    return out


def save_frame_with_boxes(frame, detections, out_path):
    """Enregistre le frame avec les rectangles des détections (format COCO: x, y, w, h)."""
    frame_draw = frame.copy()
    for det in detections:
        if det["class_id"] == 14 and det["score"] > 0.45:
            x, y, w, h = det["x"], det["y"], det["w"], det["h"]
            cv2.rectangle(frame_draw, (x, y), (x + w, y + h), (0, 255, 0), 2)
            label = f"bird {det['score']:.2f}"
            cv2.putText(
                frame_draw,
                label,
                (x, y - 10),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (0, 255, 0),
                2,
            )
    cv2.imwrite(out_path, frame_draw)


import json
import time
import cv2
import numpy as np
import onnxruntime as ort
import requests
from ultralytics import YOLO

# Charger le modèle
model = YOLO("BirdWatch/birdwatch_server/ml_models/yolov8n.pt")


# Paramètres
STREAM_URL = "http://localhost:8000/api/proxy-stream/"
ONNX_PATH = (
    "BirdWatch/birdwatch_server/ml_models/bird_detector.onnx"  # adapte si besoin
)
OUTPUT_PATH = "BirdWatch/birdwatch_server/ml_models/last_bird_box.json"

# Prétraitement pour le modèle (à adapter selon ton modèle)
IMG_SIZE = (320, 320)  # ou la taille attendue par ton modèle

# Chargement du modèle ONNX
session = ort.InferenceSession(ONNX_PATH, providers=["CPUExecutionProvider"])
input_name = session.get_inputs()[0].name


# Fonction de détection (à adapter selon ton modèle)
def sigmoid(x):
    return 1 / (1 + np.exp(-x))


def detect_bird(frame):
    results = model(frame)  # Inference sur l'image
    for res in results:
        print(res)  # Résumé: bounding boxes, classes, confidences
    dicts = yolo_results_to_dicts(results)
    return dicts if dicts else None
    # return detections if detections else None


# Boucle principale avec reconnexion automatique
while True:
    try:
        s = requests.Session()
        r = s.get(STREAM_URL, stream=True, timeout=15)
        bytes_ = b""
        for chunk in r.iter_content(chunk_size=1024):
            bytes_ += chunk
            a = bytes_.find(b"\xff\xd8")
            b_ = bytes_.find(b"\xff\xd9")
            if a != -1 and b_ != -1:
                jpg = bytes_[a : b_ + 2]
                bytes_ = bytes_[b_ + 2 :]
                frame = cv2.imdecode(
                    np.frombuffer(jpg, dtype=np.uint8), cv2.IMREAD_COLOR
                )
                if frame is not None:
                    print("[bird_detector_worker] Image reçue")
                    box = detect_bird(frame)
                    if box:
                        with open(OUTPUT_PATH, "w") as f:
                            json.dump(box, f)
                        # Sauvegarde l'image avec rectangles
                        save_frame_with_boxes(
                            frame,
                            box,
                            "BirdWatch/birdwatch_server/ml_models/last_detected.jpg",
                        )
                else:
                    print("[bird_detector_worker] Erreur décodage image")
                time.sleep(0.1)  # Limite la fréquence (10 fps max)
    except Exception as e:
        print(f"[bird_detector_worker] Erreur: {e}. Nouvelle tentative dans 5s...")
        time.sleep(5)
