# go.ps1 - SEQUENCE UNIQUE ET STABLE : recharge la branche courante, build,
# moteur de tests, puis lance l'exe.
#
# Usage (depuis n'importe ou, les chemins sont absolus) :
#   .\go.ps1              -> reload + build + tests + lancement de l'exe
#   .\go.ps1 -Tests       -> reload + build + tests, SANS lancer l'exe
#   .\go.ps1 -SansReload  -> build + tests + exe sans toucher au depot
#
# La branche rechargee = la branche COURANTE du depot (git la detecte tout
# seul) : ce script ne se modifie JAMAIS d'un lot a l'autre.
#
# Premiere utilisation seulement, si PowerShell refuse d'executer les
# scripts locaux :
#   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

param([switch]$Tests, [switch]$SansReload)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root
$exeTests = Join-Path $root "desktop\build\Debug\sketch3d_tests.exe"
$exeApp   = Join-Path $root "desktop\build\Debug\sketch3d.exe"

if (-not $SansReload) {
    $br = (git rev-parse --abbrev-ref HEAD).Trim()
    Write-Host "== depot : reload de la branche $br (les changements locaux non committes sont PERDUS)"
    git fetch origin
    git reset --hard "origin/$br"
}

Write-Host "== build (Debug)"
cmake --build desktop\build --config Debug --target sketch3d_tests sketch3d
if ($LASTEXITCODE -ne 0) { Write-Host "== BUILD ECHEC" -ForegroundColor Red; exit 1 }

Write-Host "== moteur de tests"
& $exeTests
if ($LASTEXITCODE -ne 0) { Write-Host "== TESTS ECHEC" -ForegroundColor Red; exit 1 }
Write-Host "== moteur de tests : OK (la ligne resume ci-dessus doit dire 0 echec)"

if (-not $Tests) {
    Write-Host ""
    Write-Host "==============================================================="
    Write-Host "== LANCEMENT DE L'APPLICATION - CE N'EST PAS UN BLOCAGE       =="
    Write-Host "== la console attendra la FERMETURE de la fenetre Sketch3D   =="
    Write-Host "== (le titre de la fenetre doit montrer le build du jour)    =="
    Write-Host "==============================================================="
    & $exeApp
}
