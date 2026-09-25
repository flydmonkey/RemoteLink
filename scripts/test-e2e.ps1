$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$compose = Join-Path $project 'tests/protocol-stack.compose.yml'
$usePodman = $false
$createdContainers = @()
$dockerVersion = (wsl.exe bash -lc 'docker --version 2>/dev/null').Trim()
$composeStarted = $false
if ($dockerVersion -notmatch 'podman') {
  wsl.exe bash -lc "docker compose -f '$(wsl.exe wslpath -a ($compose -replace '\\','/'))' up -d"
  $composeStarted = $LASTEXITCODE -eq 0
}
if (-not $composeStarted) {
  wsl.exe bash -lc 'podman info >/dev/null 2>&1'
  if ($LASTEXITCODE -ne 0) { throw 'Unable to start RDP/VNC/SSH test stack with Docker Compose or Podman' }
  $usePodman = $true
  $podmanAddress = ((wsl.exe bash -lc "hostname -I | awk '{print `$1}'").Trim())
  if (-not $podmanAddress) { throw 'Unable to resolve the WSL test address' }
  $containers = @(
    @{ Name = 'remotelink-rdp-test'; Args = "-p ${podmanAddress}:13389:3389 docker.io/danielguerra/ubuntu-xrdp:latest" },
    @{ Name = 'remotelink-vnc-test'; Args = '-e VNC_PASSWORD=remotelink -e RESOLUTION=1920x1080 -p 127.0.0.1:15900:5900 docker.io/dorowu/ubuntu-desktop-lxde-vnc:latest' },
    @{ Name = 'remotelink-ssh-test'; Args = '-e PUID=1000 -e PGID=1000 -e USER_NAME=test -e USER_PASSWORD=remotelink -e PASSWORD_ACCESS=true -p 127.0.0.1:12222:2222 lscr.io/linuxserver/openssh-server:latest' }
  )
  foreach ($container in $containers) {
    wsl.exe bash -lc "podman container exists '$($container.Name)'"
    if ($LASTEXITCODE -eq 0) {
      wsl.exe bash -lc "podman start '$($container.Name)' >/dev/null"
    } else {
      wsl.exe bash -lc "podman run -d --name '$($container.Name)' $($container.Args) >/dev/null"
      if ($LASTEXITCODE -ne 0) { throw "Unable to start $($container.Name)" }
      $createdContainers += $container.Name
    }
  }
}
try {
  foreach ($port in 13389, 15900, 12222) {
    $ready = $false
    $checkHost = if ($usePodman -and $port -eq 13389) { $podmanAddress } else { '127.0.0.1' }
    foreach ($attempt in 1..60) {
      if (Test-NetConnection $checkHost -Port $port -InformationLevel Quiet -WarningAction SilentlyContinue) {
        $ready = $true
        break
      }
      Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw "Protocol test service on port $port did not become ready" }
  }
  if (-not $env:REMOTELINK_TEST_URL) { $env:REMOTELINK_TEST_URL = 'https://127.0.0.1:18080' }
  if (-not $env:REMOTELINK_TEST_TOKEN) {
    $testUser = if ($env:REMOTELINK_TEST_ADMIN_USERNAME) { $env:REMOTELINK_TEST_ADMIN_USERNAME } else { 'admin' }
    $testPassword = if ($env:REMOTELINK_TEST_ADMIN_PASSWORD) { $env:REMOTELINK_TEST_ADMIN_PASSWORD } else { 'admin' }
    $env:NODE_TLS_REJECT_UNAUTHORIZED = '0'
    $env:REMOTELINK_TEST_TOKEN = node (Join-Path $project 'tests/get_test_token.mjs') $env:REMOTELINK_TEST_URL $testUser $testPassword
    if ($LASTEXITCODE -ne 0 -or -not $env:REMOTELINK_TEST_TOKEN) { throw 'Unable to authenticate to the local RemoteLink service' }
  }
  if ($usePodman) {
    $env:REMOTELINK_TEST_RDP_HOST = $podmanAddress
    $env:REMOTELINK_TEST_RDP_PORT = '13389'
  } else {
    $env:REMOTELINK_TEST_RDP_HOST = (wsl.exe bash -lc "docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' remotelink-rdp-test").Trim()
    $env:REMOTELINK_TEST_RDP_PORT = '3389'
  }
  if (-not $env:REMOTELINK_TEST_RDP_HOST) { throw 'Unable to resolve the RDP test container address' }
  $env:REMOTELINK_TEST_VNC_HOST = '127.0.0.1'
  $env:REMOTELINK_TEST_VNC_PORT = '15900'
  $env:REMOTELINK_TEST_VNC_PASSWORD = 'remotelink'
  $env:REMOTELINK_TEST_SSH_HOST = '127.0.0.1'
  $env:REMOTELINK_TEST_SSH_PORT = '12222'
  $env:REMOTELINK_TEST_SSH_USERNAME = 'test'
  $env:REMOTELINK_TEST_SSH_PASSWORD = 'remotelink'
  & (Join-Path $PSScriptRoot 'test-release.ps1')
} finally {
  if ($env:REMOTELINK_KEEP_TEST_STACK -ne '1') {
    if ($usePodman) {
      foreach ($name in $createdContainers) {
        wsl.exe bash -lc "podman rm -f '$name' >/dev/null"
      }
    } else {
      wsl.exe bash -lc "docker compose -f '$(wsl.exe wslpath -a ($compose -replace '\\','/'))' down"
    }
  }
}
