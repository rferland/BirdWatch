from django.contrib import admin
from .models import Observation

@admin.register(Observation)
class ObservationAdmin(admin.ModelAdmin):
    list_display = ('id', 'created_at', 'bird_species', 'confidence')
    search_fields = ('bird_species',)
    list_filter = ('bird_species', 'created_at')
