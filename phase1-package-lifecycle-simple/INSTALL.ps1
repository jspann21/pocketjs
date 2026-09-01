param([string]$Mount = "F:\")

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidate = Join-Path $PSScriptRoot $script:CandidateName
Assert-ExpectedHash -Path $candidate -Expected $script:CandidateSha256 | Out-Null
Assert-ExpectedHash -Path (Join-Path $PSScriptRoot "APP.PKT") -Expected $script:ActiveSha256 | Out-Null
Assert-ExpectedHash -Path (Join-Path $PSScriptRoot "ACTIVE.PKT") -Expected $script:ActiveSha256 | Out-Null
Assert-ExpectedHash -Path (Join-Path $PSScriptRoot "PENDING.PKT") -Expected $script:PendingSha256 | Out-Null

$paths = $null
try {
    Write-Host "Backing up PocketJS package slots..."
    $paths = Start-PackageTransaction -Mount $root
    Stage-ActivePhase -Paths $paths

    Write-Host "Installing the verified PocketJS firmware and backing up Rockbox..."
    & python "$PSScriptRoot\handoff.py" install --mount $root --probe $candidate
    if ($LASTEXITCODE -ne 0) { throw "handoff install failed with exit code $LASTEXITCODE" }

    & "$PSScriptRoot\VERIFY.ps1" -Mount $root -Phase active -OutputName "INSTALL-STATUS.json"
    Write-Host ""
    Write-Host "INSTALL COMPLETE. Safely eject, boot once, and follow RESULTS.txt."
} catch {
    Write-Warning "Install did not complete. Attempting an exact rollback."
    try {
        $status = Get-HandoffStatus -Mount $root
        if ($status.backupExists -or $null -ne $status.state) {
            & python "$PSScriptRoot\handoff.py" restore --mount $root
        }
    } catch {
        Write-Warning "Automatic Rockbox rollback failed: $($_.Exception.Message)"
    }
    try {
        if ($null -ne $paths) { Restore-PackageTransaction -Mount $root }
    } catch {
        Write-Warning "Automatic package rollback failed: $($_.Exception.Message)"
    }
    throw
}
