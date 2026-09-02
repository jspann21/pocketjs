param([string]$Mount = "F:\")

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$before = Get-HandoffStatus -Mount $root
if ($before.backupExists -or $null -ne $before.state) {
    Write-Host "Restoring the verified original Rockbox image..."
    & python "$PSScriptRoot\handoff.py" restore --mount $root
    if ($LASTEXITCODE -ne 0) { throw "handoff restore failed with exit code $LASTEXITCODE" }
} else {
    Write-Host "Rockbox handoff is already restored."
}
Restore-PersistenceTransaction -Mount $root
$after = Get-HandoffStatus -Mount $root
if (-not $after.targetExists -or $after.backupExists -or $null -ne $after.state) {
    throw "Rockbox restore verification failed. Do not disconnect the iPod."
}
$paths = Get-PersistencePaths -Mount $root
if ((Test-Path -LiteralPath $paths.Transaction) -or (Test-Path -LiteralPath $paths.Backup)) {
    throw "Persistence restore verification failed. Do not disconnect the iPod."
}
$result = [ordered]@{
    schema = 1
    restoredAt = (Get-Date).ToString("o")
    mount = $root
    targetSha256 = $after.targetSha256
    backupExists = $after.backupExists
    state = $after.state
    staleStateFiles = $after.staleStateFiles
    persistenceTransactionExists = $false
}
$outputPath = Join-Path $PSScriptRoot "RESTORE-STATUS.json"
Write-JsonAtomic -Path $outputPath -Value $result
Write-Host "RESTORE VERIFIED"
Write-Host "  Original Rockbox SHA-256: $($after.targetSha256)"
Write-Host "  Saved: $outputPath"
Write-Host "Safely eject and confirm that Rockbox boots normally."
