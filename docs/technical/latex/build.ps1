[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$source = 'ipr-documentation-technique.tex'
$documentName = [System.IO.Path]::GetFileNameWithoutExtension($source)
$outputDirectory = 'build'
$sourceDirectory = Split-Path -Parent $PSCommandPath
Set-Location -LiteralPath $sourceDirectory

function Find-TeXExecutable([string]$name) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidate = Join-Path $env:LOCALAPPDATA "Programs\\MiKTeX\\miktex\\bin\\x64\\$name.exe"
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }

    throw "$name est introuvable. Installez MiKTeX ou TeX Live, puis relancez cette commande."
}

$pdflatex = Find-TeXExecutable 'pdflatex'
$bibtex = Find-TeXExecutable 'bibtex'

if ($Clean) {
    if (Test-Path -LiteralPath $outputDirectory) {
        Remove-Item -LiteralPath $outputDirectory -Recurse -Force
    }
    exit 0
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$env:BIBINPUTS = "$sourceDirectory;$env:BIBINPUTS"

& $pdflatex -interaction=nonstopmode -halt-on-error "-output-directory=$outputDirectory" $source
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Push-Location $outputDirectory
& $bibtex $documentName
$status = $LASTEXITCODE
Pop-Location
if ($status -ne 0) { exit $status }

foreach ($pass in 1..2) {
    & $pdflatex -interaction=nonstopmode -halt-on-error "-output-directory=$outputDirectory" $source
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
