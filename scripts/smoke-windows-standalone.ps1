param([Parameter(Mandatory=$true)][string]$PackageDirectory, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
& $Python "$PSScriptRoot/smoke_package.py" $PackageDirectory --mode standalone
if ($LASTEXITCODE) { throw "Standalone smoke failed ($LASTEXITCODE)" }
