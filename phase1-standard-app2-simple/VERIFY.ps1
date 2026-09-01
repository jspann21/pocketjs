param(
    [string]$Mount = "F:\",
    [string]$OutputName = "VERIFY-STATUS.json"
)

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidate = Join-Path $PSScriptRoot $script:CandidateName
$package = Join-Path $PSScriptRoot "APP.PKT"
$candidateHash = Assert-ExpectedHash -Path $candidate -Expected $script:CandidateSha256
$packageHash = Assert-ExpectedHash -Path $package -Expected $script:AppSha256
$appPath = Join-Path (Join-Path $root "POCKETJS") "APP.PKT"
$deviceAppHash = Assert-ExpectedHash -Path $appPath -Expected $script:AppSha256
$status = Get-HandoffStatus -Mount $root

if (-not $status.targetExists -or $status.targetSha256 -ne $script:CandidateSha256) {
    throw "The mounted rockbox.ipod does not match this candidate."
}
if (-not $status.backupExists -or $null -eq $status.state) {
    throw "The verified Rockbox backup/state pair is missing. Do not boot; inspect the volume."
}
if ($status.backupSha256 -ne $status.state.original.sha256) {
    throw "The Rockbox backup hash does not match the recorded original hash."
}
if ($status.state.probe.sha256 -ne $script:CandidateSha256) {
    throw "The handoff state belongs to a different PocketJS candidate."
}

$result = [ordered]@{
    schema = 1
    verifiedAt = (Get-Date).ToString("o")
    mount = $root
    candidate = [ordered]@{
        file = $script:CandidateName
        sha256 = $candidateHash
    }
    appPackage = [ordered]@{
        localSha256 = $packageHash
        deviceSha256 = $deviceAppHash
    }
    handoff = $status
}
$outputPath = Join-Path $PSScriptRoot $OutputName
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputPath -Encoding utf8

Write-Host "VERIFIED"
Write-Host "  PocketJS image: $candidateHash"
Write-Host "  APP.PKT:        $deviceAppHash"
Write-Host "  Rockbox backup: $($status.backupSha256)"
Write-Host "  Saved:          $outputPath"
