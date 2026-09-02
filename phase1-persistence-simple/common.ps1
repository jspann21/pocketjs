$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$env:PYTHONDONTWRITEBYTECODE = "1"

$script:CandidateName = "pocketjs-a1099-phase1-persistence.ipod"
$script:CandidateSha256 = "8c5eea4d49e907cb09f88d6cce8ddd2ae00562c5004c274d83c0492f95b4b510"
$script:AppSha256 = "09985ef7e8291a940c458bbf649176cf96f78afd8c45652d5ce2036b0ae6e04e"
$script:State0Sha256 = "8d4a601eff6dab73e9c93dbf882e7a0334a758029f922de6fecfb860c549c7d4"
$script:State1Sha256 = "ff246981fd6cb3a2b0bf95442868dd64add6ef07943be6f4fd057fd7f16c466b"
$script:TransactionName = ".pocketjs-persistence-state.json"
$script:BackupName = ".pocketjs-persistence-backup"
$script:ManagedFiles = @("APP.PKT", "STATE0.BIN", "STATE1.BIN")

function Get-LowerSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-ExpectedHash {
    param([Parameter(Mandatory = $true)][string]$Path,
          [Parameter(Mandatory = $true)][string]$Expected)
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
    if ($resolved -ne $driveRoot) { throw "Pass the iPod drive root, not $resolved" }
    $direct = Join-Path $resolved "rockbox.ipod"
    $nested = Join-Path (Join-Path $resolved ".rockbox") "rockbox.ipod"
    if (-not (Test-Path -LiteralPath $direct -PathType Leaf) -and
        -not (Test-Path -LiteralPath $nested -PathType Leaf)) {
        throw "No rockbox.ipod was found on $resolved"
    }
    return "$resolved\"
}

function Assert-Python {
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        throw "Python 3 was not found."
    }
    $version = & python --version 2>&1
    if ($LASTEXITCODE -ne 0 -or "$version" -notmatch "Python 3\.") {
        throw "python must run Python 3; got: $version"
    }
}

function Get-HandoffStatus {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $text = & python "$PSScriptRoot\handoff.py" status --mount $Mount 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "handoff status failed:`n$text" }
    try { return $text | ConvertFrom-Json } catch { throw "Invalid handoff status:`n$text" }
}

function Copy-VerifiedFile {
    param([Parameter(Mandatory = $true)][string]$Source,
          [Parameter(Mandatory = $true)][string]$Destination,
          [Parameter(Mandatory = $true)][string]$ExpectedSha256)
    $temporary = "$Destination.pocketjs-$([guid]::NewGuid().ToString('N'))"
    try {
        Copy-Item -LiteralPath $Source -Destination $temporary
        Assert-ExpectedHash -Path $temporary -Expected $ExpectedSha256 | Out-Null
        Move-Item -LiteralPath $temporary -Destination $Destination -Force
        Assert-ExpectedHash -Path $Destination -Expected $ExpectedSha256 | Out-Null
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Write-JsonAtomic {
    param([Parameter(Mandatory = $true)][string]$Path,
          [Parameter(Mandatory = $true)]$Value)
    $temporary = "$Path.pocketjs-$([guid]::NewGuid().ToString('N'))"
    try {
        $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temporary -Encoding utf8
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Get-PersistencePaths {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $root = Get-IPodRoot -Mount $Mount
    $pocket = Join-Path $root "POCKETJS"
    return [ordered]@{
        Root = $root
        Pocket = $pocket
        Transaction = (Join-Path $root $script:TransactionName)
        Backup = (Join-Path $root $script:BackupName)
    }
}

function Get-PersistenceState {
    param([Parameter(Mandatory = $true)]$Paths)
    if (-not (Test-Path -LiteralPath $Paths.Transaction -PathType Leaf)) {
        throw "No persistence transaction is active."
    }
    $state = Get-Content -Raw -LiteralPath $Paths.Transaction | ConvertFrom-Json
    if ($state.schema -ne 1 -or $state.transaction -ne "phase1-persistence") {
        throw "Unrecognized transaction state: $($Paths.Transaction)"
    }
    return $state
}

function Assert-StagedPersistence {
    param([Parameter(Mandatory = $true)]$Paths)
    Assert-ExpectedHash -Path (Join-Path $Paths.Pocket "APP.PKT") -Expected $script:AppSha256 | Out-Null
    Assert-ExpectedHash -Path (Join-Path $Paths.Pocket "STATE0.BIN") -Expected $script:State0Sha256 | Out-Null
    Assert-ExpectedHash -Path (Join-Path $Paths.Pocket "STATE1.BIN") -Expected $script:State1Sha256 | Out-Null
}

function Start-PersistenceTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-PersistencePaths -Mount $Mount
    if ((Test-Path -LiteralPath $paths.Transaction) -or (Test-Path -LiteralPath $paths.Backup)) {
        throw "A persistence transaction already exists. Run .\RESTORE.ps1 first."
    }
    $hadPocket = Test-Path -LiteralPath $paths.Pocket -PathType Container
    if ((Test-Path -LiteralPath $paths.Pocket) -and -not $hadPocket) {
        throw "Expected a directory or no entry at $($paths.Pocket)"
    }
    New-Item -ItemType Directory -Path $paths.Pocket -Force | Out-Null
    New-Item -ItemType Directory -Path $paths.Backup | Out-Null
    $items = @()
    try {
        foreach ($name in $script:ManagedFiles) {
            $current = Join-Path $paths.Pocket $name
            if ((Test-Path -LiteralPath $current) -and
                -not (Test-Path -LiteralPath $current -PathType Leaf)) {
                throw "Expected a regular file or no entry at $current"
            }
            $exists = Test-Path -LiteralPath $current -PathType Leaf
            $hash = if ($exists) { Get-LowerSha256 -Path $current } else { $null }
            $items += [pscustomobject][ordered]@{ name = $name; existed = $exists; sha256 = $hash }
            if ($exists) {
                Copy-VerifiedFile -Source $current -Destination (Join-Path $paths.Backup $name) -ExpectedSha256 $hash
            }
        }
        $state = [ordered]@{
            schema = 1
            transaction = "phase1-persistence"
            phase = "snapshot"
            createdAt = (Get-Date).ToString("o")
            hadPocketJsDirectory = $hadPocket
            files = $items
        }
        Write-JsonAtomic -Path $paths.Transaction -Value $state
    } catch {
        foreach ($name in $script:ManagedFiles) {
            Remove-Item -LiteralPath (Join-Path $paths.Backup $name) -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path -LiteralPath $paths.Backup -PathType Container) {
            Remove-Item -LiteralPath $paths.Backup -Force
        }
        if (-not $hadPocket -and @(Get-ChildItem -LiteralPath $paths.Pocket -Force).Count -eq 0) {
            Remove-Item -LiteralPath $paths.Pocket -Force
        }
        throw
    }

    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "APP.PKT") -Destination (Join-Path $paths.Pocket "APP.PKT") -ExpectedSha256 $script:AppSha256
    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "STATE0.BIN") -Destination (Join-Path $paths.Pocket "STATE0.BIN") -ExpectedSha256 $script:State0Sha256
    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "STATE1.BIN") -Destination (Join-Path $paths.Pocket "STATE1.BIN") -ExpectedSha256 $script:State1Sha256
    Assert-StagedPersistence -Paths $paths
    $state = Get-PersistenceState -Paths $paths
    $state.phase = "staged"
    Write-JsonAtomic -Path $paths.Transaction -Value $state
    return $paths
}

