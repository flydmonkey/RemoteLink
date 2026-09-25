import { test, expect } from "@playwright/test";

test("RDP page exposes the protocol navigation", async ({ page }) => {
  await page.goto("/connect.html");
  await expect(page).toHaveTitle("RemoteLink · RDP");
  await expect(page.locator(".protocol-nav")).toContainText("RDP");
  await expect(page.locator(".protocol-nav")).toContainText("VNC");
  await expect(page.locator(".protocol-nav")).toContainText("SSH");
  await expect(page.locator('.protocol-link[aria-current="page"]')).toHaveText(
    "RDP",
  );
  await expect(page.locator(".account-menu")).not.toContainText("设置");
  await expect(page.locator(".account-menu")).not.toContainText("管理控制台");
  await expect(page.locator(".account-admin")).toHaveText(
    /用户管理|User management/,
  );
  await expect(page.locator(".account-admin")).toHaveAttribute(
    "href",
    "/users",
  );
  await expect(page.locator("#account-avatar svg")).toHaveCount(1);
  await expect(page.locator("#account-name")).toBeHidden();
  await expect(page.locator("#account-avatar")).toHaveCSS(
    "color",
    "rgb(179, 179, 179)",
  );
  await expect(page.locator("#account-avatar")).toHaveCSS(
    "background-color",
    "rgb(54, 54, 54)",
  );
  await expect(page.locator("#connection-form #username")).toHaveCount(0);
  await expect(page.locator("#connection-form #password")).toHaveCount(0);
  await expect(page.locator("#connection-form #remember")).toHaveCount(0);
  await expect(page.locator("#connection-form #forget")).toHaveCount(0);
});

test("VNC page follows the existing RemoteLink shell", async ({ page }) => {
  await page.addInitScript(() =>
    sessionStorage.setItem("remote-gateway-access-token", "test"),
  );
  await page.route("**/api/auth/me", (route) =>
    route.fulfill({
      json: { name: "Test User", username: "test", admin: false },
    }),
  );
  await page.route("**/api/vnc/targets", (route) =>
    route.fulfill({
      json: {
        targets: [
          { id: "first-vnc", name: "First VNC", viewOnly: false },
          { id: "second-vnc", name: "Second VNC", viewOnly: true },
        ],
      },
    }),
  );
  await page.goto("/vnc.html");
  await expect(page).toHaveTitle("RemoteLink · VNC");
  await expect(page.locator(".brand")).toHaveText("RemoteLink");
  await expect(page.locator(".brand .mark")).toHaveCount(0);
  await expect(page.locator('.protocol-nav a[aria-current="page"]')).toHaveText(
    "VNC",
  );
  await expect(
    page.getByRole("heading", { name: "VNC 连接", exact: true }),
  ).toBeVisible();
  await expect(
    page.locator('.hero-icon svg[data-protocol-icon="vnc"]'),
  ).toHaveCount(1);
  await expect(page.locator(".hero p")).toHaveText(
    /选择计算机并连接|Select a computer and connect/,
  );
  await expect(page.getByText("我的连接", { exact: true })).toBeVisible();
  await expect(page.locator("#target")).toBeVisible();
  await expect(page.locator("#target")).toHaveValue("first-vnc");
  await expect(page.locator("#admin-nav")).toBeHidden();
  await expect(
    page.getByRole("button", { name: "管理", exact: true }),
  ).toBeHidden();
  await expect(page.locator("#users-link")).toBeHidden();
  await expect(page.locator(".title-actions")).toHaveCSS("gap", "2px");
  await expect(page.locator(".app")).toHaveCSS("height", "270px");
});

