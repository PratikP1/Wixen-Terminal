# Fetch the prebuilt OpenConsole binaries Wixen bundles for default-terminal
# support on Windows 11.
#
# OpenConsole.exe and OpenConsoleProxy.dll ship inside the Windows Terminal
# portable release zip on GitHub (MIT license, Microsoft Corporation). This
# script downloads a pinned release, extracts the two binaries, verifies their
# SHA256 hashes against the pinned values below, and copies them into the
# vendor directory the installers and CI package from.
#
# We download prebuilt binaries; we never compile Windows Terminal.
#
# Usage:
#   .\scripts\fetch-openconsole.ps1                    # fetch + verify (x64)
#   .\scripts\fetch-openconsole.ps1 -Arch arm64        # fetch + verify (arm64)
#   .\scripts\fetch-openconsole.ps1 -UpdateHashes      # print hashes to pin
#
# Hash pinning (a human must do this once per version/arch):
#   1. On a trusted machine and network, run with -UpdateHashes.
#   2. Independently confirm the download came from
#      github.com/microsoft/terminal/releases (the script prints the URL).
#   3. Paste the printed hash lines into $ExpectedHashes below and commit.
#   The script refuses to install binaries whose hashes are unpinned or do not
#   match. -UpdateHashes never installs anything; it only prints.
#
# No administrator rights are required. Compatible with Windows PowerShell 5.1
# and PowerShell 7+.

