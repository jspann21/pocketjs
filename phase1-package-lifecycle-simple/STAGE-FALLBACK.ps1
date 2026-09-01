param([string]$Mount = "F:\")

. "$PSScriptRoot\common.ps1"

Assert-Python
$root = Get-IPodRoot -Mount $Mount
Assert-HandoffInstalled -Mount $root | Out-Null
$paths = Get-PackagePaths -Mount $root
Assert-PackagePhase -Paths $paths -Phase active | Out-Null

Remove-Item -LiteralPath (Join-Path $paths.AppDirectory "ACTIVE.PKT") -Force
Set-PackagePhase -Paths $paths -Phase "embedded"
& "$PSScriptRoot\VERIFY.ps1" -Mount $root -Phase embedded -OutputName "FALLBACK-STATUS.json"

Write-Host ""
Write-Host "FALLBACK STAGED. Safely eject and boot once more."
