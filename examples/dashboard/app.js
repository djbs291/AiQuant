"use strict";

// Talks to the aiquant_http service that served this page, so every request is same-origin
// and there is no configuration to get wrong. No build step and no CDN: the file you see is
// the file that runs.

const $ = (id) => document.getElementById(id);

// The default six, which is what a scenario trains on when it does not say otherwise.
const DEFAULT_FEATURES = [
  ["close", 100],
  ["ema_fast", 99.5],
  ["rsi", 55],
  ["macd", 0.2],
  ["macd_signal", 0.1],
  ["macd_hist", 0.1],
];

async function post(path, body, asJson) {
  const response = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": asJson ? "application/json" : "text/plain" },
    body: asJson ? JSON.stringify(body) : body,
  });
  const text = await response.text();
  let payload;
  try {
    payload = JSON.parse(text);
  } catch (err) {
    payload = { error: text.trim() || `HTTP ${response.status}` };
  }
  if (!response.ok) {
    throw new Error(payload.error || `HTTP ${response.status}`);
  }
  return payload;
}

function show(target, render) {
  const node = $(target);
  node.classList.remove("error");
  node.textContent = "…";
  return (work) =>
    work
      .then((value) => {
        node.classList.remove("error");
        node.innerHTML = "";
        node.append(render(value));
      })
      .catch((err) => {
        node.classList.add("error");
        node.textContent = err.message;
      });
}

function table(rows) {
  const el = document.createElement("table");
  for (const [key, value] of rows) {
    const tr = document.createElement("tr");
    const k = document.createElement("td");
    k.className = "key";
    k.textContent = key;
    const v = document.createElement("td");
    v.textContent = value;
    tr.append(k, v);
    el.append(tr);
  }
  return el;
}

function number(value, digits = 4) {
  return typeof value === "number" ? value.toFixed(digits) : String(value);
}

// ---- health -------------------------------------------------------------------------------

async function checkHealth() {
  const node = $("status");
  try {
    const response = await fetch("/health");
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    node.textContent = "service up";
    node.className = "status up";
  } catch (err) {
    node.textContent = `service unreachable: ${err.message}`;
    node.className = "status down";
  }
}

// ---- scenario -----------------------------------------------------------------------------

$("run-scenario").addEventListener("click", () => {
  const render = (result) =>
    table([
      ["features", result.features.join(", ")],
      ["candles", `${result.candles} (warmup ${result.warmup_candles})`],
      ["feature rows", result.feature_rows],
      ["validation RMSE", number(result.validation_rmse, 6)],
      ["trades", `${result.metrics.trades} (${result.metrics.wins}W / ${result.metrics.losses}L)`],
      ["PnL", `${number(result.metrics.pnl)} (${number(result.metrics.return_pct, 6)}%)`],
      ["max drawdown", `${number(result.metrics.max_drawdown, 6)}%`],
    ]);
  show("scenario-out", render)(post("/run-config", $("scenario-ini").value, false));
});

// ---- predict ------------------------------------------------------------------------------

function addFeatureRow(name = "", value = "") {
  const row = document.createElement("div");
  row.className = "feature-row";

  const nameInput = document.createElement("input");
  nameInput.className = "name";
  nameInput.placeholder = "feature";
  nameInput.value = name;

  const valueInput = document.createElement("input");
  valueInput.className = "value";
  valueInput.type = "number";
  valueInput.step = "any";
  valueInput.value = value;

  const remove = document.createElement("button");
  remove.type = "button";
  remove.textContent = "×";
  remove.addEventListener("click", () => row.remove());

  row.append(nameInput, valueInput, remove);
  $("feature-rows").append(row);
}

$("add-feature").addEventListener("click", () => addFeatureRow());

$("run-predict").addEventListener("click", () => {
  const features = {};
  for (const row of document.querySelectorAll(".feature-row")) {
    const name = row.querySelector(".name").value.trim();
    const value = row.querySelector(".value").value;
    if (name !== "" && value !== "") features[name] = Number(value);
  }

  const body = { features };
  const model = $("predict-model").value.trim();
  if (model !== "") body.model = model;

  const render = (result) =>
    table([
      ["prediction", number(result.prediction, 6)],
      ["features", result.features.join(", ")],
      ["model", result.model],
    ]);
  show("predict-out", render)(post("/predict", body, true));
});

// ---- signal -------------------------------------------------------------------------------

function optionalNumber(id) {
  const raw = $(id).value;
  return raw === "" ? undefined : Number(raw);
}

$("run-signal").addEventListener("click", () => {
  const body = { use_ema_crossover: $("sig-xover").checked };
  const fields = {
    close: "sig-close",
    rsi: "sig-rsi",
    ema_fast: "sig-ema-fast",
    ema_slow: "sig-ema-slow",
    prediction: "sig-prediction",
    rsi_buy: "sig-rsi-buy",
    rsi_sell: "sig-rsi-sell",
  };
  for (const [key, id] of Object.entries(fields)) {
    const value = optionalNumber(id);
    if (value !== undefined) body[key] = value;
  }

  const render = (result) => {
    const box = document.createElement("div");
    const verdict = document.createElement("div");
    verdict.className = `verdict ${result.signal}`;
    verdict.textContent = result.signal;
    box.append(
      verdict,
      table([
        ["score", number(result.score, 2)],
        ["reason", result.reason || "—"],
        ["prediction", result.prediction === null ? "none" : number(result.prediction, 6)],
        ["features", result.features.length ? result.features.join(", ") : "—"],
      ])
    );
    return box;
  };
  show("signal-out", render)(post("/signal", body, true));
});

// ---- boot ---------------------------------------------------------------------------------

for (const [name, value] of DEFAULT_FEATURES) addFeatureRow(name, value);
checkHealth();
