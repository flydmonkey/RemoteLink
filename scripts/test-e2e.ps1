$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$compose = Join-Path $project 'tests/protocol-stack.compose.yml'
wsl.exe bash -lc "docker compose -f '$(wsl.exe wslpath -a ($compose -replace '\\','/'))' up -d --wait"
if ($LASTEXITCODE -ne 0) { throw 'Unable to start RDP/VNC/SSH test stack' }
try {
  & (Join-Path $PSScriptRoot 'test-release.ps1')
} finally {
  if ($env:REMOTELINK_KEEP_TEST_STACK -ne '1') {
    wsl.exe bash -lc "docker compose -f '$(wsl.exe wslpath -a ($compose -replace '\\','/'))' down"
  }
}
