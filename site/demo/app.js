"use strict";

// A standalone client for the AiQuant HTTP API. Unlike examples/dashboard (same-origin, no key),
// this talks to a configurable base URL with an API key, so it can run from GitHub Pages or
// locally against the live service. No build step, no CDN — the file you see is the file that runs.

const $ = (id) => document.getElementById(id);
const DEFAULT_BASE = "https://aiquant-http-ukqi.onrender.com";

// ---- settings (persisted in this browser only) --------------------------------------------

function loadSettings() {
  try {
    $("base-url").value = localStorage.getItem("aiquant.baseUrl") || DEFAULT_BASE;
    $("api-key").value = localStorage.getItem("aiquant.apiKey") || "";
  } catch (_) {
    $("base-url").value = DEFAULT_BASE;
  }
}
function baseUrl() {
  const v = $("base-url").value.trim().replace(/\/+$/, "");
  return v || DEFAULT_BASE;
}
function apiKey() {
  return $("api-key").value.trim();
}
function saveSettings() {
  try {
    localStorage.setItem("aiquant.baseUrl", baseUrl());
    localStorage.setItem("aiquant.apiKey", apiKey());
  } catch (_) {
    /* private window / storage blocked: fine, just not remembered */
  }
}

// ---- API helper ----------------------------------------------------------------------------

async function api(path, body, asJson) {
  const headers = {};
  if (asJson !== null) headers["Content-Type"] = asJson ? "application/json" : "text/plain";
  const key = apiKey();
  if (key) headers["X-API-Key"] = key;

  const response = await fetch(baseUrl() + path, {
    method: body === undefined ? "GET" : "POST",
    headers,
    body: body === undefined ? undefined : asJson ? JSON.stringify(body) : body,
  });
  const text = await response.text();
  let payload;
  try { payload = JSON.parse(text); } catch (_) { payload = { error: text.trim() || `HTTP ${response.status}` }; }
  if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
  return payload;
}

function show(target, render) {
  const node = $(target);
  node.classList.remove("error");
  node.textContent = "…";
  return (work) =>
    work
      .then((value) => { node.classList.remove("error"); node.innerHTML = ""; node.append(render(value)); })
      .catch((err) => { node.classList.add("error"); node.textContent = err.message; });
}

function number(value, digits = 4) {
  return typeof value === "number" ? value.toFixed(digits) : String(value);
}

// ---- health --------------------------------------------------------------------------------

async function checkHealth() {
  const node = $("status");
  node.textContent = "connecting…"; node.className = "status";
  try {
    const r = await fetch(baseUrl() + "/health");
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    node.textContent = "service up"; node.className = "status up";
  } catch (err) {
    node.textContent = `unreachable: ${err.message}`; node.className = "status down";
  }
}

// ---- tabs ----------------------------------------------------------------------------------

for (const tab of document.querySelectorAll(".tab")) {
  tab.addEventListener("click", () => {
    for (const t of document.querySelectorAll(".tab")) t.classList.toggle("active", t === tab);
    const name = tab.dataset.tab;
    for (const p of document.querySelectorAll(".tabpanel")) p.classList.toggle("active", p.id === name);
  });
}

// ---- data: synthetic / sample / upload -----------------------------------------------------

function syntheticCsv(rows = 2500) {
  const lines = ["Timestamp,symbol,price,volume"];
  let price = 100;
  let t = Date.now() - rows * 60000;
  for (let i = 0; i < rows; i++) {
    price += (Math.random() - 0.5) * 0.4 + Math.sin(i / 40) * 0.05;
    if (price < 1) price = 1;
    const vol = (0.5 + Math.random()).toFixed(3);
    lines.push(`${t},SYNTH,${price.toFixed(4)},${vol}`);
    t += 60000;
  }
  return lines.join("\n") + "\n";
}

function setTicks(csv) {
  $("ticks").value = csv;
  const rows = csv.trim() ? csv.trim().split("\n").length - 1 : 0; // minus header
  $("ticks-meta").textContent = rows > 0 ? `${rows} rows loaded` : "";
}

$("gen-data").addEventListener("click", () => setTicks(syntheticCsv()));
$("load-sample").addEventListener("click", async () => {
  try { setTicks(await (await fetch("sample.csv")).text()); }
  catch (_) { setTicks(syntheticCsv()); }
});
$("file").addEventListener("change", (e) => {
  const file = e.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => setTicks(String(reader.result));
  reader.readAsText(file);
});

