param(
    [string]$BuildDirectory = 'build-ci',
    [string]$OutputDirectory = 'dist',
    [ValidateSet('system','standalone')][string]$Mode = 'standalone',
    [string]$Version = '0.1.0-alpha',
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [string[]]$RuntimeDirectory = @(),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$ciArgs = @("$PSScriptRoot/ci.py", '--action', 'package', '--build-dir', $BuildDirectory,
    '--output-dir', $OutputDirectory, '--mode', $Mode, '--version', $Version)
if ($QtRoot) { $ciArgs += @('--qt-root', $QtRoot) }
foreach ($directory in $RuntimeDirectory) { $ciArgs += @('--runtime-dir', $directory) }
& $Python @ciArgs
if ($LASTEXITCODE) { throw "Packaging failed ($LASTEXITCODE)" }
