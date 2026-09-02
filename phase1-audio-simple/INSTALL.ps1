param([string]$Mount = "F:\")

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidate = Join-Path $PSScriptRoot $script:CandidateName
Assert-ExpectedHash -Path $candidate -Expected $script:CandidateSha256 | Out-Null
foreach ($entry in $script:StagedFiles.GetEnumerator()) {
    Assert-ExpectedHash -Path (Join-Path $PSScriptRoot $entry.Key) -Expected $entry.Value | Out-Null
}

try {
    Write-Host "Backing up PocketJS files and Rockbox..."
    Start-LineageTransaction -Mount $root | Out-Null
    Write-Host "Installing the verified PocketJS image and backing up Rockbox..."
    & python "$PSScriptRoot\handoff.py" install --mount $root --probe $candidate
    if ($LASTEXITCODE -ne 0) { throw "handoff install failed with exit code $LASTEXITCODE" }
    & "$PSScriptRoot\VERIFY.ps1" -Mount $root -OutputName "INSTALL-STATUS.json"
    Write-Host ""
    Write-Host "INSTALL COMPLETE. Safely eject, boot, and follow RESULTS.txt."
} catch {
    Write-Warning "Install failed. Attempting an exact rollback."
    try {
        $status = Get-HandoffStatus -Mount $root
        if ($status.backupExists -or $null -ne $status.state) {
            & python "$PSScriptRoot\handoff.py" restore --mount $root
        }
    } catch { Write-Warning "Automatic Rockbox rollback failed: $($_.Exception.Message)" }
    try { Restore-LineageTransaction -Mount $root } catch {
        Write-Warning "Automatic lineage rollback failed: $($_.Exception.Message)"
    }
    throw
}
