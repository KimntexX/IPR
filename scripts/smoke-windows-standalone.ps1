param([string]$PackageDirectory = 'dist/IPR-Windows-x64-20261008')
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = Join-Path $package 'appIPR.exe'
$start.WorkingDirectory = $env:TEMP
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardError = $true
$start.RedirectStandardOutput = $true
$start.EnvironmentVariables['PATH'] = "$env:SystemRoot/System32;$env:SystemRoot"
foreach ($name in @('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH',
                    'IPR_RAW_NORMALIZATION_MANIFEST', 'IPR_ADOBE_CAMERARAW_DIR')) {
    $start.EnvironmentVariables.Remove($name)
}
$start.EnvironmentVariables['QT_QPA_PLATFORM'] = 'offscreen'
$start.EnvironmentVariables['QT_QUICK_BACKEND'] = 'software'
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $start
try {
    if (-not $process.Start()) { throw 'Application could not start.' }
    if ($process.WaitForExit(8000)) {
        throw ('Application exited prematurely: ' + $process.StandardError.ReadToEnd())
    }
    $external = @($process.Modules | Where-Object {
        $_.ModuleName -match '^(Qt6|opencv|raw|tiff|lcms|jpeg|z\.)' -and
        -not $_.FileName.StartsWith($package + '\', [StringComparison]::OrdinalIgnoreCase)
    } | ForEach-Object FileName)
    $process.Kill()
    $process.WaitForExit()
    $errorLog = $process.StandardError.ReadToEnd()
    if ($external.Count -gt 0) { throw ('External runtime libraries: ' + ($external -join ', ')) }
    if ($errorLog -match 'failed to load|is not installed|Type .* unavailable|ReferenceError|Error:') {
        throw $errorLog
    }
    [ordered]@{startup = 'passed'; runtime_search_path = 'Windows system directories only';
        working_directory = 'temporary directory, outside repository';
        external_runtime_libraries = $external; stderr = $errorLog;
        scope = 'offscreen startup only, not a clean-machine or real-RAW validation'} |
        ConvertTo-Json -Depth 3
} finally {
    if ($process.Id -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
