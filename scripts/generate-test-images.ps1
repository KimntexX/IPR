param(
    [Parameter(Mandatory=$true, Position=0)]
    [ValidateSet('all', 'raw', 'lumiere', 'courbes', 'couleur', 'melangeur',
                 'grading', 'effets', 'bruit', 'nettete', 'optique')]
    [string]$Group
)
$ErrorActionPreference = 'Stop'
if ($args.Count -gt 0) { throw 'Provide only one group, or all.' }
$previousPath = $env:PATH
$previousPluginPath = $env:QT_PLUGIN_PATH

try {
    $projectRoot = Split-Path $PSScriptRoot
    $BuildDirectory = Join-Path $projectRoot 'build-windows-msvc'
    $Config = 'Release'
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

    Push-Location -LiteralPath $projectRoot
    try { & $executable $Group.ToLowerInvariant() } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "Generation failed (exit $LASTEXITCODE). No files deleted; see message above." }
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPluginPath
}
