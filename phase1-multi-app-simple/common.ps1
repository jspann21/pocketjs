$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$env:PYTHONDONTWRITEBYTECODE = "1"

$script:CandidateName = "pocketjs-a1099-phase1-multi-app.ipod"
$script:CandidateSha256 = "de5e44d771b10a4d0d7b2d8d11411d428a5ed90ff0b7632de7d2d5d13653323b"
$script:FallbackSha256 = "d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b"
$script:LauncherSha256 = "8a114f182fed434faa2838a1d2bad2bdd28dd4c61045c0ef560d784ef7b5a42e"
$script:AlphaSha256 = "d489c03fdfbbdffc2e15922bdd185ce8b07d7722061a8516392162c151664735"
$script:BetaSha256 = "de70763dab50c79381e38c3ead29fe2361584a33a157745038f7ff31a85fe7a9"
$script:StateName = ".pocketjs-multi-app-state.json"
$script:BackupName = ".pocketjs-multi-app-backup"

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
    if ($LASTEXITCODE -ne 0) { throw "handoff status failed:`n$text" }
    try { return $text | ConvertFrom-Json } catch {
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
        $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding utf8
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Get-MultiPaths {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $root = Get-IPodRoot -Mount $Mount
    $pocket = Join-Path $root "POCKETJS"
    return [ordered]@{
        Root = $root
        Pocket = $pocket
        Launcher = (Join-Path $pocket "LAUNCHER.PKT")
        Apps = (Join-Path $pocket "APPS")
        State = (Join-Path $root $script:StateName)
        Backup = (Join-Path $root $script:BackupName)
        BackupLauncher = (Join-Path (Join-Path $root $script:BackupName) "ORIGINAL-LAUNCHER.PKT")
        BackupApps = (Join-Path (Join-Path $root $script:BackupName) "ORIGINAL-APPS")
    }
}

function Assert-SafeChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd("\") + "\"
    $pathFull = [System.IO.Path]::GetFullPath($Path).TrimEnd("\")
    if (-not $pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe path outside the iPod root: $pathFull"
    }
}

function Assert-NoReparseTree {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $items = @((Get-Item -LiteralPath $Path -Force))
    if (Test-Path -LiteralPath $Path -PathType Container) {
        $items += @(Get-ChildItem -LiteralPath $Path -Force -Recurse)
    }
    foreach ($item in $items) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse points are not supported in the transaction: $($item.FullName)"
        }
    }
}

function Get-TreeInventory {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return @() }
    Assert-NoReparseTree -Path $Path
    $base = (Get-Item -LiteralPath $Path).FullName.TrimEnd("\")
    $result = @()
    foreach ($item in @(Get-ChildItem -LiteralPath $Path -Force -Recurse | Sort-Object FullName)) {
        $relative = $item.FullName.Substring($base.Length + 1).Replace("/", "\")
        if ($item.PSIsContainer) {
            $result += [pscustomobject][ordered]@{ path = $relative; type = "directory" }
        } else {
            $result += [pscustomobject][ordered]@{
                path = $relative
                type = "file"
                length = [int64]$item.Length
                sha256 = (Get-LowerSha256 -Path $item.FullName)
            }
        }
    }
    return @($result)
}

function Assert-InventoryEqual {
    param(
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $actualJson = @($Actual) | ConvertTo-Json -Depth 8 -Compress
    $expectedJson = @($Expected) | ConvertTo-Json -Depth 8 -Compress
    if ($actualJson -ne $expectedJson) { throw "$Label inventory does not match its snapshot." }
}

function Get-MultiState {
    param([Parameter(Mandatory = $true)]$Paths)
    if (-not (Test-Path -LiteralPath $Paths.State -PathType Leaf)) {
        throw "No multi-app transaction is active."
    }
    $state = Get-Content -Raw -LiteralPath $Paths.State | ConvertFrom-Json
    if ($state.schema -ne 1 -or $state.transaction -ne "phase1-multi-app") {
        throw "Unrecognized multi-app state: $($Paths.State)"
    }
    return $state
}

function Set-MultiPhase {
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][string]$Phase
    )
    $state = Get-MultiState -Paths $Paths
    $state.phase = $Phase
    $state | Add-Member -NotePropertyName updatedAt -NotePropertyValue ((Get-Date).ToString("o")) -Force
    Write-JsonAtomic -Path $Paths.State -Value $state
}

