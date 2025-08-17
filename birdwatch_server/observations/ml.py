"""Simple detection + classification pipeline (stubs + optional ONNX)."""

from __future__ import annotations
from pathlib import Path
from functools import lru_cache
from typing import Optional, Tuple

import cv2  # type: ignore  # OpenCV dynamic attributes trigger pylint no-member
import numpy as np

try:  # pragma: no cover - optional dependency
    import onnxruntime as ort  # type: ignore
except ImportError:  # pragma: no cover
    ort = None  # type: ignore

MODEL_DIR = Path(__file__).resolve().parent.parent / "ml_models"
DETECTOR_PATH = MODEL_DIR / "bird_detector.onnx"
CLASSIFIER_PATH = MODEL_DIR / "bird_classifier.onnx"

SPECIES_LABELS = [
    "cardinal",
    "sparrow",
    "blue_jay",
    "goldfinch",
    "chickadee",
    "hummingbird",
    "woodpecker",
    "crow",
    "robin",
    "unknown",
]


class BirdMLPipeline:
    """Encapsule la logique de détection + classification."""

    def __init__(self) -> None:
        self.det_sess = self._load_session(DETECTOR_PATH)
        self.cls_sess = self._load_session(CLASSIFIER_PATH)

    def _load_session(self, path: Path):  # type: ignore[override]
        if ort and path.exists():
            providers = ["CPUExecutionProvider"]
            return ort.InferenceSession(str(path), providers=providers)
        return None

    def detect_and_classify(
        self, video_path: Path
    ) -> Optional[Tuple[str, float, np.ndarray]]:
        cap = cv2.VideoCapture(str(video_path))  # pylint: disable=no-member
        if not cap.isOpened():
            return None
        frame_count = 0
        best: Tuple[Optional[str], float, Optional[np.ndarray]] = (None, 0.0, None)
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            frame_count += 1
            if frame_count % 8 != 0:
                continue
            bird_prob = self._detect_frame(frame)
            if bird_prob < 0.4:
                continue
            species, conf = self._classify_frame(frame)
            if conf > best[1]:
                best = (species, conf, frame.copy())
        cap.release()
        if best[0] is None or best[2] is None:
            return None
        return best  # type: ignore

    def _detect_frame(self, frame: np.ndarray) -> float:
        if self.det_sess is None:
            hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)  # pylint: disable=no-member
            mask = cv2.inRange(
                hsv, (0, 30, 30), (179, 255, 255)
            )  # pylint: disable=no-member
            ratio = mask.mean() / 255.0
            return float(min(1.0, ratio * 1.5))
        arr = cv2.resize(frame, (320, 320))  # pylint: disable=no-member
        arr = arr[:, :, ::-1].astype("float32") / 255.0
        arr = np.transpose(arr, (2, 0, 1))[None, ...]
        input_name = self.det_sess.get_inputs()[0].name
        out = self.det_sess.run(None, {input_name: arr})[0]
        return float(out.reshape(-1)[0])

    def _classify_frame(self, frame: np.ndarray) -> Tuple[str, float]:
        if self.cls_sess is None:
            return "unknown", 0.5
        arr = cv2.resize(frame, (224, 224))  # pylint: disable=no-member
        arr = arr[:, :, ::-1].astype("float32") / 255.0
        arr = np.transpose(arr, (2, 0, 1))[None, ...]
        input_name = self.cls_sess.get_inputs()[0].name
        logits = self.cls_sess.run(None, {input_name: arr})[0]
        probs = self._softmax(logits[0])
        idx = int(np.argmax(probs))
        species = SPECIES_LABELS[idx] if idx < len(SPECIES_LABELS) else "unknown"
        return species, float(probs[idx])

    def _softmax(self, x: np.ndarray) -> np.ndarray:
        e = np.exp(x - np.max(x))
        return e / e.sum()


@lru_cache(maxsize=1)
def get_pipeline() -> BirdMLPipeline:
    return BirdMLPipeline()
