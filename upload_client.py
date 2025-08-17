"""Client simple pour uploader une vidéo vers le serveur Birdwatch.
Usage:
  python upload_client.py --file chemin\a\video.mp4 --url http://192.168.1.50:8000/api/upload/
Optionnel:
  --retries 3 --timeout 30
"""

from __future__ import annotations
import argparse
import os
import sys
import time
from typing import Optional

try:
    import requests  # type: ignore
except ImportError:  # pragma: no cover
    print("Le module 'requests' est requis. Installez-le: pip install requests")
    sys.exit(1)

DEFAULT_URL = "http://192.168.1.50:8000/api/upload/"


def upload(
    file_path: str, url: str, retries: int, timeout: int, error_dir: Optional[str]
) -> Optional[dict]:
    fname = os.path.basename(file_path)
    for attempt in range(1, retries + 1):
        try:
            with open(file_path, "rb") as f:
                files = {"video_file": (fname, f, "video/mp4")}
                resp = requests.post(url, files=files, timeout=timeout)
            if resp.status_code == 200:
                try:
                    return resp.json()
                except ValueError:
                    print("Réponse non JSON:", resp.text)
                    _save_error_html(error_dir, fname, resp.text, resp.status_code)
                    return None
            else:
                print(f"Echec HTTP {resp.status_code}")
                _save_error_html(error_dir, fname, resp.text, resp.status_code)
        except requests.RequestException as exc:  # type: ignore
            print(f"Erreur tentative {attempt}/{retries}: {exc}")
            _save_error_html(error_dir, fname, str(exc), 0)
        if attempt < retries:
            time.sleep(1.5 * attempt)
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Uploader une vidéo vers le serveur Birdwatch"
    )
    parser.add_argument("--file", "-f", required=True, help="Chemin du fichier vidéo")
    parser.add_argument("--url", default=DEFAULT_URL, help="URL de l'endpoint upload")
    parser.add_argument(
        "--retries", type=int, default=3, help="Nombre de tentatives en cas d'échec"
    )
    parser.add_argument("--timeout", type=int, default=60, help="Timeout requête (s)")
    parser.add_argument(
        "--error-dir",
        default="errors",
        help="Répertoire où stocker les réponses d'erreur en .html (créé si absent)",
    )
    args = parser.parse_args()

    if not os.path.isfile(args.file):
        print("Fichier introuvable:", args.file)
        sys.exit(1)

    print(f"Envoi {args.file} -> {args.url}")
    result = upload(args.file, args.url, args.retries, args.timeout, args.error_dir)
    if result:
        print("Succès:", result)
    else:
        print("Echec ou aucun oiseau détecté.")


def _safe_filename(base: str) -> str:
    return "".join(c for c in base if c.isalnum() or c in ("-", "_", "."))


def _save_error_html(
    directory: Optional[str], original_name: str, content: str, status: int
) -> None:
    if not directory:
        return
    try:
        os.makedirs(directory, exist_ok=True)
        stem = os.path.splitext(os.path.basename(original_name))[0]
        fname = f"err_{_safe_filename(stem)}_{int(time.time())}_{status}.html"
        path = os.path.join(directory, fname)
        with open(path, "w", encoding="utf-8") as f:  # noqa: P103
            f.write("<html><body><pre>\n")
            f.write(content)
            f.write("\n</pre></body></html>")
        print(f"Erreur sauvegardée: {path}")
    except OSError as e:  # pragma: no cover
        print(f"Impossible de sauvegarder le rapport HTML: {e}")


if __name__ == "__main__":
    main()
