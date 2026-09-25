import { chromium } from "@playwright/test";

const baseURL = process.env.REMOTELINK_TEST_URL;
const token = process.env.REMOTELINK_TEST_TOKEN;
const host = process.env.REMOTELINK_TEST_SSH_HOST;
const port = Number(process.env.REMOTELINK_TEST_SSH_PORT || 22);
const username = process.env.REMOTELINK_TEST_SSH_USERNAME;
const password = process.env.REMOTELINK_TEST_SSH_PASSWORD || "";
const privateKey = process.env.REMOTELINK_TEST_SSH_PRIVATE_KEY || "";
const passphrase = process.env.REMOTELINK_TEST_SSH_PASSPHRASE || "";
if (!baseURL || !token || !host || !username)
  throw new Error("SSH integration environment is incomplete");
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
    async ({
      headers,
      host,
      port,
      username,
      password,
      privateKey,
      passphrase,
    }) => {
      const response = await fetch("/api/admin/ssh/save", {
        method: "POST",
        headers,
        body: JSON.stringify({
          name: "SSH integration",
          host,
          port,
          username,
          authType: privateKey ? "key" : "password",
          password,
          privateKey,
          passphrase,
        }),
      });
      if (!response.ok) throw new Error(await response.text());
      return (await response.json()).id;
    },
    { headers, host, port, username, password, privateKey, passphrase },
  );
  const fingerprint = await page.evaluate(
    async ({ headers, targetId, host, port, username }) => {
      const tested = await fetch("/api/admin/ssh/test", {
        method: "POST",
        headers,
        body: JSON.stringify({ id: targetId, host, port, username }),
      });
      if (!tested.ok) throw new Error(await tested.text());
      const hostKeySha256 = (await tested.json()).hostKeySha256;
      const confirmed = await fetch("/api/admin/ssh/trust/confirm", {
        method: "POST",
        headers,
        body: JSON.stringify({ id: targetId, fingerprint: hostKeySha256 }),
      });
      if (!confirmed.ok) throw new Error(await confirmed.text());
      return hostKeySha256;
    },
    { headers, targetId, host, port, username },
  );
  if (!fingerprint.startsWith("SHA256:"))
    throw new Error("SSH host fingerprint was not confirmed");
  const session = await page.evaluate(
    async ({ headers, targetId }) => {
      const response = await fetch("/api/ssh/sessions", {
        method: "POST",
        headers,
        body: JSON.stringify({ targetId }),
      });
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    },
    { headers, targetId },
  );
  const output = await page.evaluate(
    ({ url }) =>
      new Promise((resolve, reject) => {
        const protocol = location.protocol === "https:" ? "wss:" : "ws:";
        const socket = new WebSocket(`${protocol}//${location.host}${url}`);
        socket.binaryType = "arraybuffer";
        let text = "";
        const timer = setTimeout(
          () => reject(new Error(`terminal timeout: ${text}`)),
          10000,
        );
        socket.onopen = () =>
          socket.send(
            new TextEncoder().encode("printf REMOTELINK_SSH_OK\\n\nexit\n"),
          );
        socket.onmessage = (event) => {
          text += new TextDecoder().decode(event.data);
          if (text.includes("REMOTELINK_SSH_OK")) {
            clearTimeout(timer);
            socket.close();
            resolve(text);
          }
        };
        socket.onerror = () => {
          clearTimeout(timer);
          reject(new Error("SSH WebSocket failed"));
        };
      }),
    { url: session.websocketUrl },
  );
  if (!output.includes("REMOTELINK_SSH_OK"))
    throw new Error("SSH command output missing");
  const trustedFingerprint = await page.evaluate(
    async ({ headers, targetId }) => {
      const response = await fetch("/api/admin/ssh", { headers });
      const item = (await response.json()).connections.find(
        (value) => value.id === targetId,
      );
      return item?.hostKeySha256 || "";
    },
    { headers, targetId },
  );
  if (trustedFingerprint !== fingerprint)
    throw new Error("SSH host fingerprint was not pinned");
  console.log(`SSH ${privateKey ? "key" : "password"} integration passed`);
} finally {
  if (targetId)
    await page
      .evaluate(
        async ({ headers, targetId }) => {
          await fetch("/api/admin/ssh/delete", {
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