// ---- backtest (/run-inline) ----------------------------------------------------------------

function tiles(metrics) {
  const wrap = document.createElement("div");
  wrap.className = "tiles";
  const add = (k, v, cls) => {
    const t = document.createElement("div"); t.className = "tile";
    const kk = document.createElement("div"); kk.className = "k"; kk.textContent = k;
    const vv = document.createElement("div"); vv.className = "v" + (cls ? " " + cls : ""); vv.textContent = v;
    t.append(kk, vv); wrap.append(t);
  };
  const sign = (n) => (n > 0 ? "pos" : n < 0 ? "neg" : "");
  add("PnL", number(metrics.pnl, 2), sign(metrics.pnl));
  add("Return", number(metrics.return_pct, 3) + "%", sign(metrics.return_pct));
  add("Trades", String(metrics.trades));
  add("Win / Loss", `${metrics.wins} / ${metrics.losses}`);
  add("Max DD", number(metrics.max_drawdown, 3) + "%");
  add("Model-decided", String(metrics.model_decisive_signals));
  return wrap;
}

function lineChart(preview) {
  const pts = preview
    .filter((p) => typeof p.predicted_delta === "number" && typeof p.actual_delta === "number")
    .map((p) => [p.predicted_delta, p.actual_delta]);
  if (pts.length < 2) return document.createTextNode("");

  const W = 640, H = 200, padL = 36, padB = 16, padT = 8, padR = 8;
  let lo = Infinity, hi = -Infinity;
  for (const [pr, ac] of pts) { lo = Math.min(lo, pr, ac); hi = Math.max(hi, pr, ac); }
  if (lo === hi) { lo -= 1; hi += 1; }
  const x = (i) => padL + (i / (pts.length - 1)) * (W - padL - padR);
  const y = (v) => padT + (1 - (v - lo) / (hi - lo)) * (H - padT - padB);
  const path = (idx) => pts.map((p, i) => `${i === 0 ? "M" : "L"}${x(i).toFixed(1)},${y(p[idx]).toFixed(1)}`).join(" ");
  const zeroY = lo <= 0 && hi >= 0 ? y(0) : null;

  const svg = `
    <svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" role="img" aria-label="Predicted vs actual delta">
      ${zeroY !== null ? `<line x1="${padL}" y1="${zeroY}" x2="${W - padR}" y2="${zeroY}" stroke="#2b323d" stroke-width="1"/>` : ""}
      <path d="${path(1)}" fill="none" stroke="#3ddc97" stroke-width="1.5"/>
      <path d="${path(0)}" fill="none" stroke="#5b9dff" stroke-width="1.5"/>
    </svg>`;
  const box = document.createElement("div");
  box.className = "chart";
  box.innerHTML = svg;
  const legend = document.createElement("div");
  legend.className = "legend";
  legend.innerHTML = `<span class="pred">— predicted</span>&nbsp;&nbsp;<span class="act">— actual</span> · next-close delta over ${pts.length} validation rows`;
  box.append(legend);
  return box;
}

function renderBacktest(r) {
  const box = document.createElement("div");

  const head = document.createElement("div");
  head.className = "hint";
  head.textContent =
    `${r.symbol || "?"} · ${r.timeframe} · ${r.model} · ${r.candles} candles ` +
    `(warmup ${r.warmup_candles}) · ${r.feature_rows} feature rows · ` +
    `validation RMSE ${number(r.validation_rmse, 6)}`;
  box.append(head);

  box.append(Object.assign(document.createElement("div"),
    { className: "section-label", textContent: `Out of sample (${r.out_of_sample_candles} candles — the headline)` }));
  box.append(tiles(r.metrics));
  box.append(Object.assign(document.createElement("div"),
    { className: "section-label", textContent: `In sample (${r.in_sample_candles} candles — training stretch, for comparison)` }));
  box.append(tiles(r.metrics_in_sample));

  if (Array.isArray(r.validation_preview) && r.validation_preview.length > 1) {
    box.append(Object.assign(document.createElement("div"),
      { className: "section-label", textContent: "Predicted vs actual (validation)" }));
    box.append(lineChart(r.validation_preview));
  }
  return box;
}

