$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$env:PYTHONDONTWRITEBYTECODE = "1"

$script:CandidateName = "pocketjs-a1099-phase1-hostops-hold2.ipod"
$script:CandidateSha256 = "6babe891ac53f09a0a3c71da1e14849713f6bc60136bad73571862e4bdcdb3f7"
$script:AppSha256 = "fc5cc221f6c6c1a951d3657eabf832ec5c0b906c703d5b73dfac0a0cfe3ff579"
$script:AppStateName = ".pocketjs-simple-app-state.json"
$script:AppBackupName = "APP.PKT.pocketjs-backup"

function Get-LowerSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing file: $Path"
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-ExpectedHash {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Expected
    )
    $actual = Get-LowerSha256 -Path $Path
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path`nExpected: $Expected`nActual:   $actual"
    }
    return $actual
}

function Get-IPodRoot {
    param([Parameter(Mandatory = $true)][string]$Mount)
    if (-not (Test-Path -LiteralPath $Mount -PathType Container)) {
        throw "iPod volume is not mounted at $Mount"
    }
    $item = Get-Item -LiteralPath $Mount
    $resolved = [System.IO.Path]::GetFullPath($item.FullName).TrimEnd("\")
    $driveRoot = [System.IO.Path]::GetFullPath($item.PSDrive.Root).TrimEnd("\")
    if ($resolved -ne $driveRoot) {
        throw "Pass the iPod drive root (for example F:\), not a subdirectory: $resolved"
    }
    $directRockbox = Join-Path $resolved "rockbox.ipod"
    $nestedRockbox = Join-Path (Join-Path $resolved ".rockbox") "rockbox.ipod"
    if (-not (Test-Path -LiteralPath $directRockbox -PathType Leaf) -and
        -not (Test-Path -LiteralPath $nestedRockbox -PathType Leaf)) {
        throw "No rockbox.ipod was found at the root or in .rockbox on $resolved"
    }
    return "$resolved\"
}

function Assert-Python {
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        throw "Python was not found. Install Python 3 or add python.exe to PATH."
    }
    $version = & python --version 2>&1
    if ($LASTEXITCODE -ne 0 -or "$version" -notmatch "Python 3\.") {
        throw "python must run Python 3; got: $version"
    }
}

function Get-HandoffStatus {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $text = & python "$PSScriptRoot\handoff.py" status --mount $Mount 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "handoff status failed:`n$text"
    }
    try {
        return $text | ConvertFrom-Json
    } catch {
        throw "handoff status returned invalid JSON:`n$text"
    }
}

function Copy-VerifiedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256
    )
    $temporary = "$Destination.pocketjs-new"
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $Source -Destination $temporary -Force
    Assert-ExpectedHash -Path $temporary -Expected $ExpectedSha256 | Out-Null
    Move-Item -LiteralPath $temporary -Destination $Destination -Force
    Assert-ExpectedHash -Path $Destination -Expected $ExpectedSha256 | Out-Null
}

function Restore-AppPackage {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $root = Get-IPodRoot -Mount $Mount
    $appDirectory = Join-Path $root "POCKETJS"
    $appPath = Join-Path $appDirectory "APP.PKT"
    $backupPath = Join-Path $appDirectory $script:AppBackupName
    $statePath = Join-Path $appDirectory $script:AppStateName
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        Write-Host "No PocketJS app-package transaction is active."
        return
    }

    $state = Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
    if ($state.hadOriginal) {
        if (-not (Test-Path -LiteralPath $backupPath -PathType Leaf)) {
            throw "APP.PKT backup is missing: $backupPath"
        }
        $expected = [string]$state.originalSha256
        Assert-ExpectedHash -Path $backupPath -Expected $expected | Out-Null
        Copy-VerifiedFile -Source $backupPath -Destination $appPath -ExpectedSha256 $expected
        Remove-Item -LiteralPath $backupPath -Force
        Write-Host "Restored the original $appPath"
    } else {
        Remove-Item -LiteralPath $appPath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
        Write-Host "Removed the staged APP.PKT; no APP.PKT existed before this test."
    }
    Remove-Item -LiteralPath $statePath -Force
}
