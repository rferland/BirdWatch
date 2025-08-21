from __future__ import annotations
import random
import time
from django.http import JsonResponse

# API qui retourne la bounding box de l'oiseau détecté (exemple statique)

import requests
from django.http import StreamingHttpResponse, HttpResponse
from django.views.decorators.http import require_GET
import threading
import queue

# Vue de redirection de / vers /api/index/
from django.shortcuts import redirect


import os
import json


def bird_box(request):
    path = os.path.join(os.path.dirname(__file__), "../ml_models/last_bird_box.json")
    try:
        with open(path, "r", encoding="utf-8") as f:
            box = json.load(f)
        return JsonResponse(box)
    except Exception as e:
        return JsonResponse({"error": str(e)}, status=500)


# Vue de redirection de / vers /api/index/


def root_redirect(request):
    return redirect("index")


# Vue pour afficher la page index.html
from django.shortcuts import render


def index(request):
    return render(request, "observations/index.html")


# --- Vue proxy pour relayer le flux MJPEG de l'ESP32 ---
from collections import defaultdict
from dataclasses import dataclass
from typing import List
from urllib.parse import urlencode
from django.http import HttpRequest, HttpResponse
from django.shortcuts import render
from django.utils.dateparse import parse_date
from .models import Observation


# --- MJPEG Proxy mémoire pour multi-clients ---
ESP32_STREAM_URL = "http://10.0.0.76:81/stream"
_mjpeg_clients = []  # List of queues for each client
_mjpeg_lock = threading.Lock()


def _mjpeg_proxy_worker():
    print("[MJPEG Proxy] Thread démarré")
    while True:
        try:
            resp = requests.get(ESP32_STREAM_URL, stream=True, timeout=5)
            if resp.status_code != 200:
                print(f"[MJPEG Proxy] Erreur HTTP: {resp.status_code}")
                time.sleep(2)
                continue
            boundary = None
            ctype = resp.headers.get("Content-Type", "")
            if "boundary=" in ctype:
                boundary = ctype.split("boundary=")[-1]
            if not boundary:
                print("[MJPEG Proxy] Pas de boundary trouvé")
                time.sleep(2)
                continue
            boundary = boundary.encode()
            buf = b""
            for chunk in resp.iter_content(chunk_size=4096):
                buf += chunk
                while True:
                    start = buf.find(b"--" + boundary)
                    if start == -1:
                        break
                    end = buf.find(b"--" + boundary, start + len(boundary) + 2)
                    if end == -1:
                        break
                    part = buf[start:end]
                    buf = buf[end:]
                    # Distribue à tous les clients
                    with _mjpeg_lock:
                        for q in _mjpeg_clients:
                            try:
                                q.put(part, block=False)
                            except queue.Full:
                                pass
        except Exception as e:
            print(f"[MJPEG Proxy] Erreur: {e}")
            time.sleep(2)


# Lance le thread proxy au premier import
if not hasattr(globals(), "_mjpeg_proxy_started"):
    t = threading.Thread(target=_mjpeg_proxy_worker, daemon=True)
    t.start()
    globals()["_mjpeg_proxy_started"] = True


@require_GET
def proxy_stream(request):
    # Chaque client reçoit sa propre file de frames (JPEG purs)
    q = queue.Queue(maxsize=20)
    with _mjpeg_lock:
        _mjpeg_clients.append(q)

    def gen():
        try:
            while True:
                part = q.get(timeout=10)
                # Extrait le JPEG du part (recherche des marqueurs JPEG)
                start = part.find(b"\xff\xd8")
                end = part.find(b"\xff\xd9")
                if start != -1 and end != -1:
                    jpeg = part[start : end + 2]
                    yield (
                        b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                        + str(len(jpeg)).encode()
                        + b"\r\n\r\n"
                        + jpeg
                        + b"\r\n"
                    )
        except Exception:
            pass
        finally:
            with _mjpeg_lock:
                if q in _mjpeg_clients:
                    _mjpeg_clients.remove(q)

    return StreamingHttpResponse(
        gen(), content_type="multipart/x-mixed-replace; boundary=frame"
    )


@dataclass
class SpeciesGroup:
    species: str
    observations: List[Observation]
    representative: Observation | None
    count: int


def gallery(request: HttpRequest) -> HttpResponse:
    """Affiche les observations groupées par espèce avec pagination & filtres.

    Filtres query params:
      - species : nom exact d'espèce (prioritaire)
      - q : sous-chaîne insensible à la casse dans le nom d'espèce
      - start, end : dates (YYYY-MM-DD) pour filtrer par created_at (intervalle inclusif)
      - per : nombre de groupes (espèces) par page (défaut 12, max 100)
      - page : page (1+)
    """
    qs = Observation.objects.recent()  # type: ignore[attr-defined]

    # Date filtering
    start_str = request.GET.get("start")
    end_str = request.GET.get("end")
    if start_str:
        d = parse_date(start_str)
        if d:
            qs = qs.filter(created_at__date__gte=d)
    if end_str:
        d = parse_date(end_str)
        if d:
            qs = qs.filter(created_at__date__lte=d)

    # Species exact filter
    species_exact = request.GET.get("species")
    if species_exact:
        qs = qs.filter(bird_species=species_exact)
    else:
        # Substring search
        q_term = request.GET.get("q")
        if q_term:
            qs = qs.filter(bird_species__icontains=q_term)

    obs_list = list(qs)

    # Group by species
    groups: dict[str, list[Observation]] = defaultdict(list)
    for obs in obs_list:
        groups[obs.bird_species].append(obs)

    species_groups: list[SpeciesGroup] = []
    for species, items in groups.items():
        rep = next((o for o in items if o.frame_image), None)
        species_groups.append(
            SpeciesGroup(
                species=species,
                observations=items,
                representative=rep,
                count=len(items),
            )
        )
    species_groups.sort(key=lambda g: g.species.lower())

    # Pagination (on species groups)
    per_default = 12
    try:
        per = int(request.GET.get("per", per_default))
    except ValueError:
        per = per_default
    per = max(1, min(per, 100))

    try:
        page = int(request.GET.get("page", "1"))
    except ValueError:
        page = 1
    page = max(1, page)

    total_groups = len(species_groups)
    total_pages = (total_groups + per - 1) // per if total_groups else 1
    if page > total_pages:
        page = total_pages
    start_idx = (page - 1) * per
    end_idx = start_idx + per
    page_groups = species_groups[start_idx:end_idx]

    # Build base query (without page)
    preserved_params = {}
    for key in ["species", "q", "start", "end", "per"]:
        val = request.GET.get(key)
        if val:
            preserved_params[key] = val
    query_no_page = urlencode(preserved_params)

    context = {
        "species_groups": page_groups,
        "total": len(obs_list),
        "total_species": total_groups,
        "page": page,
        "per": per,
        "total_pages": total_pages,
        "has_prev": page > 1,
        "has_next": page < total_pages,
        "prev_page": page - 1,
        "next_page": page + 1,
        "query_no_page": query_no_page,
        "species_exact": species_exact or "",
        "q_term": request.GET.get("q", ""),
        "start_str": start_str or "",
        "end_str": end_str or "",
    }
    return render(request, "observations/gallery.html", context)
