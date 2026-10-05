[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$docsRoot = Split-Path -Parent $PSCommandPath
$arguments = if ($Clean) { @('-Clean') } else { @() }

foreach ($script in @(
    (Join-Path $docsRoot 'technical\latex\build.ps1'),
    (Join-Path $docsRoot 'scholar\build.ps1')
)) {
    & powershell.exe -ExecutionPolicy Bypass -File $script @arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
