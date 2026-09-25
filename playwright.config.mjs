import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests/browser',
  timeout: 20_000,
  use: { baseURL: 'http://127.0.0.1:4173', headless: true },
  webServer: { command: 'node tests/browser/static-server.mjs', port: 4173, reuseExistingServer: true },
  projects: [
    { name: 'chromium', use: { browserName: 'chromium' } },
    { name: 'firefox', use: { browserName: 'firefox' } },
    { name: 'webkit', use: { browserName: 'webkit' } }
  ]
});
