# Examples

Runnable examples for the CLI, the Python module and the HTTP service. Everything here uses
`scenarios/ticks_mvp.csv`, which ships with the repository, so there is no data to fetch.

Run them from the **repository root** — relative paths resolve against the working directory.

| File | What it shows |
| --- | --- |
| `wide_features.ini` | a scenario with a wider feature set than the default six |
| `run_scenario.py` | the `aiquant_api` Python module, including how errors surface |
| `api_calls.sh` | every HTTP endpoint with `curl`, success and failure |
| `dashboard/` | a small page that drives the service from a browser |

## The scenario

```bash
./build/aiquant run-config examples/wide_features.ini --json | jq '.features, .warmup_candles'
```

Omit the `features` key and you get the historical six (`close`, `ema_fast`, `rsi`, `macd`,
`macd_signal`, `macd_hist`). The catalogue of names is in `docs/ScenarioConfig.md`.

## The Python module

```bash
PYTHONPATH=build python3 examples/run_scenario.py
```

Use the interpreter CMake found — it is printed as `Found Python3` at configure time, and the
extension module is built against that ABI.

## The HTTP service

```bash
./build/aiquant run-mvp scenarios/ticks_mvp.csv --tf M1 --model-out scenarios/model.csv
./build/aiquant_http --port 8080 --root scenarios --static examples/dashboard &
./examples/api_calls.sh 8080
```

`--root` confines both scenario and model paths; `--static` is what serves the dashboard, and
it is off unless you pass it.

## The dashboard

With the service started as above, open <http://localhost:8080/>. Three panels, one per thing
the engine can actually do: run a scenario, predict from feature values, and evaluate the
signal rules. It is plain HTML, CSS and JavaScript — no build step, and nothing loaded from a
CDN, so it works offline.
