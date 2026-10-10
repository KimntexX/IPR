param([switch]$DryRun)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$corpusRoot = Join-Path $projectRoot 'tests/image/base'
$jobs = @(
    @{ folder='lumiere'; features=@('exposure','contrast','blacks','whites'); scenes=@('6V1A0007','6V1A0037','6V1A0043','6V1A0327','6V1A9421','6V1A9950'); count=114 },
    @{ folder='raw'; group='raw'; scenes=@('6V1A9421','6V1A9950'); count=6 },
    @{ folder='courbes'; group='courbes'; scenes=@('6V1A0007','6V1A9421'); count=42 },
    @{ folder='couleur'; group='couleur'; scenes=@('6V1A0043','6V1A0327'); count=42 },
    @{ folder='color-mixer'; group='melangeur'; scenes=@('6V1A0327'); count=169 },
    @{ folder='grading'; group='grading'; scenes=@('6V1A0007','6V1A9421'); count=62 },
    @{ folder='effets'; group='effets'; scenes=@('6V1A0327','6V1A9950'); count=46 },
    @{ folder='bruit'; group='bruit'; scenes=@('6V1A9890','6V1A0043'); count=18 },
    @{ folder='nettete'; group='nettete'; scenes=@('6V1A0327','6V1A0043'); count=28 }
)
# Conservative reservation: 350 MB/export, plus 30 GB kept free. Check again
# before every group. Never overwrite a prior campaign or delete source files.
$remaining = ($jobs | Measure-Object count -Sum).Sum
$reserveBytes = 30GB
$memoryFolder = Join-Path $projectRoot ('docs/validation/raw/generation-photographique-' + $stamp)
if (-not $DryRun) { New-Item -ItemType Directory -Path $memoryFolder | Out-Null }
foreach ($job in $jobs) {
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($corpusRoot))
    if (-not $DryRun -and $drive.AvailableFreeSpace -lt ($remaining * 350000000 + $reserveBytes)) {
        throw 'Insufficient disk space for remaining campaign and 30 GiB reserve. No files deleted.'
    }
    $inputs = @($job.scenes | ForEach-Object { Join-Path $corpusRoot ($_ + '.CR3') })
    $output = Join-Path $corpusRoot ($job.folder + '/generation-' + $stamp)
    $options = @{ Images=$inputs; Output=$output; CameraNeutral=$true; Format='TIFF'; DryRun=$DryRun }
    if ($job.group) { $options.Group=@($job.group) } else { $options.Feature=$job.features }
    Write-Host ('Campaign ' + $job.folder + ': ' + $job.count + ' TIFF')
    & (Join-Path $PSScriptRoot 'generate-test-images.ps1') @options
    if (-not $DryRun) {
        $manifest = Join-Path $output 'manifest.json'
        $data = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
        if ($data.status -ne 'complete' -or $data.exports.Count -ne $job.count) { throw 'Incomplete generation manifest' }
        $data | Add-Member -NotePropertyName campaign_id -NotePropertyValue ($job.folder + '-' + $stamp)
        $data | Add-Member -NotePropertyName schema_version -NotePropertyValue 1
        $data | Add-Member -NotePropertyName data_version -NotePropertyValue 1
        $data | Add-Member -NotePropertyName measured_at_utc -NotePropertyValue $data.finishedUtc
        $data | Add-Member -NotePropertyName protocol_version -NotePropertyValue 'isolated-default-controls-camera-neutral-v1'
        $data | Add-Member -NotePropertyName generator_manifest_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash
        $canonical = Join-Path $memoryFolder ($job.folder + '.json')
        if (Test-Path -LiteralPath $canonical) { throw 'Canonical generation JSON already exists' }
        $data | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $canonical -Encoding UTF8
    }
    $remaining -= $job.count
}
Write-Host ('Campaign finished. Canonical generation manifests: ' + $memoryFolder)
