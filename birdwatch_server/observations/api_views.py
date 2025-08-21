import time
import threading
from pathlib import Path
from django.views import View
from django.http import JsonResponse, HttpRequest, StreamingHttpResponse, HttpResponse
from django.core.files.base import ContentFile
from django.views.decorators.csrf import csrf_exempt
from django.utils.decorators import method_decorator
from .models import Observation
from .ml import get_pipeline
import cv2
import tempfile
from collections import deque


class StreamBuffer:
    """Thread-safe buffer pour stocker la dernière frame JPEG et calculer un FPS approximatif.

    Avantages:
    - Encapsulation (évite les variables globales)
    - Extensible (peut ajouter conversion, redimensionnement, expiration)
    - Testable (méthodes unitaires)
    """

    def __init__(self, history_size: int = 5):
        self._lock = threading.Lock()
        self._latest: bytes | None = None
        self._recent = deque(maxlen=history_size)

    def update(self, frame_jpeg: bytes):
        with self._lock:
            self._latest = frame_jpeg
            self._recent.append((time.time(), len(frame_jpeg)))

    def get(self) -> bytes | None:
        with self._lock:
            return self._latest

    def fps(self, window: float = 5.0) -> float:
        now = time.time()
        with self._lock:
            count = len([t for t, _ in self._recent if now - t < window])
        return count / window if window > 0 else 0.0


# Instance unique (peut être déplacée vers un module séparé si besoin)
stream_buffer = StreamBuffer()


@method_decorator(csrf_exempt, name="dispatch")
class VideoUploadView(View):
    def post(self, request: HttpRequest):
        video = request.FILES.get("video_file")
        if not video:
            return JsonResponse({"error": "video_file manquant"}, status=400)
        obs = self._process_uploaded_file(video)
        if obs is None:
            return JsonResponse({"message": "Aucun oiseau détecté"})
        return JsonResponse(
            {
                "id": obs.id,
                "species": obs.bird_species,
                "confidence": obs.confidence,
                "created_at": obs.created_at.isoformat(),
            }
        )

    def _process_uploaded_file(self, file_obj):
        observation = None
        pipeline = get_pipeline()
        tmp_dir = Path(tempfile.gettempdir())
        temp_path = tmp_dir / file_obj.name
        with open(temp_path, "wb") as f:
            for chunk in file_obj.chunks():
                f.write(chunk)
        result = pipeline.detect_and_classify(temp_path)
        if result:
            species, conf, frame = result
            # Save original video
            observation = Observation.objects.create(  # pylint: disable=no-member
                bird_species=species,
                confidence=conf,
                video_file=file_obj,
                frame_image=None,
            )
            # Save frame image
            ok, buf = cv2.imencode(".jpg", frame)  # pylint: disable=no-member
            if ok:
                observation.frame_image.save(
                    f"frame_{observation.id}.jpg", ContentFile(buf.tobytes())
                )
        return observation


from django.urls import path

urlpatterns_api = [
    path("upload/", VideoUploadView.as_view(), name="upload"),
]


# --- Streaming Frame Ingest (ESP32 envoie des JPEG successifs) ---
@csrf_exempt
def ingest_frame(request: HttpRequest):
    if request.method != "POST":
        return JsonResponse({"error": "POST requis"}, status=405)
    frame_file = request.FILES.get("frame")
    if not frame_file:
        return JsonResponse({"error": "frame manquant"}, status=400)
    data = frame_file.read()
    # Validation rapide JPEG
    if not data.startswith(b"\xff\xd8"):
        return JsonResponse({"error": "Format non JPEG"}, status=400)
    stream_buffer.update(data)
    return JsonResponse({"status": "ok", "size": len(data)})


def _mjpeg_generator():
    boundary = b"--frame"
    while True:
        frame = stream_buffer.get()
        if frame is not None:
            yield (
                boundary
                + b"\r\nContent-Type: image/jpeg\r\nContent-Length: "
                + str(len(frame)).encode()
                + b"\r\n\r\n"
                + frame
                + b"\r\n"
            )
        time.sleep(0.15)  # ~6-7 fps ; ajuster selon besoin


def live_stream(request: HttpRequest):  # pylint: disable=unused-argument
    resp = StreamingHttpResponse(
        _mjpeg_generator(), content_type="multipart/x-mixed-replace; boundary=frame"
    )
    resp["Cache-Control"] = "no-cache, no-store, must-revalidate"
    return resp


def stream_status(request: HttpRequest):  # pylint: disable=unused-argument
    return JsonResponse({"fps_approx": stream_buffer.fps(5.0)})


def live_stream_page(
    request: HttpRequest,
):  # HTML page embedding MJPEG + status  # pylint: disable=unused-argument
    from django.shortcuts import render
    return render(request, "observations/live_stream.html")


urlpatterns_api += [
    path("stream/frame", ingest_frame, name="stream-frame"),
    path("stream/live", live_stream, name="stream-live"),
    path("stream/status", stream_status, name="stream-status"),
]
