param([string]$Mount = "F:\")

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidate = Join-Path $PSScriptRoot $script:CandidateName
Assert-ExpectedHash -Path $candidate -Expected $script:CandidateSha256 | Out-Null
Assert-ExpectedHash -Path (Join-Path $PSScriptRoot "APP.PKT") -Expected $script:FallbackSha256 | Out-Null
Assert-ExpectedHash -Path (Join-Path $PSScriptRoot "LAUNCHER.PKT") -Expected $script:LauncherSha256 | Out-Null
Assert-ExpectedHash -Path (Join-Path $PSScriptRoot "ALPHA.PKT") -Expected $script:AlphaSha256 | Out-Null
Assert-ExpectedHash -Path (Join-Path $PSScriptRoot "BETA.PKT") -Expected $script:BetaSha256 | Out-Null

$paths = $null
try {
    Write-Host "Backing up the existing launcher and APPS tree..."
    $paths = Start-MultiTransaction -Mount $root
    Stage-MultiApps -Paths $paths

    Write-Host "Installing the verified PocketJS image and backing up Rockbox..."
    & python "$PSScriptRoot\handoff.py" install --mount $root --probe $candidate
    if ($LASTEXITCODE -ne 0) { throw "handoff install failed with exit code $LASTEXITCODE" }

    & "$PSScriptRoot\VERIFY.ps1" -Mount $root -OutputName "INSTALL-STATUS.json"
    Write-Host ""
    Write-Host "INSTALL COMPLETE. Safely eject, boot, and follow RESULTS.txt."
} catch {
    Write-Warning "Install did not complete. Attempting an exact rollback."
    try {
        $status = Get-HandoffStatus -Mount $root
        if ($status.backupExists -or $null -ne $status.state) {
            & python "$PSScriptRoot\handoff.py" restore --mount $root
        }
    } catch { Write-Warning "Automatic Rockbox rollback failed: $($_.Exception.Message)" }
    try {
        if ($null -ne $paths -or (Test-Path -LiteralPath (Join-Path $root $script:StateName))) {
            Restore-MultiTransaction -Mount $root
        }
    } catch { Write-Warning "Automatic package rollback failed: $($_.Exception.Message)" }
    throw
}