test("VNC administrators add connections from management only", async ({
  page,
}) => {
  await page.addInitScript(() =>
    sessionStorage.setItem("remote-gateway-access-token", "test"),
  );
  await page.route("**/api/auth/me", (route) =>
    route.fulfill({
      json: { name: "Administrator", username: "admin", admin: true },
    }),
  );
  await page.route("**/api/vnc/targets", (route) =>
    route.fulfill({ json: { targets: [] } }),
  );
  await page.route("**/api/admin/vnc", (route) =>
    route.fulfill({ json: { connections: [] } }),
  );
  await page.route("**/api/admin/vnc/activity", (route) =>
    route.fulfill({
      json: {
        active: [
          {
            id: "s1",
            targetName: "Live Desktop",
            username: "test",
            startedAt: 1700000000,
            endedAt: 0,
          },
        ],
        history: [
          {
            id: "s0",
            targetName: "Old Desktop",
            username: "test",
            startedAt: 1699990000,
            endedAt: 1699991000,
          },
        ],
      },
    }),
  );
  await page.route("**/api/admin/vnc/test", (route) =>
    route.fulfill({ json: { reachable: true, authenticated: true } }),
  );
  await page.route("**/api/admin/vnc/save", (route) =>
    route.fulfill({ json: { id: "vnc-test" } }),
  );
  await page.goto("/vnc.html");
  await expect(page.getByRole("button", { name: "添加 VNC 连接" })).toHaveCount(
    0,
  );
  await expect(page.locator("#connect-form .primary")).toHaveCSS(
    "min-width",
    "124px",
  );
  await expect(page.locator(".account-menu")).not.toContainText("设置");
  await expect(page.locator(".account-menu")).not.toContainText("管理控制台");
  await expect(page.locator("#users-link")).toHaveText(
    /用户管理|User management/,
  );
  await expect(page.locator("#users-link")).toHaveAttribute("href", "/users");
  await expect(page.locator("#account-trigger svg")).toHaveCount(1);
  await expect(page.locator("#account-trigger .avatar-icon")).toHaveCSS(
    "color",
    "rgb(179, 179, 179)",
  );
  await expect(page.locator("#account-trigger .avatar-icon")).toHaveCSS(
    "background-color",
    "rgb(54, 54, 54)",
  );
  await expect(page.locator("#account-name")).toHaveText(
    "Administrator · admin",
  );
  await page.getByRole("button", { name: "管理", exact: true }).click();
  await expect(page.locator(".admin-title")).toBeVisible();
  await expect(page.locator(".admin-title")).toContainText("管理");
  await expect(page.locator(".admin-title a")).toHaveAttribute("href", "/vnc");
  await expect(page.locator(".admin-title a svg")).toBeVisible();
  await expect(page.locator(".titlebar > .brand")).toBeHidden();
  await expect(page.locator(".titlebar > .protocol-nav")).toBeHidden();
  await expect(page.locator(".titlebar > .title-actions")).toBeHidden();
  await expect(page.locator("#admin-nav")).toBeVisible();
  await expect(
    page.getByRole("heading", { name: "VNC 连接管理" }),
  ).toBeVisible();
  await expect(page.getByRole("link", { name: "连接管理" })).toHaveAttribute(
    "href",
    "#connections",
  );
  await page.getByRole("button", { name: "添加连接", exact: true }).click();
  await expect(page.locator("#editor")).toBeVisible();
  await expect(page.locator("#editor").getByLabel("主机")).toBeVisible();
  await expect(
    page.locator("#editor").getByLabel(/用户名|Username/),
  ).toBeVisible();
  await expect(page.locator("#editor").getByLabel("CA 证书")).toBeVisible();
  await page.locator("#editor").getByLabel("名称").fill("Test VNC");
  await page.locator("#editor").getByLabel("主机").fill("192.168.1.10");
  await page
    .locator("#editor")
    .getByRole("button", { name: "测试连接" })
    .click();
  await expect(page.locator("#editor-notice")).toHaveText(
    "连接和身份验证成功。",
  );
  await page.locator("#editor").getByRole("button", { name: "保存" }).click();
  await expect(page.locator("#editor")).toBeHidden();
  await expect(page.getByRole("link", { name: "用户授权" })).toHaveCount(0);
  await page.getByRole("link", { name: /活动会话|Active sessions/ }).click();
  await expect(page.getByText("Live Desktop")).toBeVisible();
  await page.getByRole("link", { name: "连接历史" }).click();
  await expect(page.getByText("Old Desktop")).toBeVisible();
  await page.goto("/vnc.html?admin=1");
  await expect(
    page.getByRole("heading", { name: "VNC 连接管理" }),
  ).toBeVisible();
});

