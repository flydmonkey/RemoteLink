import { expect, test } from "@playwright/test";
import { readFileSync } from "node:fs";

const pages = [
  "/connect.html", "/admin.html", "/settings.html", "/index.html",
  "/vnc.html", "/vnc-session.html", "/ssh.html", "/ssh-session.html",
  "/telnet.html", "/telnet-session.html", "/users.html",
  "/guacamole-session.html",
];

test("every Chinese source literal can be translated", async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem("remotelink-language", "en"));
  await page.goto("/connect.html");
  const sources = new Set();
  for (const path of pages.filter((value) => !["/index.html", "/settings.html"].includes(value))) {
    const source = readFileSync(new URL(`../../web${path}`, import.meta.url), "utf8");
    for (const match of source.matchAll(/["'`]([^"'`\r\n]*\p{Script=Han}[^"'`\r\n]*)["'`]/gu))
      sources.add(match[1]);
  }
  const untranslated = await page.evaluate((values) => values.filter((value) =>
    window.RemoteLinkI18n.t(value) === value), [...sources]);
  expect(untranslated, untranslated.join("\n")).toEqual([]);
});

test("every web module translates visible Chinese copy to English", async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem("remotelink-language", "en");
    sessionStorage.setItem("remotelink-access-token", "i18n-audit");
    sessionStorage.setItem("remotelink-session", JSON.stringify({ target: "rdp-audit", accessToken: "i18n-audit" }));
    sessionStorage.setItem("remotelink-vnc-session", JSON.stringify({ websocketUrl: "/vnc/ws?ticket=audit" }));
    sessionStorage.setItem("remotelink-ssh-session", JSON.stringify({ websocketUrl: "/ssh/ws?ticket=audit", targetId: "ssh-audit" }));
    sessionStorage.setItem("remotelink-telnet-session", JSON.stringify({ websocketUrl: "/telnet/ws?ticket=audit" }));
    sessionStorage.setItem("remotelink-guacamole-session", JSON.stringify({ websocketUrl: "/guacamole/ws?ticket=audit" }));
  });
  await page.route("**/api/**", async (route) => {
    const path = new URL(route.request().url()).pathname;
    const payload = path === "/api/auth/me"
      ? { username: "admin", name: "Administrator", admin: true }
      : path.endsWith("/activity") ? { active: [], history: [] }
      : path === "/api/admin/users" ? { items: [] }
      : path === "/api/admin/state" ? { targets: [], events: [], sessions: 0 }
      : path.startsWith("/api/admin/") ? { connections: [], targets: [] }
      : { targets: [] };
    await route.fulfill({ json: payload });
  });
  const missing = [];
  for (const path of pages) {
    await page.goto(path);
    await page.waitForTimeout(100);
    const values = await page.evaluate(() => {
      const found = new Set();
      const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
      while (walker.nextNode()) {
        if (["SCRIPT", "STYLE"].includes(walker.currentNode.parentElement?.tagName)) continue;
        if (walker.currentNode.parentElement?.closest("#account-language")) continue;
        const value = walker.currentNode.nodeValue.trim();
        if (/\p{Script=Han}/u.test(value)) found.add(value);
      }
      for (const element of document.querySelectorAll("[title],[aria-label],[placeholder]"))
        for (const name of ["title", "aria-label", "placeholder"]) {
          const value = element.getAttribute(name)?.trim();
          if (value && /\p{Script=Han}/u.test(value)) found.add(value);
        }
      return [...found];
    });
    missing.push(...values.map((value) => `${path}: ${value}`));
  }
  expect(missing, missing.join("\n")).toEqual([]);
});

