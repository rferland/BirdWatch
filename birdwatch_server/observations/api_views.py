import os
from pathlib import Path
from django.views import View
from django.http import JsonResponse, HttpRequest
from django.core.files.base import ContentFile
from django.views.decorators.csrf import csrf_exempt
from django.utils.decorators import method_decorator
from .models import Observation
from .ml import get_pipeline
import cv2
import tempfile


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
            observation = Observation.objects.create(
                bird_species=species,
                confidence=conf,
                video_file=file_obj,
                frame_image=None,
            )
            # Save frame image
            ok, buf = cv2.imencode(".jpg", frame)
            if ok:
                observation.frame_image.save(
                    f"frame_{observation.id}.jpg", ContentFile(buf.tobytes())
                )
        return observation


from django.urls import path

urlpatterns_api = [
    path("upload/", VideoUploadView.as_view(), name="upload"),
]
