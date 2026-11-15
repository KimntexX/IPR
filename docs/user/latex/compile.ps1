[CmdletBinding()]
param([ValidateSet('fr', 'en', 'all')][string]$Language = 'all')
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
    $taskDocuments = @()
    if ($Language -in @('fr', 'all')) { $taskDocuments += 'ipr-livre-utilisateur' }
    if ($Language -in @('en', 'all')) { $taskDocuments += 'ipr-livre-utilisateur_en' }
    foreach ($taskDocument in $taskDocuments) {
        foreach ($taskPass in 1..3) {
            & $taskExecutable -interaction=nonstopmode -halt-on-error "-output-directory=$taskOutputDirectory" "$taskDocument.tex"
            if ($LASTEXITCODE -ne 0) { throw "Échec LaTeX $taskDocument à la passe $taskPass (code $LASTEXITCODE)." }
        }
        Write-Output "PDF : $taskOutputDirectory/$taskDocument.pdf"
    }
} finally {
    Pop-Location
}
