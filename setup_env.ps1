# Usage:  PowerShell ->  Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass; ./setup_env.ps1
Write-Host '--- Birdwatch Server environment setup ---'
if (-not (Test-Path .venv)) {
    Write-Host 'Creating virtual environment (.venv)...'
    python -m venv .venv
}
Write-Host 'Activating virtual environment'
. ./.venv/Scripts/Activate.ps1
Write-Host 'Python:' (Get-Command python).Source
Write-Host 'Installing requirements...'
pip install --upgrade pip > $null
pip install -r birdwatch_server/requirements.txt
Write-Host 'Django version:' (python -c "import django; print(django.get_version())")
Write-Host 'Done. Next steps:'
Write-Host '  cd birdwatch_server'
Write-Host '  python manage.py migrate'
Write-Host '  python manage.py runserver 0.0.0.0:8000'
