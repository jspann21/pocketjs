param([string]$Mount = "F:\", [string]$OutputName = "VERIFY-STATUS.json")

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidateHash = Assert-ExpectedHash -Path (Join-Path $PSScriptRoot $script:CandidateName) -Expected $script:CandidateSha256
$paths = Get-PersistencePaths -Mount $root
$state = Get-PersistenceState -Paths $paths
if ($state.phase -ne "staged") { throw "Persistence transaction is not staged." }
Assert-StagedPersistence -Paths $paths
$handoff = Assert-HandoffInstalled -Mount $root
$result = [ordered]@{
    schema = 1
    verifiedAt = (Get-Date).ToString("o")
    mount = $root
    candidate = [ordered]@{ file = $script:CandidateName; sha256 = $candidateHash }
    appSha256 = $script:AppSha256
    state0Sha256 = $script:State0Sha256
    state1Sha256 = $script:State1Sha256
    handoff = $handoff
}
$outputPath = Join-Path $PSScriptRoot $OutputName
Write-JsonAtomic -Path $outputPath -Value $result
Write-Host "VERIFIED"
Write-Host "  PocketJS image: $candidateHash"
Write-Host "  State slots:    512 bytes each, fixed and preallocated"
Write-Host "  Rockbox backup: $($handoff.backupSha256)"
Write-Host "  Saved:          $outputPath"