function Restore-PersistenceTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-PersistencePaths -Mount $Mount
    if (-not (Test-Path -LiteralPath $paths.Transaction -PathType Leaf)) {
        Write-Host "Persistence files are already restored."
        return
    }
    $state = Get-PersistenceState -Paths $paths
    if (-not (Test-Path -LiteralPath $paths.Backup -PathType Container)) {
        throw "Persistence backup is missing: $($paths.Backup)"
    }
    foreach ($item in @($state.files)) {
        $current = Join-Path $paths.Pocket ([string]$item.name)
        if ((Test-Path -LiteralPath $current) -and
            -not (Test-Path -LiteralPath $current -PathType Leaf)) {
            throw "Refusing to overwrite non-file path: $current"
        }
        if ($item.name -eq "APP.PKT" -and (Test-Path -LiteralPath $current -PathType Leaf)) {
            Assert-ExpectedHash -Path $current -Expected $script:AppSha256 | Out-Null
        } elseif ($item.name -ne "APP.PKT" -and
                  (Test-Path -LiteralPath $current -PathType Leaf) -and
                  (Get-Item -LiteralPath $current).Length -ne 512) {
            throw "State slot changed size; refusing to overwrite: $current"
        }
        if ($item.existed) {
            $backup = Join-Path $paths.Backup ([string]$item.name)
            Copy-VerifiedFile -Source $backup -Destination $current -ExpectedSha256 ([string]$item.sha256)
            Remove-Item -LiteralPath $backup -Force
        } else {
            Remove-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
        }
    }
    if (@(Get-ChildItem -LiteralPath $paths.Backup -Force).Count -ne 0) {
        throw "Unexpected files remain in $($paths.Backup)"
    }
    Remove-Item -LiteralPath $paths.Backup -Force
    Remove-Item -LiteralPath $paths.Transaction -Force
    if (-not $state.hadPocketJsDirectory -and
        @(Get-ChildItem -LiteralPath $paths.Pocket -Force).Count -eq 0) {
        Remove-Item -LiteralPath $paths.Pocket -Force
    }
    Write-Host "Original APP.PKT and state slots restored."
}

function Assert-HandoffInstalled {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $status = Get-HandoffStatus -Mount $Mount
    if (-not $status.targetExists -or $status.targetSha256 -ne $script:CandidateSha256) {
        throw "The mounted rockbox.ipod does not match this candidate."
    }
    if (-not $status.backupExists -or $null -eq $status.state -or
        $status.backupSha256 -ne $status.state.original.sha256 -or
        $status.state.probe.sha256 -ne $script:CandidateSha256) {
        throw "The verified Rockbox backup/state pair is invalid."
    }
    return $status
}
