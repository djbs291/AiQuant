# AiQuant

AiQuant is a C++20 engine for quantitative-trading research. It reads tick data from CSV, resamples it into OHLCV candles, computes technical indicators, trains a linear model — a closed-form ridge fit, or stochastic gradient descent that can go on learning as new candles close — to predict the next candle's close-to-close delta, and backtests a rule-based signal engine driven by those predictions.

The whole pipeline is described by a single INI "scenario" and can be driven three ways: the `aiquant` CLI, the `aiquant_http` microservice, and the `aiquant_api` Python module.

- Architecture and roadmap: `docs/CppFinancialAIEngine.md`, per-layer designs in `docs/*LayerDesign_*.md`
- Current state, known issues and priorities: `docs/ProjectStatus.md`
- Scenario INI grammar: `docs/ScenarioConfig.md`

## Build

Requires CMake 3.20+, Ninja and a C++20 compiler. Python bindings are ON by default, and configuring **fails** without pybind11 (`brew install pybind11`, `pip install pybind11`, or `apt install pybind11-dev`). Pass `-DAIQUANT_BUILD_PYTHON=OFF` to skip them.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Test

```bash
ctest --test-dir build --output-on-failure            # C++ tests, one entry per TEST_CASE
ctest --test-dir build -L unit                        # or -L golden
./build/aiquant_tests "[rsi]"                         # Catch2 tag filtering
PYTHONPATH=build python3 tests/python/smoke_test.py   # Python module smoke test
python3 tests/http/smoke_test.py                      # HTTP service smoke test
```

Tests use Catch2 v3, taken from the system package when one is installed and fetched otherwise. `-DAIQUANT_USE_BUNDLED_CATCH=ON` falls back to the bundled minicatch for offline builds, at the cost of filtering.