test("every localized module has entries for every supported locale", async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem("remotelink-language", "zh-CN");
    sessionStorage.setItem("remotelink-access-token", "i18n-audit");
    sessionStorage.setItem("remotelink-session", JSON.stringify({ target: "rdp-audit", accessToken: "i18n-audit" }));
    sessionStorage.setItem("remotelink-vnc-session", JSON.stringify({ websocketUrl: "/vnc/ws?ticket=audit" }));
    sessionStorage.setItem("remotelink-ssh-session", JSON.stringify({ websocketUrl: "/ssh/ws?ticket=audit", targetId: "ssh-audit" }));
    sessionStorage.setItem("remotelink-telnet-session", JSON.stringify({ websocketUrl: "/telnet/ws?ticket=audit" }));
    sessionStorage.setItem("remotelink-guacamole-session", JSON.stringify({ websocketUrl: "/guacamole/ws?ticket=audit" }));
  });
  await page.route("**/api/**", (route) => route.fulfill({ json: { username: "admin", name: "Administrator", admin: true, targets: [], connections: [], active: [], history: [], items: [], events: [], sessions: 0 } }));
  const missing = new Map();
  for (const path of pages) {
    await page.goto(path);
    await page.waitForTimeout(50);
    const values = await page.evaluate(() => {
      const found = new Set();
      const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
      while (walker.nextNode()) {
        if (["SCRIPT", "STYLE"].includes(walker.currentNode.parentElement?.tagName)) continue;
        if (walker.currentNode.parentElement?.closest("#account-language")) continue;
        const value = walker.currentNode.nodeValue.trim();
        if (/\p{Script=Han}/u.test(value)) found.add(value);
      }
      for (const element of document.querySelectorAll("[title],[aria-label],[placeholder]"))
        for (const name of ["title", "aria-label", "placeholder"]) {
          const value = element.getAttribute(name)?.trim();
          if (value && /\p{Script=Han}/u.test(value)) found.add(value);
        }
      return [...found];
    });
    for (const value of values) {
      const translations = await page.evaluate((source) =>
        Object.fromEntries(["zh-TW", "en", "ja", "ko"].map((locale) =>
          [locale, window.RemoteLinkI18n.hasTranslation(source, locale)])), value);
      for (const [locale, translated] of Object.entries(translations)) {
        if (translated) continue;
        const entry = missing.get(value) || { paths: new Set(), locales: new Set() };
        entry.paths.add(path);
        entry.locales.add(locale);
        missing.set(value, entry);
      }
    }
  }
  const report = [...missing.entries()].map(([value, entry]) =>
    `${value} | ${[...entry.locales].join(",")} | ${[...entry.paths].join(",")}`);
  expect(report, report.join("\n")).toEqual([]);
});

test("dynamic Telnet errors, credential state, and confirmation dialogs are localized", async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem("remotelink-language", "en");
    sessionStorage.setItem("remotelink-access-token", "i18n-audit");
  });
  await page.route("**/api/auth/me", (route) => route.fulfill({ json: { username: "admin", admin: true } }));
  await page.route("**/api/telnet/targets", (route) => route.fulfill({
    json: { targets: [{ id: "router", name: "openwrt", host: "192.0.2.1", port: 23, username: "test", hasPassword: true }] },
  }));
  await page.route("**/api/admin/telnet", (route) => route.fulfill({
    json: { connections: [{ id: "router", name: "openwrt", host: "192.0.2.1", port: 23, username: "test", hasPassword: true }] },
  }));
  await page.route("**/api/admin/telnet/activity", (route) => route.fulfill({ json: { active: [], history: [] } }));
  await page.route("**/api/telnet/sessions", (route) => route.fulfill({ status: 502, json: { error: "无法连接 Telnet 目标" } }));
  await page.goto("/telnet.html");
  await expect(page.locator("#list .muted")).toContainText("Credentials configured (not displayed)");
  await page.getByRole("button", { name: "Connect" }).click();
  await expect(page.locator("#notice")).toHaveText("Unable to connect to the Telnet target");
  const dialogMessage = new Promise((resolve) => page.once("dialog", async (dialog) => {
    resolve(dialog.message());
    await dialog.dismiss();
  }));
  await page.evaluate(() => document.querySelector("#manager").showModal());
  await page.getByRole("button", { name: "Clear credentials" }).click();
  expect(await dialogMessage).toBe("Clear the saved sign-in credentials for “openwrt”?");
});
