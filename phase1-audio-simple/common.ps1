$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$env:PYTHONDONTWRITEBYTECODE = "1"

$script:CandidateName = "pocketjs-a1099-phase1-audio.ipod"
$script:CandidateSha256 = "e3dd2ee1e51667b17f10abbe943d86cd439c35d104d0f63215c2de800cfbaa45"
$script:TransactionName = ".pocketjs-audio-state.json"
$script:BackupName = ".pocketjs-audio-backup"
$script:ManagedFiles = @(
    "ACTIVE.PKT", "PENDING.PKT", "LASTGOOD.PKT", "APP.PKT",
    "STATE0.BIN", "STATE1.BIN"
)
$script:StagedFiles = [ordered]@{
    "ACTIVE.PKT" = "d489c03fdfbbdffc2e15922bdd185ce8b07d7722061a8516392162c151664735"
    "LASTGOOD.PKT" = "d489c03fdfbbdffc2e15922bdd185ce8b07d7722061a8516392162c151664735"
    "APP.PKT" = "d489c03fdfbbdffc2e15922bdd185ce8b07d7722061a8516392162c151664735"
    "STATE0.BIN" = "1b82ac80abc8cbea1b3777f0ade19795a398d1730f00b2ab0614443a2083b4ac"
    "STATE1.BIN" = "5721fb6065c6f602f5e77a4e5949573e95600d7d5e9542b05ec8e56cd66ebb17"
}

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

function Get-LineagePaths {
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

function Get-LineageState {
    param([Parameter(Mandatory = $true)]$Paths)
    if (-not (Test-Path -LiteralPath $Paths.Transaction -PathType Leaf)) {
        throw "No lineage transaction is active."
    }
    $state = Get-Content -Raw -LiteralPath $Paths.Transaction | ConvertFrom-Json
    if ($state.schema -ne 1 -or $state.transaction -ne "phase1-audio") {
        throw "Unrecognized transaction state: $($Paths.Transaction)"
    }
    return $state
}

function Assert-StagedLineage {
    param([Parameter(Mandatory = $true)]$Paths)
    foreach ($entry in $script:StagedFiles.GetEnumerator()) {
        Assert-ExpectedHash -Path (Join-Path $Paths.Pocket $entry.Key) -Expected $entry.Value | Out-Null
    }
}

function Start-LineageTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-LineagePaths -Mount $Mount
    if ((Test-Path -LiteralPath $paths.Transaction) -or (Test-Path -LiteralPath $paths.Backup)) {
        throw "A lineage transaction already exists. Run .\RESTORE.ps1 first."
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
            transaction = "phase1-audio"
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

    foreach ($entry in $script:StagedFiles.GetEnumerator()) {
        Copy-VerifiedFile -Source (Join-Path $PSScriptRoot $entry.Key) -Destination (Join-Path $paths.Pocket $entry.Key) -ExpectedSha256 $entry.Value
    }
    foreach ($name in $script:ManagedFiles) {
        if (-not $script:StagedFiles.Contains($name)) {
            Remove-Item -LiteralPath (Join-Path $paths.Pocket $name) -Force -ErrorAction SilentlyContinue
        }
    }
    Assert-StagedLineage -Paths $paths
    $state = Get-LineageState -Paths $paths
    $state.phase = "staged"
    Write-JsonAtomic -Path $paths.Transaction -Value $state
    return $paths
}

function Restore-LineageTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-LineagePaths -Mount $Mount
    if (-not (Test-Path -LiteralPath $paths.Transaction -PathType Leaf)) {
        Write-Host "Lineage files are already restored."
        return
    }
    $state = Get-LineageState -Paths $paths
    if (-not (Test-Path -LiteralPath $paths.Backup -PathType Container)) {
        throw "Lineage backup is missing: $($paths.Backup)"
    }
    $items = @($state.files)
    if ($items.Count -ne $script:ManagedFiles.Count) {
        throw "Transaction file manifest has the wrong number of entries."
    }
    $seen = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    foreach ($item in $items) {
        $name = [string]$item.name
        if ($script:ManagedFiles -cnotcontains $name -or -not $seen.Add($name)) {
            throw "Transaction file manifest contains an invalid name: $name"
        }
    }
    foreach ($item in $items) {
        $current = Join-Path $paths.Pocket ([string]$item.name)
        if ((Test-Path -LiteralPath $current) -and
            -not (Test-Path -LiteralPath $current -PathType Leaf)) {
            throw "Refusing to overwrite non-file path: $current"
        }
        $isState = ([string]$item.name).StartsWith("STATE")
        if ($script:StagedFiles.Contains([string]$item.name) -and -not $isState -and
            (Test-Path -LiteralPath $current -PathType Leaf)) {
            Assert-ExpectedHash -Path $current -Expected $script:StagedFiles[[string]$item.name] | Out-Null
        } elseif ($isState -and (Test-Path -LiteralPath $current -PathType Leaf) -and
                  (Get-Item -LiteralPath $current).Length -ne 512) {
            throw "State slot changed size; refusing to overwrite: $current"
        } elseif (-not $script:StagedFiles.Contains([string]$item.name)) {
            if (Test-Path -LiteralPath $current) {
                throw "Suppressed file unexpectedly appeared during the gate: $current"
            }
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
    Write-Host "Original package files and state slots restored."
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