Golden tests compare the indicators against [TA-Lib](https://ta-lib.org/) and are off by default. Install the C library (`brew install ta-lib`, or the `TA-Lib/setup-ta-lib` action) and configure with `-DAIQUANT_WITH_TALIB=ON`.

## CLI

Subcommands: `features`, `backtest`, `train-linear`, `run-mvp`, `run-config`. Run `./build/aiquant` without arguments for usage.

```bash
./build/aiquant run-config scenarios/mvp.ini            # ready-to-run example scenario
./build/aiquant run-config scenarios/mvp.ini --json
./build/aiquant run-mvp scenarios/ticks_mvp.csv --tf M1 --model-out model.csv
./build/aiquant backtest scenarios/ticks_mvp.csv --model-linear model.csv
```

A scenario needs enough candles to clear indicator warmup: about 33 with the default periods, plus at least 3 feature rows. `scenarios/ticks_mvp.csv` is a synthetic 300-tick file that satisfies this; the small `ticks_*.csv` files written by the tests do not.

## Scenario Configs

`aiquant run-config` executes full scenarios. The supported keys and grammar are documented in `docs/ScenarioConfig.md`, and a ready-to-run example lives at `scenarios/mvp.ini` (pointing to `scenarios/ticks_mvp.csv`). Use these as a template when wiring new experiments.

The model's feature set is part of the scenario. `features = close,ema_fast,rsi,atr,adx` picks any of the 20 names in the catalogue (every output of the 11 indicators, `vwap` included); omit the key and you get the historical six. The resolved set is reported in the JSON and written into the trained model file, so a model cannot be applied to a feature set it was not trained on without noticing:

```bash
./build/aiquant run-mvp scenarios/ticks_mvp.csv --features close,ema_fast,rsi,atr,adx --json | jq .features
```

So is the trainer. `model = sgd` swaps the closed-form ridge fit for stochastic gradient descent, and `online_update = true` keeps that model training through the out-of-sample stretch: one update per candle that closes, always one row behind, so nothing is read before it has happened. The run then reports `online_validation_rmse` — the same rows scored predict-then-learn — beside `validation_rmse`, which scores the model as trained:

```bash
./build/aiquant run-config examples/sgd_online.ini --json \
  | jq '{model, validation_rmse, online_validation_rmse, online_updates}'
```

An SGD run is saved and served as a plain linear model, with the standardizer folded into the weights, so `--model-linear` and `/predict` need no changes to consume one.

## C++ / Python API

The `fin::api::ScenarioService` offers a stable programmatic entry point for running scenarios. Link against the `fin_api` static library and call `ScenarioService::run` or `ScenarioService::run_file`. Python bindings are implemented with [pybind11](https://pybind11.readthedocs.io/) (installable via `pip install pybind11`) and expose the same helpers. Build + import example:

```bash
cmake -DAIQUANT_BUILD_PYTHON=ON -S . -B build
cmake --build build --target aiquant_api
PYTHONPATH=build python3 -c "import aiquant_api as aq; print(aq.run_config({'ticks_path': 'scenarios/ticks_mvp.csv'})['metrics'])"
```

Use the interpreter CMake found (printed as `Found Python3` at configure time); the extension module is built against that ABI. `cmake` automatically checks `python -m pybind11 --cmakedir` when locating the package, but you can always pass `-DCMAKE_PREFIX_PATH=$(python -m pybind11 --cmakedir)` explicitly if you use a custom Python environment.

## HTTP Microservice

`aiquant_http` exposes the scenario runner over HTTP. Example usage:

```bash
./build/aiquant_http --port 8080 --root scenarios --model model.csv --static examples/dashboard &
curl http://localhost:8080/health                                              # liveness
curl -X POST http://localhost:8080/run-file --data "mvp.ini"                   # body = path under --root
curl -X POST http://localhost:8080/run-config --data-binary @scenarios/mvp.ini # body = raw INI
curl -X POST http://localhost:8080/predict -d '{"features": {"close": 100, "rsi": 55}}'
curl -X POST http://localhost:8080/signal  -d '{"close": 100, "rsi": 20, "ema_fast": 11, "ema_slow": 10}'
```

### `/predict` and `/signal`

These two take a **JSON object** (the scenario endpoints keep taking INI) and answer with JSON.

`POST /predict` applies a trained model to one set of feature values:

```json
{"model": "model.csv", "features": {"close": 100.0, "rsi": 55.0}}
```

`model` is optional and resolves under `--root`, exactly like `/run-file`; without it the `--model` given at startup is used. The values are reordered to the column order recorded in the model file, and a missing or unexpected feature is a `400` naming it — the model is never applied to a feature set it was not trained on. The reply carries `prediction`, the `features` order actually used, and the `model` path.

`POST /signal` evaluates the rule engine:

```json
{"close": 100.0, "rsi": 20.0, "ema_fast": 11.0, "ema_slow": 10.0,
 "prediction": 0.5, "rsi_buy": 30.0, "rsi_sell": 70.0, "use_ema_crossover": true}
```

Every field is optional. Supply `prediction` to evaluate the rules against a number you already have, or supply `features` (plus `model`) and the service predicts first. With neither, it answers on the indicator rules alone. The reply carries `signal` (`Buy`/`Sell`/`Hold`), `score`, `reason`, `symbol`, `prediction` (or `null`) and `features`.

`POST /run-file` takes a path to a scenario file; it is resolved against `--root` (default: the working directory) and anything outside that directory is refused. `POST /run-config` accepts raw INI contents and executes them via a temporary file. Both return the JSON emitted by the CLI `--json` flag.

| Option | Default | Meaning |
| --- | --- | --- |
| `--port` | 8080 | TCP port |
| `--root` | working directory | directory `/run-file` and request-supplied model paths resolve under |
| `--model` | none | default model for `/predict` and `/signal` |
| `--static` | off | directory served over `GET`; without it the service stays POST-only |
| `--max-body` | 1048576 | maximum request body in bytes |
| `--max-connections` | 32 | requests served concurrently before answering 503 |

Status codes: `200` on success, `400` for a malformed request or unparsable INI, `403` for a path outside `--root`, `404` for a missing file or unknown endpoint, `405` for a method other than POST (except `GET /health` and, with `--static`, GET of a served file), `413` for an oversized body, `422` for a valid scenario the engine cannot run (too few candles, say), `503` when the concurrency limit is reached, and `500` otherwise.

### Serving the dashboard

`--static DIR` turns on `GET` for files under `DIR`, and it is **off unless you pass it** — publishing a directory should be a deliberate act. With it on:

```bash
./build/aiquant_http --port 8080 --root scenarios --static examples/dashboard
```

then open <http://localhost:8080/>. The page is described in [`examples/README.md`](examples/README.md), along with runnable scenarios and API scripts.

The serving path is narrow on purpose: paths are confined to `DIR` the same way `/run-file` is confined to `--root` (a symlink pointing out is refused, not followed), percent-encoded paths are rejected rather than decoded, only a whitelist of extensions is served — `.svg` is excluded because SVG can carry script — dotfiles are refused, directories are never listed, and responses carry `nosniff` and a `default-src 'self'` policy. Asking for an API route with `GET` still answers `405`, not `404`. The server also refuses at startup to serve a directory that contains `--root`, which would otherwise publish your scenarios and models.

Each connection is served on its own thread. The service has no TLS and no authentication, and the `ticks` path inside a scenario is not restricted by `--root`, so keep it on a trusted network (see `docs/ProjectStatus.md`).
