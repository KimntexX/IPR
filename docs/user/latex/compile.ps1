[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskSourceDirectory = Split-Path -Parent $PSCommandPath
$taskOutputDirectory = Join-Path $taskSourceDirectory 'build'
$taskTeX = Get-Command pdflatex -ErrorAction SilentlyContinue
if ($taskTeX) {
    $taskExecutable = $taskTeX.Source
} else {
    $taskExecutable = Join-Path $env:LOCALAPPDATA 'Programs/MiKTeX/miktex/bin/x64/pdflatex.exe'
    if (-not (Test-Path -LiteralPath $taskExecutable)) {
        throw 'pdflatex introuvable : installer MiKTeX ou TeX Live.'
    }
}
New-Item -ItemType Directory -Force -Path $taskOutputDirectory | Out-Null
Push-Location -LiteralPath $taskSourceDirectory
try {
    foreach ($taskPass in 1..3) {
        & $taskExecutable -interaction=nonstopmode -halt-on-error "-output-directory=$taskOutputDirectory" ipr-livre-utilisateur.tex
        if ($LASTEXITCODE -ne 0) { throw "Échec LaTeX à la passe $taskPass (code $LASTEXITCODE)." }
    }
    Write-Output "PDF : $taskOutputDirectory/ipr-livre-utilisateur.pdf"
} finally {
    Pop-Location
}