test("SSH page exposes password and key connection management", async ({
  page,
}) => {
  await page.addInitScript(() =>
    sessionStorage.setItem("remote-gateway-access-token", "test"),
  );
  await page.route("**/api/auth/me", (route) =>
    route.fulfill({ json: { admin: true } }),
  );
  await page.route("**/api/ssh/targets", (route) =>
    route.fulfill({ json: { targets: [{ id: "ssh-1", name: "Linux" }] } }),
  );
  await page.route("**/api/admin/ssh", (route) =>
    route.fulfill({ json: { connections: [] } }),
  );
  await page.route("**/api/admin/ssh/activity", (route) =>
    route.fulfill({ json: { active: [], history: [] } }),
  );
  await page.goto("/ssh.html");
  await expect(page.locator(".brand")).toHaveText("RemoteLink");
  await expect(page.locator(".brand")).toHaveCSS("font-weight", "600");
  await expect(page.getByRole("heading", { name: "SSH 连接" })).toHaveCSS(
    "font-weight",
    "600",
  );
  await expect(page.locator(".app")).toHaveCSS("height", "270px");
  await expect(page.locator('.nav a[aria-current="page"]')).toHaveText("SSH");
  await expect(page.getByRole("heading", { name: "SSH 连接" })).toBeVisible();
  await expect(page.locator("#target")).toHaveValue("ssh-1");
  await expect(page.locator("#target option")).toHaveCount(1);
  await expect(page.locator("#target option")).toHaveText("Linux");
  await expect(page.locator("#manage")).toHaveAttribute(
    "href",
    "/ssh/settings",
  );
  await expect(page.locator("#manage")).toHaveAttribute("aria-label", "管理");
  await expect(page.locator("#manage path")).toHaveAttribute(
    "d",
    "M3 12h4l2-6 4 12 2-6h6",
  );
  await expect(page.locator("#manage circle")).toHaveCount(0);
  await expect(page.locator("#manage")).toHaveCSS("width", "34px");
  await expect(page.locator("#account-trigger")).toHaveCSS("width", "34px");
  await page.locator("#account-trigger").click();
  await expect(page.locator("#account-menu")).toBeVisible();
  await expect(page.locator("#logout")).toHaveText("退出登录");
  await page.evaluate(() => document.querySelector("#manager").showModal());
  await expect(page.locator(".settings-title a svg")).toBeVisible();
  await expect(page.locator(".settings-title a")).toHaveCSS("width", "34px");
  await page.getByRole("button", { name: "添加连接" }).click();
  await expect(page.locator("#key-fields")).toBeHidden();
  await expect(page.locator("#password")).toBeVisible();
  await page.locator("#auth").selectOption("key");
  await expect(page.locator("#key-fields")).toBeVisible();
  await expect(page.locator("#password")).toBeHidden();
  await expect(
    page.getByRole("option", { name: "用户名和密码" }),
  ).toBeAttached();
  await expect(page.getByRole("option", { name: "SSH 私钥" })).toBeAttached();
});

test("VNC session uses noVNC Core with RemoteLink controls", async ({
  page,
}) => {
  await page.addInitScript(() =>
    sessionStorage.setItem("remote-gateway-access-token", "test"),
  );
  await page.route("**/api/vnc/sessions", (route) =>
    route.fulfill({
      json: {
        websocketUrl: "/vnc/ws?ticket=test",
        password: "",
        viewOnly: false,
      },
    }),
  );
  await page.route("**/vendor/novnc/core/rfb.js", (route) =>
    route.fulfill({
      contentType: "application/javascript",
      body: 'export default class RFB extends EventTarget { constructor(target){super();window.testRfb=this;this.scaleViewport=false;this.resizeSession=false;this.viewOnly=false;this._canvas=document.createElement("canvas");this._cursor={_canvas:document.createElement("canvas"),change(){}};target.append(this._canvas)} sendCtrlAltDel(){} clipboardPasteFrom(){} disconnect(){} }',
    }),
  );
  await page.goto("/vnc-session.html?target=test&name=Test%20VNC");
  await expect(page.locator(".bar")).toHaveCount(0);
  await expect(
    page.getByRole("button", { name: "Ctrl+Alt+Del" }),
  ).toBeVisible();
  await expect(page.getByRole("button", { name: "剪贴板" })).toBeVisible();
  await expect(page.getByRole("button", { name: "全屏" })).toBeVisible();
  await expect(page.getByRole("button", { name: "缩放" })).toHaveCount(0);
  await expect(page.locator("#remote-toolbar")).toHaveCSS(
    "flex-direction",
    "column",
  );
  await expect(page.locator("#remote-toolbar")).toHaveCSS("width", "48px");
  await expect(page.locator("#screen canvas")).toHaveCSS("cursor", "default");
  await page.getByRole("button", { name: "收起工具栏" }).click();
  await expect(page.locator("#remote-toolbar")).toHaveClass(/collapsed/);
  await expect(page.getByRole("button", { name: "展开工具栏" })).toBeVisible();
  await page.evaluate(() =>
    window.testRfb.dispatchEvent(
      new CustomEvent("disconnect", { detail: { clean: false } }),
    ),
  );
  await expect(page.getByRole("button", { name: "重新连接" })).toBeVisible();
  await expect(
    page.getByRole("link", { name: "返回连接列表" }),
  ).toHaveAttribute("href", "/vnc");
  await page.locator("#disconnect").evaluate((button) => button.click());
  await expect(page).toHaveURL(/\/vnc$/);
});

