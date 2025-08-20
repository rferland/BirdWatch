# Usage:  PowerShell ->  Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass; ./setup_env.ps1
# Write-Host '--- Birdwatch Server environment setup ---'
# if (-not (Test-Path .venv)) {
#     Write-Host 'Creating virtual environment (.venv)...'
#     python -m venv .venv
# }
Write-Host 'Activating virtual environment'
if ($IsWindows) {
    # Windows: utilise le chemin relatif
    $venvPath = "..\.venv\Scripts\Activate.ps1"
    if (Test-Path $venvPath) {
        . $venvPath
    }
    else {
        Write-Host "Erreur: $venvPath introuvable."
        exit 1
    }install --upgrade pip
}
else {
    # Linux: utilise le chemin relatif
    $venvPath = "../.venv/bin/activate"
    if (Test-Path $venvPath) {
        Write-Host "Pour Linux, exécutez: source $venvPath"
        Write-Host "(Ce script PowerShell ne peut pas activer un venv bash, faites-le manuellement.)"
        exit 0
    }
    else {
        Write-Host "Erreur: $venvPath introuvable."
        exit 1
    }
}
Write-Host 'Python:' (Get-Command python).Source
Write-Host 'Installing requirements...'
pip install --upgrade pip > $null
pip install -r birdwatch_server/requirements.txt
Write-Host 'Django version:' (python -c "import django; print(django.get_version())")
Write-Host 'Done. Next steps:'
Write-Host '  cd birdwatch_server'
Write-Host '  python manage.py migrate'
Write-Host '  python manage.py runserver 0.0.0.0:8000'
Write-Host ''
Write-Host 'Appuyez sur une touche pour fermer...'
[void][System.Console]::ReadKey($true)
