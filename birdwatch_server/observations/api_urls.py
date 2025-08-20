from django.urls import path
from .api_views import (
    VideoUploadView,
    live_stream_page,
    ingest_frame,
    live_stream,
    stream_status,
)
from .views import gallery

urlpatterns = [
    path("upload/", VideoUploadView.as_view(), name="api-upload"),
    path("stream/frame", ingest_frame, name="stream-frame"),
    path("stream/live", live_stream, name="stream-live"),
    path("stream/status", stream_status, name="stream-status"),
    path("live/", live_stream_page, name="live-stream-page"),
    path("gallery/", gallery, name="observations-gallery"),
]
