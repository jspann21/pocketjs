$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$env:PYTHONDONTWRITEBYTECODE = "1"

$script:CandidateName = "pocketjs-a1099-phase1-package-lifecycle.ipod"
$script:CandidateSha256 = "a55eeb41b3a9afb35b525aa1c5cfbf5ad015d1b9136ea724a32c57b85f963702"
$script:ActiveSha256 = "d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b"
$script:PendingSha256 = "eca8936f299161b3668622247e740bae0de93ddd6dfcf432525e6404cf1df630"
$script:SlotNames = @("PENDING.PKT", "ACTIVE.PKT", "LASTGOOD.PKT", "APP.PKT")
$script:PackageStateName = ".pocketjs-package-lifecycle-state.json"
$script:PackageBackupName = ".pocketjs-package-lifecycle-backup"

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
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Value
    )
    $temporary = "$Path.pocketjs-$([guid]::NewGuid().ToString('N'))"
    try {
        $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temporary -Encoding utf8
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Get-PackagePaths {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $root = Get-IPodRoot -Mount $Mount
    return [ordered]@{
        Root = $root
        AppDirectory = (Join-Path $root "POCKETJS")
        State = (Join-Path $root $script:PackageStateName)
        BackupDirectory = (Join-Path $root $script:PackageBackupName)
    }
}

function Get-PackageState {
    param([Parameter(Mandatory = $true)]$Paths)
    if (-not (Test-Path -LiteralPath $Paths.State -PathType Leaf)) {
        throw "No package-lifecycle transaction is active."
    }
    $state = Get-Content -Raw -LiteralPath $Paths.State | ConvertFrom-Json
    if ($state.schema -ne 1 -or $state.transaction -ne "phase1-package-lifecycle") {
        throw "Unrecognized package-lifecycle state: $($Paths.State)"
    }
    return $state
}

function Start-PackageTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-PackagePaths -Mount $Mount
    if ((Test-Path -LiteralPath $paths.State) -or
        (Test-Path -LiteralPath $paths.BackupDirectory)) {
        throw "A package-lifecycle transaction already exists. Run .\RESTORE.ps1 first."
    }

    $hadDirectory = Test-Path -LiteralPath $paths.AppDirectory -PathType Container
    New-Item -ItemType Directory -Path $paths.AppDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $paths.BackupDirectory | Out-Null
    $originals = [ordered]@{}
    try {
        foreach ($name in $script:SlotNames) {
            $source = Join-Path $paths.AppDirectory $name
            $hadOriginal = Test-Path -LiteralPath $source -PathType Leaf
            $hash = $null
            if ($hadOriginal) {
                $hash = Get-LowerSha256 -Path $source
                $backup = Join-Path $paths.BackupDirectory $name
                Copy-VerifiedFile -Source $source -Destination $backup -ExpectedSha256 $hash
            } elseif (Test-Path -LiteralPath $source) {
                throw "Expected a regular file or no entry at $source"
            }
            $originals[$name] = [ordered]@{ exists = $hadOriginal; sha256 = $hash }
        }
        $state = [ordered]@{
            schema = 1
            transaction = "phase1-package-lifecycle"
            phase = "snapshot"
            createdAt = (Get-Date).ToString("o")
            hadPocketJsDirectory = $hadDirectory
            originals = $originals
        }
        Write-JsonAtomic -Path $paths.State -Value $state
    } catch {
        foreach ($name in $script:SlotNames) {
            Remove-Item -LiteralPath (Join-Path $paths.BackupDirectory $name) -Force -ErrorAction SilentlyContinue
        }
        Remove-Item -LiteralPath $paths.BackupDirectory -Force -ErrorAction SilentlyContinue
        if (-not $hadDirectory -and
            (Test-Path -LiteralPath $paths.AppDirectory -PathType Container) -and
            @(Get-ChildItem -LiteralPath $paths.AppDirectory -Force).Count -eq 0) {
            Remove-Item -LiteralPath $paths.AppDirectory -Force
        }
        throw
    }
    return $paths
}

