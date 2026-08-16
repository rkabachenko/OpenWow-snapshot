<#
.SYNOPSIS
  packaging/windows/sign.ps1 -- Authenticode-sign a staged openwow-client.exe.

.DESCRIPTION
  ENTRY POINT (called by CI after `cmake --install ... --component client`):

      pwsh packaging/windows/sign.ps1 <path\to\openwow-client.exe>

  Exactly one positional argument: the executable to sign, in place.

  Environment (all optional):

      WINDOWS_CERT_PFX_BASE64   base64-encoded code-signing certificate (.pfx).
                                When empty/unset the script prints "unsigned"
                                and exits 0 -- the executable is left as built.
                                Unsigned builds trigger the SmartScreen
                                "Windows protected your PC" prompt on first
                                launch (More info -> Run anyway); see README.
      WINDOWS_CERT_PASSWORD     password of the .pfx.
      WINDOWS_TIMESTAMP_URL     RFC 3161 timestamp server
                                (default: http://timestamp.digicert.com).
      SIGNTOOL                  explicit path to signtool.exe; otherwise it is
                                located under the installed Windows SDK bin
                                directories (newest version, x64 then x86).

  Signing runs `signtool sign /fd sha256 /tr <url> /td sha256 /f <pfx> /p <pw>`
  followed by `signtool verify /pa`. The decoded certificate is deleted on
  exit regardless of outcome. Non-zero exit on any attempted step failing.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$ExePath
)

$ErrorActionPreference = 'Stop'

function Write-Log([string]$Message) { Write-Host "[sign.ps1] $Message" }

if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    Write-Error "'$ExePath' does not exist"
    exit 2
}

$pfxBase64 = $env:WINDOWS_CERT_PFX_BASE64
if ([string]::IsNullOrWhiteSpace($pfxBase64)) {
    Write-Log "WINDOWS_CERT_PFX_BASE64 not set: unsigned"
    Write-Host "unsigned"
    exit 0
}

function Find-SignTool {
    if (-not [string]::IsNullOrWhiteSpace($env:SIGNTOOL)) {
        if (Test-Path -LiteralPath $env:SIGNTOOL) { return $env:SIGNTOOL }
        throw "SIGNTOOL='$($env:SIGNTOOL)' does not exist"
    }
    $fromPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    foreach ($root in $roots) {
        $versions = Get-ChildItem -LiteralPath $root -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($v in $versions) {
            foreach ($arch in @('x64', 'x86', 'arm64')) {
                $candidate = Join-Path $v.FullName "$arch\signtool.exe"
                if (Test-Path -LiteralPath $candidate) { return $candidate }
            }
        }
    }
    throw "signtool.exe not found; install the Windows SDK or set SIGNTOOL"
}

$signtool = Find-SignTool
Write-Log "using $signtool"

$timestampUrl = if ([string]::IsNullOrWhiteSpace($env:WINDOWS_TIMESTAMP_URL)) {
    'http://timestamp.digicert.com'
} else { $env:WINDOWS_TIMESTAMP_URL }

$pfxPath = Join-Path ([System.IO.Path]::GetTempPath()) ("openwow-sign-" + [guid]::NewGuid().ToString('N') + '.pfx')
try {
    [System.IO.File]::WriteAllBytes($pfxPath, [Convert]::FromBase64String($pfxBase64))
    $signArgs = @('sign', '/fd', 'sha256', '/tr', $timestampUrl, '/td', 'sha256', '/f', $pfxPath)
    if (-not [string]::IsNullOrEmpty($env:WINDOWS_CERT_PASSWORD)) {
        $signArgs += @('/p', $env:WINDOWS_CERT_PASSWORD)
    }
    $signArgs += @($ExePath)
    Write-Log "signing $ExePath"
    & $signtool @signArgs
    if ($LASTEXITCODE -ne 0) { throw "signtool sign failed with exit code $LASTEXITCODE" }
    & $signtool verify /pa /v $ExePath
    if ($LASTEXITCODE -ne 0) { throw "signtool verify failed with exit code $LASTEXITCODE" }
    Write-Log "signed and verified $ExePath"
}
finally {
    if (Test-Path -LiteralPath $pfxPath) { Remove-Item -LiteralPath $pfxPath -Force }
}
