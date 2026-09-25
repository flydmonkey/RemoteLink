const [baseURL, username = "admin", password = "admin"] = process.argv.slice(2);
if (!baseURL)
  throw new Error("usage: get_test_token.mjs BASE_URL [USERNAME] [PASSWORD]");
const response = await fetch(new URL("/api/auth/login", baseURL), {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ username, password }),
});
if (!response.ok)
  throw new Error(
    `test login failed: ${response.status} ${await response.text()}`,
  );
process.stdout.write((await response.json()).token);
