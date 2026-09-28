import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const root = new URL("./", import.meta.url);
const html = await readFile(new URL("index.html", root), "utf8");
const css = await readFile(new URL("styles.css", root), "utf8");
const app = await readFile(new URL("app.js", root), "utf8");

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
