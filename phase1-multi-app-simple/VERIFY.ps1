param(
    [string]$Mount = "F:\",
    [string]$OutputName = "VERIFY-STATUS.json"
)

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidate = Join-Path $PSScriptRoot $script:CandidateName
$candidateHash = Assert-ExpectedHash -Path $candidate -Expected $script:CandidateSha256
$handoff = Assert-HandoffInstalled -Mount $root
$paths = Get-MultiPaths -Mount $root
$state = Get-MultiState -Paths $paths
if ($state.phase -ne "staged") { throw "Multi-app state says '$($state.phase)', expected 'staged'." }
Assert-StagedFiles -Paths $paths

$result = [ordered]@{
    schema = 1
    verifiedAt = (Get-Date).ToString("o")
    mount = $root
    candidate = [ordered]@{ file = $script:CandidateName; sha256 = $candidateHash }
    launcherSha256 = $script:LauncherSha256
    apps = [ordered]@{ "ALPHA.PKT" = $script:AlphaSha256; "BETA.PKT" = $script:BetaSha256 }
    handoff = $handoff
}
$outputPath = Join-Path $PSScriptRoot $OutputName
Write-JsonAtomic -Path $outputPath -Value $result

Write-Host "VERIFIED"
Write-Host "  PocketJS image: $candidateHash"
Write-Host "  Launcher:       $script:LauncherSha256"
Write-Host "  Rockbox backup: $($handoff.backupSha256)"
Write-Host "  Saved:          $outputPath"