function Start-MultiTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-MultiPaths -Mount $Mount
    if ((Test-Path -LiteralPath $paths.State) -or (Test-Path -LiteralPath $paths.Backup)) {
        throw "A multi-app transaction already exists. Run .\RESTORE.ps1 first."
    }
    $hadPocket = Test-Path -LiteralPath $paths.Pocket -PathType Container
    if ((Test-Path -LiteralPath $paths.Pocket) -and -not $hadPocket) {
        throw "Expected a directory or no entry at $($paths.Pocket)"
    }
    if ($hadPocket) { Assert-NoReparseTree -Path $paths.Pocket }
    $launcherExists = Test-Path -LiteralPath $paths.Launcher -PathType Leaf
    if ((Test-Path -LiteralPath $paths.Launcher) -and -not $launcherExists) {
        throw "Expected a regular file or no entry at $($paths.Launcher)"
    }
    $launcherHash = if ($launcherExists) { Get-LowerSha256 -Path $paths.Launcher } else { $null }
    $appsExists = Test-Path -LiteralPath $paths.Apps -PathType Container
    if ((Test-Path -LiteralPath $paths.Apps) -and -not $appsExists) {
        throw "Expected a directory or no entry at $($paths.Apps)"
    }
    $appsInventory = if ($appsExists) { @(Get-TreeInventory -Path $paths.Apps) } else { @() }
    New-Item -ItemType Directory -Path $paths.Pocket -Force | Out-Null
    New-Item -ItemType Directory -Path $paths.Backup | Out-Null
    $state = [ordered]@{
        schema = 1
        transaction = "phase1-multi-app"
        phase = "prepared"
        createdAt = (Get-Date).ToString("o")
        hadPocketJsDirectory = $hadPocket
        launcherExists = $launcherExists
        launcherSha256 = $launcherHash
        appsExists = $appsExists
        appsInventory = $appsInventory
    }
    try {
        if ($launcherExists) {
            Copy-VerifiedFile -Source $paths.Launcher -Destination $paths.BackupLauncher -ExpectedSha256 $launcherHash
        }
        Write-JsonAtomic -Path $paths.State -Value $state
    } catch {
        Remove-Item -LiteralPath $paths.BackupLauncher -Force -ErrorAction SilentlyContinue
        if ((Test-Path -LiteralPath $paths.Backup -PathType Container) -and
            @(Get-ChildItem -LiteralPath $paths.Backup -Force).Count -eq 0) {
            Remove-Item -LiteralPath $paths.Backup -Force
        }
        if (-not $hadPocket -and
            @(Get-ChildItem -LiteralPath $paths.Pocket -Force).Count -eq 0) {
            Remove-Item -LiteralPath $paths.Pocket -Force
        }
        throw
    }
    if ($appsExists) {
        Move-Item -LiteralPath $paths.Apps -Destination $paths.BackupApps
        Assert-InventoryEqual -Actual (Get-TreeInventory -Path $paths.BackupApps) -Expected $appsInventory -Label "APPS backup"
    }
    Set-MultiPhase -Paths $paths -Phase "snapshot"
    return $paths
}

function Assert-StagedFiles {
    param([Parameter(Mandatory = $true)]$Paths)
    Assert-ExpectedHash -Path $Paths.Launcher -Expected $script:LauncherSha256 | Out-Null
    Assert-NoReparseTree -Path $Paths.Apps
    $items = @(Get-ChildItem -LiteralPath $Paths.Apps -Force)
    if ($items.Count -ne 2 -or @($items | Where-Object { $_.PSIsContainer }).Count -ne 0) {
        throw "POCKETJS\APPS must contain only ALPHA.PKT and BETA.PKT."
    }
    Assert-ExpectedHash -Path (Join-Path $Paths.Apps "ALPHA.PKT") -Expected $script:AlphaSha256 | Out-Null
    Assert-ExpectedHash -Path (Join-Path $Paths.Apps "BETA.PKT") -Expected $script:BetaSha256 | Out-Null
}

function Stage-MultiApps {
    param([Parameter(Mandatory = $true)]$Paths)
    Set-MultiPhase -Paths $Paths -Phase "staging"
    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "LAUNCHER.PKT") -Destination $Paths.Launcher -ExpectedSha256 $script:LauncherSha256
    New-Item -ItemType Directory -Path $Paths.Apps | Out-Null
    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "ALPHA.PKT") -Destination (Join-Path $Paths.Apps "ALPHA.PKT") -ExpectedSha256 $script:AlphaSha256
    Copy-VerifiedFile -Source (Join-Path $PSScriptRoot "BETA.PKT") -Destination (Join-Path $Paths.Apps "BETA.PKT") -ExpectedSha256 $script:BetaSha256
    Assert-StagedFiles -Paths $Paths
    Set-MultiPhase -Paths $Paths -Phase "staged"
}

