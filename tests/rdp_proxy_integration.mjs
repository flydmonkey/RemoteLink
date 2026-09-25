import { chromium } from "@playwright/test";

const baseURL = process.env.REMOTELINK_TEST_URL;
const token = process.env.REMOTELINK_TEST_TOKEN;
const host = process.env.REMOTELINK_TEST_RDP_HOST;
const port = Number(process.env.REMOTELINK_TEST_RDP_PORT || 3389);
if (!baseURL || !token || !host)
  throw new Error("RDP integration environment is incomplete");
const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({ ignoreHTTPSErrors: true });
const page = await context.newPage();
const headers = {
  Authorization: `Bearer ${token}`,
  "Content-Type": "application/json",
};
let targetId = "";
try {
  await page.goto(`${baseURL}/healthz`);
  targetId = await page.evaluate(
    async ({ headers, host, port }) => {
      const response = await fetch("/api/admin/connections/create", {
        method: "POST",
        headers,
        body: JSON.stringify({
          name: "RDP integration",
          host,
          port,
          username: "integration",
          password: "managed-secret",
          width: 1280,
          height: 720,
          ignoreCertificate: true,
        }),
      });
      if (!response.ok) throw new Error(await response.text());
      return (await response.json()).id;
    },
    { headers, host, port },
  );
  const reachable = await page.evaluate(
    async ({ headers, targetId }) => {
      const response = await fetch("/api/admin/connections/test", {
        method: "POST",
        headers,
        body: JSON.stringify({ id: targetId }),
      });
      return response.ok && (await response.json()).reachable;
    },
    { headers, targetId },
  );
  if (!reachable) throw new Error("RDP test container is unreachable");
  const credentialState = async () =>
    page.evaluate(
      async ({ headers, targetId }) => {
        const response = await fetch("/api/admin/state", { headers });
        return (await response.json()).targets.find(
          (item) => item.id === targetId,
        );
      },
      { headers, targetId },
    );
  const before = await credentialState();
  if (!before?.hasPassword || "password" in before)
    throw new Error("RDP credential status is not safely exposed");
  await page.evaluate(
    async ({ headers, targetId }) => {
      const response = await fetch("/api/admin/connections/credentials/clear", {
        method: "POST",
        headers,
        body: JSON.stringify({ id: targetId }),
      });
      if (!response.ok) throw new Error(await response.text());
    },
    { headers, targetId },
  );
  if ((await credentialState())?.hasPassword)
    throw new Error("RDP credential was not cleared");
  console.log("RDP reachability and credential lifecycle integration passed");
} finally {
  if (targetId)
    await page
      .evaluate(
        async ({ headers, targetId }) => {
          await fetch("/api/admin/connections/delete", {
            method: "POST",
            headers,
            body: JSON.stringify({ id: targetId }),
          });
        },
        { headers, targetId },
      )
      .catch(() => {});
  await browser.close();
}
