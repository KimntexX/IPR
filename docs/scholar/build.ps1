[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$sourceDirectory = Split-Path -Parent $PSCommandPath
$outputDirectory = $sourceDirectory
Set-Location -LiteralPath $sourceDirectory

function Find-TeXExecutable([string]$name) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidate = Join-Path $env:LOCALAPPDATA "Programs\\MiKTeX\\miktex\\bin\\x64\\$name.exe"
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    throw "$name est introuvable. Installez MiKTeX ou TeX Live."
}

$pdflatex = Find-TeXExecutable 'pdflatex'
$bibtex = Find-TeXExecutable 'bibtex'
$env:BIBINPUTS = "$sourceDirectory;$env:BIBINPUTS"
$documents = Get-ChildItem -LiteralPath $sourceDirectory -File -Filter '*.tex' |
    Where-Object { $_.Name -ne 'preamble.tex' } | Sort-Object Name

if ($Clean) {
    $generatedSuffixes = @('.aux', '.bbl', '.blg', '.log', '.out', '.pdf', '.toc', '.synctex.gz')
    foreach ($document in $documents) {
        foreach ($suffix in $generatedSuffixes) {
            $artifact = Join-Path $outputDirectory "$($document.BaseName)$suffix"
            if (Test-Path -LiteralPath $artifact) {
                Remove-Item -LiteralPath $artifact -Force
            }
        }
    }
    exit 0
}

foreach ($document in $documents) {
    & $pdflatex -interaction=nonstopmode -halt-on-error "-output-directory=$outputDirectory" $document.FullName
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Push-Location $outputDirectory
    & $bibtex $document.BaseName
    $status = $LASTEXITCODE
    Pop-Location
    if ($status -ne 0) { exit $status }
    foreach ($pass in 1..2) {
        & $pdflatex -interaction=nonstopmode -halt-on-error "-output-directory=$outputDirectory" $document.FullName
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}
