param(
    [string]$BuildDirectory = 'build-windows-msvc',
    [string]$Destination = 'dist/IPR-Windows-x64-20261008',
    [string]$QtRoot = 'C:/Qt/6.11.2/msvc2022_64',
    [string]$OpenCvBin = 'C:/opencv/build/x64/vc16/bin',
    [string]$VcpkgBin = 'C:/dev/vcpkg/installed/x64-windows/bin',
    [switch]$IncludeLocalAdobeProfiles
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$target = [IO.Path]::GetFullPath((Join-Path $project $Destination))
$allowed = [IO.Path]::GetFullPath((Join-Path $project 'dist')) + [IO.Path]::DirectorySeparatorChar
if (-not $target.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Destination must be a new directory inside the project dist folder.'
}
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath ($target + '.zip'))) {
    throw 'Destination already exists. Choose a new name; existing packages are never overwritten.'
}
$binary = Join-Path $project "$BuildDirectory/Release/appIPR.exe"
if (-not (Test-Path -LiteralPath $binary)) { throw 'Build Release/appIPR.exe first.' }
New-Item -ItemType Directory -Path $target | Out-Null
Copy-Item -LiteralPath $binary -Destination $target
$env:PATH = "$QtRoot/bin;$OpenCvBin;$VcpkgBin;" + $env:PATH
& "$QtRoot/bin/windeployqt.exe" --release --compiler-runtime --include-plugins qoffscreen --qmldir "$project/src/qml" "$target/appIPR.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed; incomplete directory retained for diagnosis.' }
Get-ChildItem -LiteralPath $VcpkgBin -Filter '*.dll' | Copy-Item -Destination $target
Get-ChildItem -LiteralPath $OpenCvBin -Filter 'opencv_world*.dll' |
    Where-Object { $_.BaseName -notmatch 'd$' } | Copy-Item -Destination $target
Copy-Item -LiteralPath "$project/config" -Destination $target -Recurse
Copy-Item -LiteralPath "$project/profiles" -Destination $target -Recurse
Copy-Item -LiteralPath "$project/licenses" -Destination $target -Recurse
Copy-Item -LiteralPath "$project/build-canon-normalization-20261008/normalization.json" -Destination "$target/config/raw-normalization.json"
Copy-Item -LiteralPath "$project/docs/architecture/references/note-utilisation-profils-adobe-camera-raw-2026-10-01.md" -Destination "$target/licenses/ADOBE-PROFILES-NOTICE.md"
Copy-Item -LiteralPath "$project/docs/architecture/build-windows-standalone.md" -Destination "$target/LISEZ-MOI.md"
if ($IncludeLocalAdobeProfiles) {
    # Explicit opt-in: private local testing only, never a redistributable payload.
    $adobe = Join-Path $env:ProgramData 'Adobe/CameraRaw'
    $neutral = Join-Path $adobe 'CameraProfiles/Camera/Canon EOS R6 Mark III/Canon EOS R6 Mark III Camera Neutral.dcp'
    Copy-Item -LiteralPath $neutral -Destination "$target/profiles/dcp"
    # Include Canon lens profiles to preserve automatic optical correction
    # without needing an Adobe installation on the test machine.
    Get-ChildItem -LiteralPath "$adobe/LensProfiles/1.0/Canon" -Filter '*.lcp' -Recurse |
        Copy-Item -Destination "$target/profiles/lcp"
}
$inventory = [ordered]@{
    created_utc = [DateTime]::UtcNow.ToString('o')
    architecture = 'Windows x64'
    local_adobe_profiles = [bool]$IncludeLocalAdobeProfiles
    public_distribution_approved = $false
    files = @(Get-ChildItem -LiteralPath $target -Recurse -File | ForEach-Object {
        [ordered]@{path = $_.FullName.Substring($target.Length + 1); bytes = $_.Length;
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
}
$inventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$target/package-manifest.json" -Encoding UTF8
Compress-Archive -Path "$target/*" -DestinationPath ($target + '.zip') -CompressionLevel Optimal
Write-Output "Standalone directory: $target"
Write-Output "Archive: $target.zip"
