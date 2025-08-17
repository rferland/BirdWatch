# Birdwatch Server (Django)

Serveur Django pour Raspberry Pi recevant des vidéos (upload HTTP ou flux multipart), détectant la présence d'un oiseau puis classifiant l'espèce et enregistrant les métadonnées dans SQLite.

## Fonctionnalités

- Upload de fichiers vidéo (POST /api/upload/)
- Endpoint streaming chunked (POST /api/stream/) optionnel
- Détection présence d'oiseau (stub + modèle ONNX si disponible)
- Classification espèce (stub + mapping labels)
- Sauvegarde Observation (espèce, confiance, horodatage, nom fichier) dans SQLite
- Interface admin Django

## Stack

- Python 3.11+ (ou 3.9+ sur Pi)
- Django 5.x
- Pillow (miniatures)
- opencv-python (analyse frames) — sur Pi utiliser opencv-python-headless
- onnxruntime (ou onnxruntime-gpu si support) — sur Pi ARM32 utiliser onnxruntime (wheel dispo aarch64)

## Installation

```
python -m venv .venv
. .venv/Scripts/activate  # Windows PowerShell: .venv\Scripts\Activate.ps1
pip install -r requirements.txt
python manage.py migrate
python manage.py createsuperuser
python manage.py runserver 0.0.0.0:8000
```

### Windows PowerShell (détails)

```
python -m venv .venv
./.venv/Scripts/Activate.ps1   # ou: . .venv/Scripts/Activate.ps1
pip install -r requirements.txt
cd birdwatch_server            # si vous êtes à la racine du repo
python manage.py migrate
python manage.py createsuperuser
python manage.py runserver 0.0.0.0:8000
```

Si vous obtenez une erreur de politique d'exécution (ExecutionPolicy), exécutez d'abord:

```
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

Pour vérifier que Django est installé dans l'environnement:

```
python -c "import django; print(django.get_version())"
```

Pour désactiver:

```
deactivate
```

## Upload vidéo

```
POST /api/upload/
Form-Data: video_file=<fichier.mp4>
```

Réponse: JSON avec observation si oiseau détecté.

## Modèles IA

Placer un modèle détection (ex: yolov8 birds) converti en ONNX dans `ml_models/bird_detector.onnx` et un modèle classification dans `ml_models/bird_classifier.onnx`.
Si absents: fallback heuristique simple (mouvement + couleur) et classification "unknown".

## Tâches futures

- Auth token pour upload
- Websocket progress
- Tableau frontend
- Retrain pipeline

## Licence

MIT (adapter selon besoin)
