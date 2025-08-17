from django.urls import path
from .api_views import VideoUploadView

urlpatterns = [
    path('upload/', VideoUploadView.as_view(), name='api-upload'),
]
