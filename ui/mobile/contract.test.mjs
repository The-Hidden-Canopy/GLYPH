import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const root = new URL("./", import.meta.url);
const html = await readFile(new URL("index.html", root), "utf8");
const css = await readFile(new URL("styles.css", root), "utf8");
const app = await readFile(new URL("app.js", root), "utf8");

test("primary navigation exposes the current view to assistive technology", () => {
  assert.equal([...html.matchAll(/aria-current="page"/g)].length, 1);
  assert.match(html, /class="nav-button is-active"[^>]+aria-current="page"[^>]+data-mode="send"/);
  assert.match(app, /const selected = button.dataset.mode === mode;/);
  assert.match(app, /button.setAttribute\("aria-current", "page"\)/);
  assert.match(app, /button.removeAttribute\("aria-current"\)/);
});

test("mobile UI exposes the explicit send and receive boundary", () => {
  assert.match(html, /id="send-view"/);
  assert.match(html, /id="receive-view"/);
  assert.match(html, /id="run-synthetic-send"/);
  assert.match(html, /id="run-synthetic-receive"/);
  assert.match(html, /SHA-256/);
  assert.match(html, /OFFLINE ONLY/);
});

test("mobile UI has a visible final integrity gate", () => {
  assert.match(html, /id="quality-hash"/);
  assert.match(html, /id="boundary-text"/);
  assert.match(app, /INTEGRITY_GATE_BLOCKED/);
  assert.match(app, /source and destination hashes are not available/);
  assert.match(app, /verified: false/);
});

test("preview does not add a network or remote-service path", () => {
  assert.doesNotMatch(app, /\bfetch\s*\(/);
  assert.doesNotMatch(app, /XMLHttpRequest/);
  assert.doesNotMatch(app, /WebSocket/);
  assert.match(css, /env\(safe-area-inset-bottom\)/);
  assert.match(css, /prefers-reduced-motion/);
});

test("mobile preview keeps the optical surface visibly synthetic", () => {
  assert.match(html, /PREVIEW · NOT A LIVE TRANSMISSION/);
  assert.match(app, /Synthetic walkthrough only/);
  assert.match(app, /decoder is not attached/);
});

test("landing and navigation descriptions accurately disclose preview-only behavior", () => {
  assert.match(html, /No live transfer is available/);
  assert.match(app, /No live transfer is available/);
  assert.match(app, /No receiver or decoder is connected/);
  assert.match(app, /No durable transfer history is available/);
  assert.doesNotMatch(app, /Inspect local sessions, durable checkpoints/);
});

test("native-host-independent shell appearance does not activate transfer or update transport", async () => {
  const chrome = await readFile(new URL("chrome.js", root), "utf8");
  assert.match(html, /id="chrome-theme"/);
  assert.match(html, /id="update-pill"[^>]*hidden/);
  assert.match(css, /data-theme="light"/);
  assert.match(chrome, /prefers-color-scheme/);
  assert.doesNotMatch(chrome, /\b(fetch|WebSocket|XMLHttpRequest|getUserMedia)\b/);
  assert.doesNotMatch(chrome, /update-pill.*hidden\s*=\s*false/);
});
test("approved icon is used as the mobile preview brand and favicon", async () => {
  const {createHash} = await import("node:crypto");
  const icon = await readFile(new URL("glyph-icon.png", root));
  assert.equal(createHash("sha256").update(icon).digest("hex"), "fa5a181247ef957a94b1caf0bb265ae22bf465b8b679a4d211ccb625e9762057");
  assert.match(html, /src="glyph-icon.png"/);
  assert.match(html, /rel="icon" type="image\/png" href="glyph-icon.png"/);
  const manifest = JSON.parse(await readFile(new URL("manifest.webmanifest", root), "utf8"));
  assert.equal(manifest.display, "browser");
  assert.equal(manifest.icons[0].src, "glyph-icon.png");
  assert.match(manifest.name, /Preview/);
});