function Assert-StagedSubset {
    param([Parameter(Mandatory = $true)]$Paths)
    if (-not (Test-Path -LiteralPath $Paths.Apps)) { return }
    if (-not (Test-Path -LiteralPath $Paths.Apps -PathType Container)) {
        throw "Expected a directory or no entry at $($Paths.Apps)"
    }
    Assert-NoReparseTree -Path $Paths.Apps
    foreach ($item in @(Get-ChildItem -LiteralPath $Paths.Apps -Force -Recurse)) {
        if ($item.PSIsContainer) { throw "Unexpected staged subdirectory: $($item.FullName)" }
        if ($item.Name -eq "ALPHA.PKT") {
            Assert-ExpectedHash -Path $item.FullName -Expected $script:AlphaSha256 | Out-Null
        } elseif ($item.Name -eq "BETA.PKT") {
            Assert-ExpectedHash -Path $item.FullName -Expected $script:BetaSha256 | Out-Null
        } else {
            throw "Unexpected staged file: $($item.FullName)"
        }
    }
}

function Restore-MultiTransaction {
    param([Parameter(Mandatory = $true)][string]$Mount)
    $paths = Get-MultiPaths -Mount $Mount
    if (-not (Test-Path -LiteralPath $paths.State -PathType Leaf)) {
        Write-Host "Launcher and APPS transaction is already restored."
        return
    }
    $state = Get-MultiState -Paths $paths
    if (-not (Test-Path -LiteralPath $paths.Backup -PathType Container)) {
        throw "Multi-app backup directory is missing: $($paths.Backup)"
    }
    Assert-NoReparseTree -Path $paths.Backup

    if ($state.launcherExists) {
        Assert-ExpectedHash -Path $paths.BackupLauncher -Expected ([string]$state.launcherSha256) | Out-Null
    } elseif (Test-Path -LiteralPath $paths.BackupLauncher) {
        throw "Unexpected launcher backup: $($paths.BackupLauncher)"
    }
    if ($state.appsExists) {
        Assert-InventoryEqual -Actual (Get-TreeInventory -Path $paths.BackupApps) -Expected @($state.appsInventory) -Label "APPS backup"
    } elseif (Test-Path -LiteralPath $paths.BackupApps) {
        throw "Unexpected APPS backup: $($paths.BackupApps)"
    }

    if (Test-Path -LiteralPath $paths.Launcher -PathType Leaf) {
        $currentLauncher = Get-LowerSha256 -Path $paths.Launcher
        $allowed = @($script:LauncherSha256)
        if ($state.launcherExists) { $allowed += [string]$state.launcherSha256 }
        if ($allowed -notcontains $currentLauncher) {
            throw "$($paths.Launcher) changed outside this transaction; refusing to overwrite it."
        }
    } elseif (Test-Path -LiteralPath $paths.Launcher) {
        throw "Expected a regular file or no entry at $($paths.Launcher)"
    }

    if ($state.phase -eq "prepared" -and (Test-Path -LiteralPath $paths.Apps -PathType Container)) {
        Assert-InventoryEqual -Actual (Get-TreeInventory -Path $paths.Apps) -Expected @($state.appsInventory) -Label "Current APPS"
    } else {
        Assert-StagedSubset -Paths $paths
    }

    if (Test-Path -LiteralPath $paths.Apps -PathType Container) {
        Assert-SafeChildPath -Root $paths.Root -Path $paths.Apps
        Remove-Item -LiteralPath $paths.Apps -Recurse -Force
    }
    if ($state.appsExists) {
        Move-Item -LiteralPath $paths.BackupApps -Destination $paths.Apps
        Assert-InventoryEqual -Actual (Get-TreeInventory -Path $paths.Apps) -Expected @($state.appsInventory) -Label "Restored APPS"
    }

    if ($state.launcherExists) {
        Copy-VerifiedFile -Source $paths.BackupLauncher -Destination $paths.Launcher -ExpectedSha256 ([string]$state.launcherSha256)
        Remove-Item -LiteralPath $paths.BackupLauncher -Force
    } else {
        Remove-Item -LiteralPath $paths.Launcher -Force -ErrorAction SilentlyContinue
    }

    if (@(Get-ChildItem -LiteralPath $paths.Backup -Force).Count -ne 0) {
        throw "Unexpected files remain in $($paths.Backup); transaction state was preserved."
    }
    Remove-Item -LiteralPath $paths.Backup -Force
    Remove-Item -LiteralPath $paths.State -Force
    if (-not $state.hadPocketJsDirectory -and
        @(Get-ChildItem -LiteralPath $paths.Pocket -Force).Count -eq 0) {
        Remove-Item -LiteralPath $paths.Pocket -Force
    }
    Write-Host "Original launcher and APPS tree restored."
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