#Requires -Version 5.1
[CmdletBinding()]
param(
    # Windows Terminal release version whose zip carries the binaries.
    # Must match a tag (without the leading "v") that has portable-zip assets:
    # https://github.com/microsoft/terminal/releases
    [string]$WtVersion = "1.24.11911.0",

    # Architecture of the binaries to fetch.
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    # Directory the verified binaries are copied into.
    [string]$OutDir = "vendor/openconsole",

    # Print computed SHA256 hashes for pinning instead of installing.
    [switch]$UpdateHashes
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$BinaryNames = @("OpenConsole.exe", "OpenConsoleProxy.dll")

# Pinned SHA256 hashes of the extracted binaries, keyed by architecture.
# "PIN-ME" means no human has pinned this file yet; the script will refuse to
# install it. See the hash pinning instructions in the header comment.
$ExpectedHashes = @{
    "x64" = @{
        "OpenConsole.exe"      = "PIN-ME"
        "OpenConsoleProxy.dll" = "PIN-ME"
    }
    "arm64" = @{
        "OpenConsole.exe"      = "PIN-ME"
        "OpenConsoleProxy.dll" = "PIN-ME"
    }
}

$AssetName = "Microsoft.WindowsTerminal_${WtVersion}_${Arch}.zip"
$AssetUrl = "https://github.com/microsoft/terminal/releases/download/v${WtVersion}/${AssetName}"

function Write-AttributionNotice {
    Write-Host ""
    Write-Host "Attribution: OpenConsole.exe and OpenConsoleProxy.dll are unmodified"
    Write-Host "binaries from Windows Terminal v$WtVersion ($Arch),"
    Write-Host "Copyright (c) Microsoft Corporation, distributed under the MIT license."
    Write-Host "Source: $AssetUrl"
    Write-Host "License and details: vendor/openconsole/README.md"
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

# Returns the names of pinned files under $Directory whose hashes match the
# pins; used for the idempotency check. Unpinned entries never match.
function Test-InstalledBinaries([string]$Directory, [hashtable]$Pins) {
    foreach ($name in $BinaryNames) {
        $path = Join-Path $Directory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
        $pin = $Pins[$name]
        if ($pin -eq "PIN-ME") { return $false }
        if ((Get-Sha256 $path) -ne $pin.ToUpperInvariant()) { return $false }
    }
    return $true
}

$pins = $ExpectedHashes[$Arch]

# Resolve OutDir relative to the repository root (parent of scripts/), so the
# script works from any current directory.
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $repoRoot $OutDir
}

# Idempotency: skip the download when verified binaries are already in place.
if (-not $UpdateHashes -and (Test-InstalledBinaries $OutDir $pins)) {
    Write-Host "OpenConsole binaries already present in $OutDir and match pinned hashes; nothing to do."
    Write-AttributionNotice
    exit 0
}

# Download the release zip to a temp directory.
$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "wixen-openconsole-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

try {
    $zipPath = Join-Path $tempDir $AssetName
    Write-Host "Downloading $AssetUrl"

    # Windows PowerShell 5.1 defaults to TLS 1.0; force TLS 1.2+.
    [System.Net.ServicePointManager]::SecurityProtocol = `
        [System.Net.ServicePointManager]::SecurityProtocol -bor [System.Net.SecurityProtocolType]::Tls12

    try {
        Invoke-WebRequest -Uri $AssetUrl -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Error ("Download failed: $($_.Exception.Message)`n" +
            "URL: $AssetUrl`n" +
            "Check your network connection, and confirm that Windows Terminal " +
            "v$WtVersion has a '$AssetName' asset at " +
            "https://github.com/microsoft/terminal/releases")
        exit 1
    }

    Write-Host "Extracting $AssetName"
    $extractDir = Join-Path $tempDir "extracted"
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

    # Locate the two binaries anywhere inside the zip (the portable zip nests
    # them under a versioned top-level folder).
    $found = @{}
    foreach ($name in $BinaryNames) {
        $match = Get-ChildItem -Path $extractDir -Recurse -Filter $name | Select-Object -First 1
        if (-not $match) {
            Write-Error ("'$name' not found inside $AssetName. The Windows Terminal " +
                "release layout may have changed; inspect the zip contents and " +
                "update this script if needed.")
            exit 1
        }
        $found[$name] = $match.FullName
    }

    # Compute hashes of the extracted binaries.
    $computed = @{}
    foreach ($name in $BinaryNames) {
        $computed[$name] = Get-Sha256 $found[$name]
    }

    if ($UpdateHashes) {
        Write-Host ""
        Write-Host "Computed SHA256 hashes for Windows Terminal v$WtVersion ($Arch)."
        Write-Host "Verify the download URL above is the official microsoft/terminal"
        Write-Host "release, then pin these values in `$ExpectedHashes in this script:"
        Write-Host ""
        Write-Host "    `"$Arch`" = @{"
        foreach ($name in $BinaryNames) {
            $padded = "`"$name`"".PadRight(22)
            Write-Host "        $padded = `"$($computed[$name])`""
        }
        Write-Host "    }"
        Write-Host ""
        Write-Host "Nothing was installed. Re-run without -UpdateHashes after pinning."
        exit 0
    }

    # Verify against pins; refuse to install on any mismatch or missing pin.
    $failures = @()
    foreach ($name in $BinaryNames) {
        $pin = $pins[$name]
        if ($pin -eq "PIN-ME") {
            $failures += "$name has no pinned hash for $Arch."
        } elseif ($computed[$name] -ne $pin.ToUpperInvariant()) {
            $failures += "$name hash mismatch: expected $pin, got $($computed[$name])."
        }
    }
    if ($failures.Count -gt 0) {
        Write-Error (($failures -join "`n") + "`n" +
            "Refusing to install unverified binaries. If this is a new version or " +
            "architecture, a human must run this script with -UpdateHashes on a " +
            "trusted machine, confirm the source, and pin the printed hashes. If " +
            "the pins are current, the download may be corrupted or tampered with " +
            "- do not bypass this check.")
        exit 1
    }

    # Install the verified binaries.
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    foreach ($name in $BinaryNames) {
        Copy-Item -LiteralPath $found[$name] -Destination (Join-Path $OutDir $name) -Force
        Write-Host "Installed $name -> $OutDir"
    }

    Write-Host "All hashes verified against pinned values."
    Write-AttributionNotice
} finally {
    if (Test-Path -LiteralPath $tempDir) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