$("run-backtest").addEventListener("click", () => {
  saveSettings();
  const ticks_csv = $("ticks").value.trim();
  if (!ticks_csv) { show("backtest-out", () => document.createTextNode(""))(Promise.reject(new Error("No tick data — generate or paste some first."))); return; }

  const lines = [`tf = ${$("tf").value}`, `model = ${$("model").value}`];
  const num = (id, key) => { const v = $(id).value.trim(); if (v !== "") lines.push(`${key} = ${v}`); };
  num("train-ratio", "train_ratio");
  num("model-weight", "model_weight");
  num("cash", "cash");
  num("qty", "qty");
  num("fee", "fee");
  num("preview", "preview");
  const features = $("features").value.trim();
  if (features) lines.push(`features = ${features}`);

  const body = { ticks_csv, config: lines.join("\n") + "\n" };
  show("backtest-out", renderBacktest)(api("/run-inline", body, true));
});

// ---- signal (/signal) ----------------------------------------------------------------------

function optionalNumber(id) {
  const raw = $(id).value;
  return raw === "" ? undefined : Number(raw);
}

$("run-signal").addEventListener("click", () => {
  saveSettings();
  const body = { use_ema_crossover: $("sig-xover").checked };
  const fields = {
    close: "sig-close", rsi: "sig-rsi", ema_fast: "sig-ema-fast", ema_slow: "sig-ema-slow",
    prediction: "sig-prediction", model_weight: "sig-model-weight", rsi_buy: "sig-rsi-buy", rsi_sell: "sig-rsi-sell",
  };
  for (const [key, id] of Object.entries(fields)) {
    const v = optionalNumber(id);
    if (v !== undefined) body[key] = v;
  }

  const render = (r) => {
    const box = document.createElement("div");
    const verdict = document.createElement("div");
    verdict.className = `verdict ${r.signal}`;
    verdict.textContent = r.signal;
    box.append(verdict, table([
      ["score", number(r.score, 2)],
      ["reason", r.reason || "—"],
      ["model decided it", r.model_decisive ? "yes" : "no"],
      ["prediction", r.prediction === null ? "none" : number(r.prediction, 6)],
    ]));
    return box;
  };
  show("signal-out", render)(api("/signal", body, true));
});

// ---- predict (/predict) --------------------------------------------------------------------

const DEFAULT_FEATURES = [["close", 100], ["ema_fast", 99.5], ["rsi", 55], ["macd", 0.2], ["macd_signal", 0.1], ["macd_hist", 0.1]];

function addFeatureRow(name = "", value = "") {
  const row = document.createElement("div");
  row.className = "feature-row";
  const n = document.createElement("input"); n.className = "name"; n.placeholder = "feature"; n.value = name;
  const v = document.createElement("input"); v.className = "value"; v.type = "number"; v.step = "any"; v.value = value;
  const x = document.createElement("button"); x.type = "button"; x.textContent = "×";
  x.addEventListener("click", () => row.remove());
  row.append(n, v, x);
  $("feature-rows").append(row);
}

$("add-feature").addEventListener("click", () => addFeatureRow());

$("run-predict").addEventListener("click", () => {
  saveSettings();
  const features = {};
  for (const row of document.querySelectorAll("#predict .feature-row")) {
    const name = row.querySelector(".name").value.trim();
    const value = row.querySelector(".value").value;
    if (name !== "" && value !== "") features[name] = Number(value);
  }
  const body = { features };
  const model = $("predict-model").value.trim();
  if (model !== "") body.model = model;

  const render = (r) => table([
    ["prediction", number(r.prediction, 6)],
    ["features", r.features.join(", ")],
    ["model", r.model],
  ]);
  show("predict-out", render)(api("/predict", body, true));
});

function table(rows) {
  const el = document.createElement("table");
  for (const [key, value] of rows) {
    const tr = document.createElement("tr");
    const k = document.createElement("td"); k.className = "key"; k.textContent = key;
    const v = document.createElement("td"); v.textContent = value;
    tr.append(k, v); el.append(tr);
  }
  return el;
}

// ---- boot ----------------------------------------------------------------------------------

loadSettings();
for (const [n, v] of DEFAULT_FEATURES) addFeatureRow(n, v);
$("base-url").addEventListener("change", () => { saveSettings(); checkHealth(); });
$("api-key").addEventListener("change", saveSettings);
checkHealth();
