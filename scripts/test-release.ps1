$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$wslProject = (wsl.exe wslpath -a ($project -replace '\\','/')).Trim()
wsl.exe bash -lc "cd '$wslProject' && cmake --build build -j2 && ctest --test-dir build --output-on-failure"
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }
Push-Location $project
try {
  npx playwright test --project=chromium --project=webkit
  if ($LASTEXITCODE -ne 0) { throw 'Browser tests failed' }
  if ($env:REMOTELINK_TEST_URL -and $env:REMOTELINK_TEST_TOKEN -and $env:REMOTELINK_TEST_VNC_HOST) {
    node tests/vnc_proxy_integration.mjs
    if ($LASTEXITCODE -ne 0) { throw 'VNC integration failed' }
  }
  if ($env:REMOTELINK_TEST_URL -and $env:REMOTELINK_TEST_TOKEN -and $env:REMOTELINK_TEST_SSH_HOST) {
    node tests/ssh_proxy_integration.mjs
    if ($LASTEXITCODE -ne 0) { throw 'SSH integration failed' }
  }
} finally { Pop-Location }
