import { chromium } from '@playwright/test';

const baseURL = process.env.REMOTELINK_TEST_URL;
const token = process.env.REMOTELINK_TEST_TOKEN;
const host = process.env.REMOTELINK_TEST_VNC_HOST;
const port = Number(process.env.REMOTELINK_TEST_VNC_PORT || 5909);
const password = process.env.REMOTELINK_TEST_VNC_PASSWORD || '';
const username = process.env.REMOTELINK_TEST_VNC_USERNAME || '';
const caFile = process.env.REMOTELINK_TEST_VNC_CA_FILE || '';
if (!baseURL || !token || !host) throw new Error('integration test environment is incomplete');

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({ ignoreHTTPSErrors: true });
const page = await context.newPage();
const headers = { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' };
let targetId = '';
try {
  await page.goto(`${baseURL}/vnc`);
  targetId = await page.evaluate(async ({ headers, host, port, username, password, caFile }) => {
    const response = await fetch('/api/admin/vnc/save', {
      method: 'POST', headers, body: JSON.stringify({ name: 'Proxy integration test', host, port, username, password, caFile, viewOnly: false })
    });
    if (!response.ok) throw new Error(`create target failed: ${response.status} ${await response.text()}`);
    return (await response.json()).id;
  }, { headers, host, port, username, password, caFile });
  const session = await page.evaluate(async ({ headers, targetId }) => {
    const response = await fetch('/api/vnc/sessions', {
      method: 'POST', headers, body: JSON.stringify({ targetId })
    });
    if (!response.ok) throw new Error(`create session failed: ${response.status} ${await response.text()}`);
    return response.json();
  }, { headers, targetId });
  if ('password' in session) throw new Error('VNC password leaked in session response');
  await page.evaluate(value => sessionStorage.setItem('remote-gateway-access-token', value), token);
  await page.goto(`${baseURL}/vnc/session?target=${encodeURIComponent(targetId)}&name=Integration%20VNC`);
  try {
    await page.locator('#status').waitFor({ state: 'hidden', timeout: 12000 });
  } catch (error) {
    const diagnostics = await page.locator('#status-title, #status-detail').allTextContents();
    throw new Error(`noVNC did not connect: ${diagnostics.join(' | ')} (${error.message})`);
  }
  console.log('VNC WebSocket/RFB proxy and noVNC Core integration passed');
} finally {
  if (targetId) {
    await page.evaluate(async ({ headers, targetId }) => {
      await fetch('/api/admin/vnc/delete', { method: 'POST', headers, body: JSON.stringify({ id: targetId }) });
    }, { headers, targetId }).catch(() => {});
  }
  await browser.close();
}