test("VNC disconnect returns even when the target is unavailable", async ({
  page,
}) => {
  await page.addInitScript(() =>
    sessionStorage.setItem("remote-gateway-access-token", "test"),
  );
  await page.route("**/api/vnc/sessions", (route) =>
    route.fulfill({ status: 502, json: { error: "unavailable" } }),
  );
  await page.goto("/vnc-session.html?target=offline").catch(() => {});
  await expect(page.getByText("无法创建 VNC 会话")).toBeVisible();
  await page.locator("#disconnect").evaluate((button) => button.click());
  await expect(page).toHaveURL(/\/vnc$/);
});

test("VNC legacy management UI is no longer duplicated", async ({ page }) => {
  await page.addInitScript(() =>
    sessionStorage.setItem("remote-gateway-access-token", "test"),
  );
  await page.route("**/api/auth/me", (route) =>
    route.fulfill({
      json: { name: "Administrator", username: "admin", admin: true },
    }),
  );
  await page.route("**/api/vnc/targets", (route) =>
    route.fulfill({ json: { targets: [] } }),
  );
  await page.goto("/vnc.html");
  await expect(
    page.getByRole("heading", { name: "VNC 连接", exact: true }),
  ).toBeVisible();
  await expect(page.locator("#admin-panel")).toBeHidden();
});

test("RDP connection editor does not expose grouping", async ({ page }) => {
  await page.goto("/admin.html");
  await expect(page.locator("#connection-group")).toHaveCount(0);
  await expect(page.getByText("连接分组")).toHaveCount(0);
});

test("user management exposes the complete account lifecycle", async ({
  page,
}) => {
  await page.addInitScript(() =>
    sessionStorage.setItem("remote-gateway-access-token", "test"),
  );
  await page.route("**/api/admin/users", (route) =>
    route.fulfill({
      json: {
        items: [
          {
            id: 0,
            name: "Administrator",
            username: "admin",
            admin: true,
            enabled: true,
            allowedTargets: [],
          },
          {
            id: 1,
            name: "Test User",
            username: "test",
            admin: false,
            enabled: true,
            allowedTargets: [],
          },
        ],
      },
    }),
  );
  await page.route("**/api/admin/state", (route) =>
    route.fulfill({
      json: { targets: [{ id: "rdp-1", name: "Windows", managed: true }] },
    }),
  );
  await page.route("**/api/admin/vnc", (route) =>
    route.fulfill({
      json: { connections: [{ id: "vnc-1", name: "Desktop" }] },
    }),
  );
  await page.route("**/api/admin/ssh", (route) =>
    route.fulfill({ json: { connections: [{ id: "ssh-1", name: "Linux" }] } }),
  );
  await page.goto("/users.html");
  await expect(page.getByRole("button", { name: "添加用户" })).toBeVisible();
  await expect(page.getByRole("button", { name: "编辑" })).toHaveCount(2);
  await expect(page.getByRole("button", { name: "重置密码" })).toHaveCount(2);
  await expect(page.getByRole("button", { name: "冻结" })).toHaveCount(1);
  await expect(page.getByRole("button", { name: "授权" })).toHaveCount(1);
  await page.getByRole("button", { name: "授权" }).click();
  await expect(page.locator("#permissions")).toBeVisible();
  await expect(
    page.locator("#permissions").getByRole("heading", { name: "RDP" }),
  ).toBeVisible();
  await expect(
    page.locator("#permissions").getByRole("heading", { name: "VNC" }),
  ).toBeVisible();
  await expect(
    page.locator("#permissions").getByRole("heading", { name: "SSH" }),
  ).toBeVisible();
  await expect(page.locator("#permissions").getByText("Windows")).toBeVisible();
  await expect(page.locator("#permissions").getByText("Desktop")).toBeVisible();
  await expect(page.locator("#permissions").getByText("Linux")).toBeVisible();
});
