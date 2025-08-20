from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from typing import List
from urllib.parse import urlencode
from django.http import HttpRequest, HttpResponse
from django.shortcuts import render
from django.utils.dateparse import parse_date
from .models import Observation


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
