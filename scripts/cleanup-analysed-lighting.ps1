param(
    [Parameter(Mandatory=$true)][string]$Campaign,
    [Parameter(Mandatory=$true)][ValidateSet('ombre','hl')][string]$Feature,
    [Parameter(Mandatory=$true)][ValidateSet('negative','positive')][string]$Verdict
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$campaignPath = (Resolve-Path -LiteralPath $Campaign).Path
$data = Get-Content -LiteralPath $campaignPath -Raw | ConvertFrom-Json
if ($data.data_version -lt 2 -or $data.missing.Count -ne 0) { throw 'Incomplete canonical memory' }
$records = @($data.records | Where-Object feature -EQ $Feature)
if ($records.Count -ne 96 -or @($records | Where-Object { $_.warnings.Count -gt 0 }).Count -ne 0) {
    throw 'Incomplete or unaudited series; no deletion'
}
$targetRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot ('tests/image/base/' + $Feature)))
$prefix = $targetRoot + [IO.Path]::DirectorySeparatorChar
$targets = @($records | Where-Object { $Verdict -eq 'positive' -or $_.engine -eq 'IPR' })
$checked = @()
foreach ($record in $targets) {
    $path = [IO.Path]::GetFullPath((Join-Path $projectRoot $record.metadata.path))
    if (-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetDirectoryName($path) -ne $targetRoot -or
        [IO.Path]::GetExtension($path) -notin @('.tif','.tiff')) { throw 'Target outside authorized series' }
    $file = Get-Item -LiteralPath $path
    if ($file.Length -ne $record.metadata.bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.metadata.sha256) {
        throw 'Image changed since canonical measurement; no deletion'
    }
    if (-not $record.sampling.zone_counts) { throw 'Missing zone provenance' }
    $checked += [pscustomobject]@{ path=$path; bytes=$file.Length; sha256=$record.metadata.sha256; deleted=$false }
}
if (@($checked.path | Select-Object -Unique).Count -ne $checked.Count) { throw 'Duplicate deletion target' }
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$journal = Join-Path $projectRoot ('docs/reports/storage/lighting-cleanup-' + $stamp)
New-Item -ItemType Directory -Path $journal | Out-Null
$summary = @{ campaign_id=$data.campaign_id; canonical_sha256=(Get-FileHash -LiteralPath $campaignPath).Hash;
    feature=$Feature; verdict=$Verdict; measured_source=$campaignPath; targets=$checked; raw_and_neutrals='preserved';
    deletion='permanent, not recycle bin; hashes cannot restore pixels'; started_at_utc=$stamp }
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $journal 'plan.json') -Encoding UTF8
foreach ($target in $checked) {
    Remove-Item -LiteralPath $target.path
    $target.deleted = $true
    $checked | Export-Csv -LiteralPath (Join-Path $journal 'deletion-log.csv') -NoTypeInformation -Encoding UTF8
}
Write-Host ('Deleted ' + $checked.Count + ' TIFF, bytes=' + ($checked | Measure-Object bytes -Sum).Sum + '; journal=' + $journal)
