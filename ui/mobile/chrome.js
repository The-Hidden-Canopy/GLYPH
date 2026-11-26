"use strict";
(function () {
  const select = document.getElementById("chrome-theme");
  const themes = new Set(["system", "dark", "light"]);
  const system = window.matchMedia("(prefers-color-scheme: dark)");
  let preference = "system";
  try { const value = localStorage.getItem("glyph.theme"); if (themes.has(value)) preference = value; } catch (_) {}
  function apply() {
    document.documentElement.dataset.theme = preference === "system" ? (system.matches ? "dark" : "light") : preference;
    select.value = preference;
    try { localStorage.setItem("glyph.theme", preference); } catch (_) {}
  }
  select.addEventListener("change", () => { preference = themes.has(select.value) ? select.value : "system"; apply(); });
  system.addEventListener("change", () => { if (preference === "system") apply(); });
  apply();
  // No verified release provider exists in this source-only preview. The
  // update position stays hidden, inert, and without a download destination.
})();
