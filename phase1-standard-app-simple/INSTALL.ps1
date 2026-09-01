param([string]$Mount = "F:\")

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
$candidate = Join-Path $PSScriptRoot $script:CandidateName
$package = Join-Path $PSScriptRoot "APP.PKT"
Assert-ExpectedHash -Path $candidate -Expected $script:CandidateSha256 | Out-Null
Assert-ExpectedHash -Path $package -Expected $script:AppSha256 | Out-Null

$appDirectory = Join-Path $root "POCKETJS"
$appPath = Join-Path $appDirectory "APP.PKT"
$backupPath = Join-Path $appDirectory $script:AppBackupName
$statePath = Join-Path $appDirectory $script:AppStateName
New-Item -ItemType Directory -Path $appDirectory -Force | Out-Null

if (Test-Path -LiteralPath $statePath) {
    throw "An APP.PKT transaction is already active. Run .\RESTORE.ps1 first."
}
if (Test-Path -LiteralPath $backupPath) {
    throw "An untracked APP.PKT backup already exists: $backupPath"
}

$hadOriginal = Test-Path -LiteralPath $appPath -PathType Leaf
$originalSha256 = $null
try {
    if ($hadOriginal) {
        $originalSha256 = Get-LowerSha256 -Path $appPath
        Copy-Item -LiteralPath $appPath -Destination $backupPath
        Assert-ExpectedHash -Path $backupPath -Expected $originalSha256 | Out-Null
    }

    $appState = [ordered]@{
        schema = 1
        hadOriginal = $hadOriginal
        originalSha256 = $originalSha256
        candidateSha256 = $script:AppSha256
        createdAt = (Get-Date).ToString("o")
    }
    $stateTemporary = "$statePath.new"
    $appState | ConvertTo-Json | Set-Content -LiteralPath $stateTemporary -Encoding utf8
    Move-Item -LiteralPath $stateTemporary -Destination $statePath -Force
    Copy-VerifiedFile -Source $package -Destination $appPath -ExpectedSha256 $script:AppSha256

    Write-Host "Installing the verified PocketJS image and creating a verified Rockbox backup..."
    & python "$PSScriptRoot\handoff.py" install --mount $root --probe $candidate
    if ($LASTEXITCODE -ne 0) {
        throw "handoff install failed with exit code $LASTEXITCODE"
    }

    & "$PSScriptRoot\VERIFY.ps1" -Mount $root -OutputName "INSTALL-STATUS.json"
    Write-Host ""
    Write-Host "INSTALL COMPLETE. Safely eject the iPod, boot it, and follow RESULTS.txt."
} catch {
    Write-Warning "Install did not complete. Attempting to restore Rockbox and APP.PKT."
    try {
        $status = Get-HandoffStatus -Mount $root
        if ($status.backupExists -or $null -ne $status.state) {
            & python "$PSScriptRoot\handoff.py" restore --mount $root
        }
    } catch {
        Write-Warning "Automatic Rockbox rollback also failed: $($_.Exception.Message)"
    }
    try {
        Restore-AppPackage -Mount $root
    } catch {
        Write-Warning "Automatic APP.PKT rollback also failed: $($_.Exception.Message)"
    }
    throw
}

