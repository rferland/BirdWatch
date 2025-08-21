from django.apps import AppConfig


import os
import sys
import subprocess


class ObservationsConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "observations"

    def ready(self):
        # Ne lance que si c'est le process principal runserver
        if "runserver" in sys.argv or "runserver_plus" in sys.argv:
            script = os.path.join(
                os.path.dirname(os.path.dirname(__file__)),
                "ml_models",
                "bird_detector_worker.py",
            )
            # Vérifie si déjà lancé (option simple: pas de double lancement)
            if not hasattr(self, "_worker_started"):
                subprocess.Popen([sys.executable, script])
                self._worker_started = True
