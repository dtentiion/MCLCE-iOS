# Downloads the latest Minecraft.LCE.ipa built by GitHub Actions.
#
# Usage:
#   .\scripts\fetch-latest-ipa.ps1
#   .\scripts\fetch-latest-ipa.ps1 -Branch main -Out .\Minecraft.LCE.ipa
#
# Requires the GitHub CLI (`gh`) and an authenticated session (`gh auth login`).

param(
    [string] $Repo   = "dtentiion/MCLE-iOS",
    [string] $Branch = "main",
    [string] $Out    = "Minecraft.LCE.ipa"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Error "GitHub CLI (gh) is not installed. Get it at https://cli.github.com/"
}

Write-Host "Looking up the latest successful build of $Repo on $Branch..."

$runJson = gh run list -R $Repo --branch $Branch --workflow "iOS build" --status success --limit 1 --json databaseId,conclusion,headSha,createdAt
$run = $runJson | ConvertFrom-Json
if (-not $run -or $run.Count -eq 0) {
    Write-Error "No successful iOS build found on branch $Branch yet. Check the Actions tab."
}

$runId = $run[0].databaseId
Write-Host "Found run $runId ($($run[0].createdAt)), downloading artifact..."

$tmp = New-Item -ItemType Directory -Path (Join-Path $env:TEMP "mcle-ipa-$runId") -Force
try {
    gh run download $runId -R $Repo -n "Minecraft.LCE-ipa" -D $tmp.FullName | Out-Null
    $ipa = Get-ChildItem -Path $tmp.FullName -Filter "*.ipa" -Recurse | Select-Object -First 1
    if (-not $ipa) {
        Write-Error "Artifact downloaded but no .ipa was found inside it."
    }

    Copy-Item -Path $ipa.FullName -Destination $Out -Force
    Write-Host "Saved to $(Resolve-Path $Out)"
    Write-Host ""
    Write-Host "Install it with one of:"
    Write-Host "  - Sideloadly  : open the .ipa and pick your device"
    Write-Host "  - AltStore    : share the .ipa to AltStore on a paired device"
    Write-Host "  - xtool       : xtool install $Out"
    Write-Host "  - TrollStore  : AirDrop / share the file if your device supports it"
} finally {
    Remove-Item -Path $tmp.FullName -Recurse -Force -ErrorAction SilentlyContinue
}