function Set-PackagePhase {
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][string]$Phase
    )
    $state = Get-PackageState -Paths $Paths
    $state.phase = $Phase
    $state | Add-Member -NotePropertyName updatedAt `
        -NotePropertyValue ((Get-Date).ToString("o")) -Force
    Write-JsonAtomic -Path $Paths.State -Value $state
}

function Stage-ActivePhase {
    param([Parameter(Mandatory = $true)]$Paths)
    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "PENDING.PKT") `
        -Destination (Join-Path $Paths.AppDirectory "PENDING.PKT") `
        -ExpectedSha256 $script:PendingSha256
    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "ACTIVE.PKT") `
        -Destination (Join-Path $Paths.AppDirectory "ACTIVE.PKT") `
        -ExpectedSha256 $script:ActiveSha256
    Remove-Item -LiteralPath (Join-Path $Paths.AppDirectory "LASTGOOD.PKT") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $Paths.AppDirectory "APP.PKT") -Force -ErrorAction SilentlyContinue
    Set-PackagePhase -Paths $Paths -Phase "active"
}

function Get-ExpectedSlotHash {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("active", "embedded")][string]$Phase,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if ($Name -eq "PENDING.PKT") { return $script:PendingSha256 }
    if ($Phase -eq "active" -and $Name -eq "ACTIVE.PKT") { return $script:ActiveSha256 }
    return $null
}

function Assert-PackagePhase {
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][ValidateSet("active", "embedded")][string]$Phase
    )
    $state = Get-PackageState -Paths $Paths
    if ($state.phase -ne $Phase) {
        throw "Package state says '$($state.phase)', expected '$Phase'."
    }
    $result = [ordered]@{}
    foreach ($name in $script:SlotNames) {
        $path = Join-Path $Paths.AppDirectory $name
        $expected = Get-ExpectedSlotHash -Phase $Phase -Name $name
        if ($null -eq $expected) {
            if (Test-Path -LiteralPath $path) {
                throw "$name should be absent during the $Phase phase."
            }
            $result[$name] = $null
        } else {
            $result[$name] = Assert-ExpectedHash -Path $path -Expected $expected
        }
    }
    return $result
}

function Restore-PackageTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-PackagePaths -Mount $Mount
    if (-not (Test-Path -LiteralPath $paths.State -PathType Leaf)) {
        Write-Host "Package slots are already restored."
        return
    }
    $state = Get-PackageState -Paths $paths
    if (-not (Test-Path -LiteralPath $paths.BackupDirectory -PathType Container)) {
        throw "Package backup directory is missing: $($paths.BackupDirectory)"
    }

    foreach ($name in $script:SlotNames) {
        $original = $state.originals.$name
        $backup = Join-Path $paths.BackupDirectory $name
        if ($original.exists) {
            Assert-ExpectedHash -Path $backup -Expected ([string]$original.sha256) | Out-Null
        } elseif (Test-Path -LiteralPath $backup) {
            throw "Unexpected backup entry: $backup"
        }

        $current = Join-Path $paths.AppDirectory $name
        if (Test-Path -LiteralPath $current -PathType Leaf) {
            $currentHash = Get-LowerSha256 -Path $current
            $allowed = @($script:PendingSha256, $script:ActiveSha256)
            if ($original.exists) { $allowed += [string]$original.sha256 }
            if ($allowed -notcontains $currentHash) {
                throw "$current was changed outside this transaction; refusing to overwrite it."
            }
        } elseif (Test-Path -LiteralPath $current) {
            throw "Expected a regular file or no entry at $current"
        }
    }

    foreach ($name in $script:SlotNames) {
        $original = $state.originals.$name
        $current = Join-Path $paths.AppDirectory $name
        $backup = Join-Path $paths.BackupDirectory $name
        if ($original.exists) {
            Copy-VerifiedFile -Source $backup -Destination $current `
                -ExpectedSha256 ([string]$original.sha256)
        } else {
            Remove-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
        }
    }

    foreach ($name in $script:SlotNames) {
        Remove-Item -LiteralPath (Join-Path $paths.BackupDirectory $name) -Force -ErrorAction SilentlyContinue
    }
    if (@(Get-ChildItem -LiteralPath $paths.BackupDirectory -Force).Count -ne 0) {
        throw "Unexpected files remain in $($paths.BackupDirectory); transaction state was preserved."
    }
    Remove-Item -LiteralPath $paths.BackupDirectory -Force
    Remove-Item -LiteralPath $paths.State -Force
    if (-not $state.hadPocketJsDirectory -and
        @(Get-ChildItem -LiteralPath $paths.AppDirectory -Force).Count -eq 0) {
        Remove-Item -LiteralPath $paths.AppDirectory -Force
    }
    Write-Host "Original PocketJS package slots restored."
}

function Assert-HandoffInstalled {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $status = Get-HandoffStatus -Mount $Mount
    if (-not $status.targetExists -or $status.targetSha256 -ne $script:CandidateSha256) {
        throw "The mounted rockbox.ipod does not match this candidate."
    }
    if (-not $status.backupExists -or $null -eq $status.state) {
        throw "The verified Rockbox backup/state pair is missing."
    }
    if ($status.backupSha256 -ne $status.state.original.sha256 -or
        $status.state.probe.sha256 -ne $script:CandidateSha256) {
        throw "The Rockbox backup/state hashes do not match this transaction."
    }
    return $status
}
