param(
    [string]$Mount = "F:\",
    [ValidateSet("active", "embedded")][string]$Phase = "active",
    [string]$OutputName = "VERIFY-STATUS.json"
)

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidate = Join-Path $PSScriptRoot $script:CandidateName
$candidateHash = Assert-ExpectedHash -Path $candidate -Expected $script:CandidateSha256
$handoff = Assert-HandoffInstalled -Mount $root
$paths = Get-PackagePaths -Mount $root
$slots = Assert-PackagePhase -Paths $paths -Phase $Phase

$result = [ordered]@{
    schema = 1
    verifiedAt = (Get-Date).ToString("o")
    mount = $root
    phase = $Phase
    candidate = [ordered]@{ file = $script:CandidateName; sha256 = $candidateHash }
    packageSlots = $slots
    handoff = $handoff
}
$outputPath = Join-Path $PSScriptRoot $OutputName
Write-JsonAtomic -Path $outputPath -Value $result

Write-Host "VERIFIED: $Phase phase"
Write-Host "  PocketJS image: $candidateHash"
Write-Host "  Rockbox backup: $($handoff.backupSha256)"
Write-Host "  Saved:          $outputPath"
