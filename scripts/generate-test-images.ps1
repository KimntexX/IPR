param(
    [Parameter(Position=0)][ValidateSet('help')][string]$Command,
    [Parameter(Position=1)][string]$HelpTopic = 'all',
    [string]$Help,
    [string[]]$Group = @(),
    [string[]]$Images = @(),
    [string[]]$Feature = @('neutral'),
    [string[]]$Values = @(),
    [string]$Output,
    [ValidateSet('TIFF', 'PNG', 'JPEG')][string]$Format = 'TIFF',
    [int]$Quality = 100,
    [int]$GradingHue = 30,
    [int]$GradingSaturation = 25,
    [int]$SharpeningBase = 50,
    [string]$BuildDirectory,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [switch]$List,
    [switch]$CameraNeutral,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$previousPath = $env:PATH
$previousPluginPath = $env:QT_PLUGIN_PATH

try {
    $projectRoot = Split-Path $PSScriptRoot
    if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build-windows-msvc' }
    $executable = Join-Path $BuildDirectory ($Config + '/iprGenerateTests.exe')
    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw 'Build iprGenerateTests first: bash scripts/build-windows.sh'
    }
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { throw 'Missing CMakeCache.txt' }

    # Same dependencies as IPR. Environment changes affect this invocation only.
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^([^#/:][^:]*):[^=]*=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
    }
    $runtimeDirectories = @()
    if ($cache['Qt6_DIR']) {
        $qtRoot = Split-Path (Split-Path (Split-Path $cache['Qt6_DIR']))
        $runtimeDirectories += Join-Path $qtRoot 'bin'
        $env:QT_PLUGIN_PATH = Join-Path $qtRoot 'plugins'
    }
    if ($cache['OpenCV_DIR']) { $runtimeDirectories += Join-Path (Split-Path $cache['OpenCV_DIR']) 'bin' }
    foreach ($key in @('TIFF_LIBRARY_RELEASE', 'LIBRAW_LIBRARY')) {
        if ($cache[$key]) {
            $libraryDirectory = Split-Path $cache[$key]
            if ((Split-Path $libraryDirectory -Leaf) -eq 'manual-link') { $libraryDirectory = Split-Path $libraryDirectory }
            $runtimeDirectories += Join-Path (Split-Path $libraryDirectory) 'bin'
        }
    }
    $runtimeDirectories = @($runtimeDirectories | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -Unique)
    $env:PATH = ($runtimeDirectories -join ';') + ';' + $env:PATH

    $arguments = @('--feature', ($Feature -join ','), '--format', $Format, '--quality', [string]$Quality,
                   '--grading-hue', [string]$GradingHue, '--grading-saturation', [string]$GradingSaturation,
                   '--sharpening-base', [string]$SharpeningBase)
    if ($List) { $arguments += '--list' }
    if ($CameraNeutral) { $arguments += '--camera-neutral' }
    if ($Group.Count -gt 0) { $arguments += @('--group', ($Group -join ',')) }
    if ($DryRun) { $arguments += '--dry-run' }
    if ($Values.Count -gt 0) { $arguments += '--values=' + ($Values -join ',') }
    if ($Output) { $arguments += @('--output', $Output) }
    foreach ($imagePath in $Images) { $arguments += @('--input', $imagePath) }
    if ($Help -or $Command -eq 'help') {
        $topic = if ($Help) { $Help } else { $HelpTopic }
        $arguments = @('--help-group', $topic)
    }
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) { throw "Generation failed (exit $LASTEXITCODE). See message above." }
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPluginPath
}
