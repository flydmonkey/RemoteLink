import { test, expect } from '@playwright/test';

test('session starts with a black localized loading screen', async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem('remotelink-language', 'en');
    sessionStorage.setItem('remotelink-session', JSON.stringify({target:'test',accessToken:'test'}));
  });
  await page.goto('/index.html');
  await expect(page.locator('#session-loading')).toBeVisible();
  await expect(page.locator('#session-loading-text')).toHaveText('Connecting…');
  await expect(page.locator('#paste-text .toolbar-tooltip')).toHaveText('Paste text');
  await expect(page.locator('#download-print')).toHaveJSProperty('hidden', false);
  await expect(page.locator('html')).toHaveCSS('background-color', 'rgb(0, 0, 0)');
});

test('file mapping is opt-in and automatic resolution is displayed', async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem('remotelink-language', 'en'));
  await page.goto('/settings.html');
  await expect(page.locator('#files')).not.toBeChecked();
  await expect(page.locator('#resolution option').first()).toContainText('Automatic');
});
