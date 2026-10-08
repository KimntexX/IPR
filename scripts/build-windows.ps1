param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$PlanPath
)
$ErrorActionPreference = 'Stop'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

try {
    $plan = Get-Content -Raw -Encoding UTF8 -LiteralPath $PlanPath | ConvertFrom-Json
    if (-not $plan.tests_only) {
        $expectedExe = [IO.Path]::GetFullPath((Join-Path $BuildDirectory ($plan.config + '/appIPR.exe')))
        foreach ($process in @(Get-Process appIPR -ErrorAction SilentlyContinue)) {
            if ($process.Path -eq $expectedExe) { throw 'Close appIPR.exe before building, or use --tests-only.' }
        }
    }
    $configure = @('-S', $ProjectRoot, '-B', $BuildDirectory, '-DBUILD_TESTING=ON')
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
        $configure += @('-G', 'Visual Studio 17 2022', '-A', 'x64')
    }
    $configure += @($plan.configure_args)
    Invoke-Checked 'cmake' $configure

    # Only child processes inherit this runtime search path; no global installation changes.
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt')) {
        if ($line -match '^([^#/:][^:]*):[^=]*=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
    }
    $runtimeDirectories = @()
    if ($cache['Qt6_DIR']) {
        $qtRoot = Split-Path (Split-Path (Split-Path $cache['Qt6_DIR']))
        $runtimeDirectories += Join-Path $qtRoot 'bin'
        $env:QT_PLUGIN_PATH = Join-Path $qtRoot 'plugins'
        $env:QML_IMPORT_PATH = Join-Path $qtRoot 'qml'
        $env:QML2_IMPORT_PATH = $env:QML_IMPORT_PATH
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
    $buildArguments = @('--build', $BuildDirectory, '--config', $plan.config, '-j', [string]$plan.jobs)
    if ($plan.full -and -not $plan.tests_only) {
        Invoke-Checked 'cmake' $buildArguments  # All targets, including optional integration targets.
    } else {
        $targets = @($plan.targets)
        # Both executables statically link the engine. Rebuild the generator as
        # well, even for a targeted application change, to avoid stale exports.
        if (-not $plan.tests_only) { $targets += @('appIPR', 'iprGenerateTests') }
        if ($targets.Count -gt 0) { Invoke-Checked 'cmake' ($buildArguments + @('--target') + $targets) }
    }
    if (@($plan.tests).Count -eq 0) { Write-Output 'No CTest tests selected.'; exit 0 }
    $listing = & ctest --test-dir $BuildDirectory -C $plan.config --show-only=json-v1
    if ($LASTEXITCODE -ne 0) { throw 'CTest discovery failed' }
    $available = @((($listing -join "`n") | ConvertFrom-Json).tests | ForEach-Object name)
    foreach ($test in $plan.tests) {
        if ($test -notin $available) { throw "Requested test not registered: $test" }
    }
    $testArguments = @('--test-dir', $BuildDirectory, '-C', $plan.config, '--output-on-failure')
    if (-not $plan.full) {
        $expression = '^(' + (($plan.tests | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
        $testArguments += @('-R', $expression)
    }
    Invoke-Checked 'ctest' $testArguments
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
